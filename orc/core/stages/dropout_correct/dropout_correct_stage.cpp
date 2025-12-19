/*
 * File:        dropout_correct_stage.cpp
 * Module:      orc-core
 * Purpose:     Dropout correction stage
 *
 * This stage corrects video dropouts by replacing corrupted samples with data
 * from other lines/fields. The output has corrected data, so get_dropout_hints()
 * returns empty (no dropouts remain). The original dropout locations can still
 * be retrieved via get_corrected_regions() for visualization/debugging.
 *
 * Hint Semantics: Outputs describe corrected state (no dropouts)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include <dropout_correct_stage.h>
#include "tbc_metadata.h"
#include "tbc_video_field_representation.h"
#include "stage_registry.h"
#include "logging.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace orc {

// Register the stage
static StageRegistration dropout_correct_registration([]() {
    return std::make_shared<DropoutCorrectStage>();
});

// DAGStage::execute() implementation
std::vector<ArtifactPtr> DropoutCorrectStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    if (inputs.empty()) {
        throw DAGExecutionError("DropoutCorrectStage requires at least one input");
    }
    
    // First input should be a VideoFieldRepresentation
    auto source = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    if (!source) {
        throw DAGExecutionError("DropoutCorrectStage input must be VideoFieldRepresentation");
    }
    
    ORC_LOG_DEBUG("DropoutCorrectStage::execute - Source type: {}", source->type_name());
    
    // Apply parameters if provided
    if (!parameters.empty()) {
        ORC_LOG_DEBUG("DropoutCorrectStage: applying {} parameters", parameters.size());
        const_cast<DropoutCorrectStage*>(this)->set_parameters(parameters);
    }
    
    ORC_LOG_DEBUG("DropoutCorrectStage config AFTER params: highlight={}, intrafield_only={}, overcorrect={}",
                  config_.highlight_corrections, config_.intrafield_only, config_.overcorrect_extension);
    
    // Get field range
    auto range = source->field_range();
    if (!range.is_valid()) {
        std::cerr << "DropoutCorrectStage: Invalid field range\n";
        std::vector<ArtifactPtr> outputs;
        outputs.push_back(std::const_pointer_cast<VideoFieldRepresentation>(source));
        return outputs;
    }
    
    // Return the corrected representation (lazy - corrections computed on-demand per field)
    auto corrected = std::make_shared<CorrectedVideoFieldRepresentation>(
        source, 
        const_cast<DropoutCorrectStage*>(this),
        config_.highlight_corrections
    );
    
    std::vector<ArtifactPtr> outputs;
    outputs.push_back(std::static_pointer_cast<VideoFieldRepresentation>(corrected));
    return outputs;
}

// CorrectedVideoFieldRepresentation implementation

CorrectedVideoFieldRepresentation::CorrectedVideoFieldRepresentation(
    std::shared_ptr<const VideoFieldRepresentation> source,
    DropoutCorrectStage* stage,
    bool highlight_corrections)
    : VideoFieldRepresentationWrapper(source, ArtifactID("corrected_field"), Provenance{})
    , stage_(stage), highlight_corrections_(highlight_corrections)
{
}

void CorrectedVideoFieldRepresentation::ensure_field_corrected(FieldID field_id) const {
    // Check if already processed
    if (processed_fields_.count(field_id)) {
        return;
    }
    
    ORC_LOG_DEBUG("CorrectedVideoFieldRepresentation: processing field {}", field_id.value());
    
    // Mark as processed
    processed_fields_.insert(field_id);
    
    // Correct this field
    stage_->correct_single_field(const_cast<CorrectedVideoFieldRepresentation*>(this), source_, field_id);
}

const uint16_t* CorrectedVideoFieldRepresentation::get_line(FieldID id, size_t line) const {
    // Ensure this field has been corrected
    ensure_field_corrected(id);
    
    // Check if we have a corrected version of this line
    auto key = std::make_pair(id, static_cast<uint32_t>(line));
    auto it = corrected_lines_.find(key);
    
    if (it != corrected_lines_.end()) {
        return it->second.data();
    }
    
    // Return original line
    return source_->get_line(id, line);
}

std::vector<uint16_t> CorrectedVideoFieldRepresentation::get_field(FieldID id) const {
    // Get descriptor to know field dimensions
    auto desc_opt = source_->get_descriptor(id);
    if (!desc_opt) {
        return {};
    }
    
    std::vector<uint16_t> field_data;
    field_data.reserve(desc_opt->width * desc_opt->height);
    
    // Assemble field from individual lines (corrected where applicable)
    for (size_t line = 0; line < desc_opt->height; ++line) {
        const uint16_t* line_data = get_line(id, line);
        field_data.insert(field_data.end(), line_data, line_data + desc_opt->width);
    }
    
    return field_data;
}

// DropoutCorrectStage implementation

std::shared_ptr<CorrectedVideoFieldRepresentation> DropoutCorrectStage::correct_field(
    std::shared_ptr<const VideoFieldRepresentation> source,
    FieldID /*field_id*/,
    const std::vector<DropoutRegion>& /*dropouts*/,
    const DropoutDecisions& /*decisions*/)
{
    // For now, this method creates a lazy corrected representation
    // The dropouts and decisions parameters are ignored since the source
    // should provide dropout hints
    // TODO: Support explicit dropout list and decisions
    
    auto corrected = std::make_shared<CorrectedVideoFieldRepresentation>(
        source, 
        const_cast<DropoutCorrectStage*>(this),
        config_.highlight_corrections
    );
    
    return corrected;
}

std::shared_ptr<CorrectedVideoFieldRepresentation> DropoutCorrectStage::correct_field_multisource(
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
    FieldID field_id,
    const std::vector<std::vector<DropoutRegion>>& all_dropouts,
    const DropoutDecisions& decisions)
{
    // For now, use the first source as primary and fall back to others
    // A more sophisticated implementation would analyze all sources and pick the best
    
    if (sources.empty()) {
        return nullptr;
    }
    
    // Use first source as primary
    auto primary_dropouts = all_dropouts.empty() ? std::vector<DropoutRegion>() : all_dropouts[0];
    return correct_field(sources[0], field_id, primary_dropouts, decisions);
}

DropoutCorrectStage::DropoutLocation DropoutCorrectStage::classify_dropout(
    const DropoutRegion& dropout,
    const FieldDescriptor& descriptor) const
{
    // We need video parameters to know color burst and active video regions
    // For now, use typical PAL/NTSC values
    // TODO: Get these from video parameters
    
    uint32_t color_burst_end = 0;
    uint32_t active_video_end = descriptor.width;
    
    // Rough estimates based on format
    if (descriptor.format == VideoFormat::PAL) {
        color_burst_end = 100;  // ~100 samples for PAL color burst
        active_video_end = descriptor.width - 20;
    } else if (descriptor.format == VideoFormat::NTSC) {
        color_burst_end = 80;   // ~80 samples for NTSC color burst  
        active_video_end = descriptor.width - 20;
    }
    
    if (dropout.start_sample <= color_burst_end) {
        return DropoutLocation::COLOUR_BURST;
    } else if (dropout.start_sample > color_burst_end && dropout.start_sample <= active_video_end) {
        return DropoutLocation::VISIBLE_LINE;
    }
    
    return DropoutLocation::UNKNOWN;
}

std::vector<DropoutRegion> DropoutCorrectStage::split_dropout_regions(
    const std::vector<DropoutRegion>& dropouts,
    const FieldDescriptor& descriptor) const
{
    std::vector<DropoutRegion> result;
    
    // Determine boundaries
    uint32_t color_burst_end = 0;
    uint32_t active_video_end = descriptor.width;
    
    if (descriptor.format == VideoFormat::PAL) {
        color_burst_end = 100;
        active_video_end = descriptor.width - 20;
    } else if (descriptor.format == VideoFormat::NTSC) {
        color_burst_end = 80;
        active_video_end = descriptor.width - 20;
    }
    
    for (const auto& dropout : dropouts) {
        auto location = classify_dropout(dropout, descriptor);
        
        if (location == DropoutLocation::COLOUR_BURST) {
            // Check if it extends beyond color burst
            if (dropout.end_sample > color_burst_end) {
                // Split into two regions
                DropoutRegion burst_region = dropout;
                burst_region.end_sample = color_burst_end;
                result.push_back(burst_region);
                
                DropoutRegion active_region = dropout;
                active_region.start_sample = color_burst_end + 1;
                result.push_back(active_region);
            } else {
                result.push_back(dropout);
            }
        } else if (location == DropoutLocation::VISIBLE_LINE) {
            // Check if it extends beyond active video
            if (dropout.end_sample > active_video_end) {
                // Truncate to active video end
                DropoutRegion truncated = dropout;
                truncated.end_sample = active_video_end;
                result.push_back(truncated);
            } else {
                result.push_back(dropout);
            }
        } else {
            result.push_back(dropout);
        }
    }
    
    return result;
}

DropoutCorrectStage::ReplacementLine DropoutCorrectStage::find_replacement_line(
    const VideoFieldRepresentation& source,
    FieldID field_id,
    uint32_t line,
    const DropoutRegion& dropout,
    bool intrafield) const
{
    ReplacementLine best;
    best.found = false;
    best.quality = -1.0;
    
    auto descriptor_opt = source.get_descriptor(field_id);
    if (!descriptor_opt) {
        return best;
    }
    const auto& descriptor = *descriptor_opt;
    
    if (intrafield) {
        // Search nearby lines in the same field
        for (uint32_t dist = 1; dist <= config_.max_replacement_distance; ++dist) {
            // Try line above
            if (line >= dist) {
                uint32_t candidate_line = line - dist;
                const uint16_t* candidate_data = source.get_line(field_id, candidate_line);
                double quality = calculate_line_quality(candidate_data, descriptor.width, dropout);
                
                if (quality > best.quality) {
                    best.found = true;
                    best.source_field = field_id;
                    best.source_line = candidate_line;
                    best.quality = quality;
                    best.distance = dist;
                }
            }
            
            // Try line below
            if (line + dist < descriptor.height) {
                uint32_t candidate_line = line + dist;
                const uint16_t* candidate_data = source.get_line(field_id, candidate_line);
                double quality = calculate_line_quality(candidate_data, descriptor.width, dropout);
                
                if (quality > best.quality) {
                    best.found = true;
                    best.source_field = field_id;
                    best.source_line = candidate_line;
                    best.quality = quality;
                    best.distance = dist;
                }
            }
        }
    } else {
        // Interfield correction: use same line from other field
        // For now, use the adjacent field (field_id +/- 1)
        FieldID other_field = config_.reverse_field_order 
            ? FieldID(field_id.value() + 1)
            : (field_id.value() > 0 ? FieldID(field_id.value() - 1) : FieldID(field_id.value() + 1));
        
        auto other_descriptor_opt = source.get_descriptor(other_field);
        if (other_descriptor_opt && line < other_descriptor_opt->height) {
            const uint16_t* candidate_data = source.get_line(other_field, line);
            double quality = calculate_line_quality(candidate_data, descriptor.width, dropout);
            
            best.found = true;
            best.source_field = other_field;
            best.source_line = line;
            best.quality = quality;
            best.distance = 1;  // One field away
        }
    }
    
    return best;
}

void DropoutCorrectStage::apply_correction(
    std::vector<uint16_t>& line_data,
    const DropoutRegion& dropout,
    const uint16_t* replacement_data,
    bool highlight) const
{
    // If highlighting, fill with white IRE level (100 IRE = 65535 in 16-bit)
    // Otherwise, copy samples from replacement line to corrected line
    uint16_t fill_value = highlight ? 65535 : 0;  // White IRE level
    
    for (uint32_t sample = dropout.start_sample; sample < dropout.end_sample; ++sample) {
        if (sample < line_data.size()) {
            line_data[sample] = highlight ? fill_value : replacement_data[sample];
        }
    }
}

double DropoutCorrectStage::calculate_line_quality(
    const uint16_t* line_data,
    size_t width,
    const DropoutRegion& dropout) const
{
    // Calculate quality based on variance in the dropout region
    // Lower variance = better quality (more stable signal)
    
    if (dropout.start_sample >= dropout.end_sample || dropout.end_sample > width) {
        return 0.0;
    }
    
    // Calculate mean
    double sum = 0.0;
    uint32_t count = dropout.end_sample - dropout.start_sample;
    for (uint32_t i = dropout.start_sample; i < dropout.end_sample; ++i) {
        sum += line_data[i];
    }
    double mean = sum / count;
    
    // Calculate variance
    double var_sum = 0.0;
    for (uint32_t i = dropout.start_sample; i < dropout.end_sample; ++i) {
        double diff = line_data[i] - mean;
        var_sum += diff * diff;
    }
    double variance = var_sum / count;
    
    // Return inverse of variance (higher = better quality)
    // Add small epsilon to avoid division by zero
    return 1.0 / (variance + 1.0);
}

void DropoutCorrectStage::correct_single_field(
    CorrectedVideoFieldRepresentation* corrected,
    std::shared_ptr<const VideoFieldRepresentation> source,
    FieldID field_id) const
{
    ORC_LOG_DEBUG("DropoutCorrectStage::correct_single_field - field {}", field_id.value());
    
    // Get field descriptor
    auto descriptor_opt = source->get_descriptor(field_id);
    if (!descriptor_opt) {
        ORC_LOG_DEBUG("DropoutCorrectStage: no descriptor for field {}", field_id.value());
        return;  // Can't process without descriptor
    }
    const auto& descriptor = *descriptor_opt;
    
    ORC_LOG_DEBUG("DropoutCorrectStage: field {} dimensions: {}x{}", 
                  field_id.value(), descriptor.width, descriptor.height);
    
    // Get dropout hints from source
    auto dropouts = source->get_dropout_hints(field_id);
    ORC_LOG_DEBUG("DropoutCorrectStage: field {} has {} dropout hints", 
                  field_id.value(), dropouts.size());
    
    if (dropouts.empty()) {
        ORC_LOG_DEBUG("DropoutCorrectStage: field {} has no dropouts to correct", field_id.value());
        return;  // No dropouts to correct
    }
    
    // Log first few dropouts for debugging
    size_t log_count = std::min(dropouts.size(), size_t(5));
    for (size_t i = 0; i < log_count; ++i) {
        ORC_LOG_DEBUG("  Dropout {}: line {}, samples {}-{}", 
                      i, dropouts[i].line, dropouts[i].start_sample, dropouts[i].end_sample);
    }
    
    // Apply overcorrection if configured
    if (config_.overcorrect_extension > 0) {
        for (auto& dropout : dropouts) {
            if (dropout.start_sample > config_.overcorrect_extension) {
                dropout.start_sample -= config_.overcorrect_extension;
            } else {
                dropout.start_sample = 0;
            }
            
            if (dropout.end_sample + config_.overcorrect_extension < descriptor.width) {
                dropout.end_sample += config_.overcorrect_extension;
            } else {
                dropout.end_sample = static_cast<uint32_t>(descriptor.width);
            }
        }
    }
    
    // Split dropouts by location
    auto split_dropouts = split_dropout_regions(dropouts, descriptor);
    
    ORC_LOG_DEBUG("DropoutCorrectStage: split into {} dropout regions", split_dropouts.size());
    
    // Process each dropout
    size_t corrections_applied = 0;
    for (const auto& dropout : split_dropouts) {
        // Get current line data (either already corrected or original)
        auto line_key = std::make_pair(field_id, dropout.line);
        std::vector<uint16_t> line_data;
        
        if (corrected->corrected_lines_.count(line_key)) {
            // Use the already-corrected version
            line_data = corrected->corrected_lines_[line_key];
        } else {
            // Get original line data
            const uint16_t* original_data = source->get_line(field_id, dropout.line);
            line_data.assign(original_data, original_data + descriptor.width);
        }
        
        // Find replacement line
        bool use_intrafield = config_.intrafield_only;
        auto replacement = find_replacement_line(*source, field_id, dropout.line, dropout, use_intrafield);
        
        // If no intrafield replacement and not forced, try interfield
        if (!replacement.found && !config_.intrafield_only) {
            replacement = find_replacement_line(*source, field_id, dropout.line, dropout, false);
        }
        
        if (replacement.found) {
            // Apply the correction
            const uint16_t* replacement_data = source->get_line(replacement.source_field, replacement.source_line);
            apply_correction(line_data, dropout, replacement_data, corrected->highlight_corrections_);
            corrections_applied++;
            
            ORC_LOG_DEBUG("  Applied correction to line {} samples {}-{} from field {} line {} (quality={:.2f}, highlight={})",
                          dropout.line, dropout.start_sample, dropout.end_sample,
                          replacement.source_field.value(), replacement.source_line,
                          replacement.quality, corrected->highlight_corrections_);
            
            // Store corrected line
            corrected->corrected_lines_[std::make_pair(field_id, dropout.line)] = line_data;
        } else {
            ORC_LOG_DEBUG("  No replacement found for line {} samples {}-{}",
                          dropout.line, dropout.start_sample, dropout.end_sample);
        }
    }
    
    ORC_LOG_DEBUG("DropoutCorrectStage: field {} complete - applied {} corrections out of {} regions",
                  field_id.value(), corrections_applied, split_dropouts.size());
}

// Parameter interface implementation

std::vector<ParameterDescriptor> DropoutCorrectStage::get_parameter_descriptors() const
{
    std::vector<ParameterDescriptor> descriptors;
    
    // Overcorrect extension parameter
    {
        ParameterDescriptor desc;
        desc.name = "overcorrect_extension";
        desc.display_name = "Overcorrect Extension";
        desc.description = "Extend dropout regions by this many samples (useful for heavily damaged sources)";
        desc.type = ParameterType::UINT32;
        desc.constraints.min_value = static_cast<uint32_t>(0);
        desc.constraints.max_value = static_cast<uint32_t>(48);
        desc.constraints.default_value = static_cast<uint32_t>(0);
        desc.constraints.required = false;
        descriptors.push_back(desc);
    }
    
    // Intrafield only parameter
    {
        ParameterDescriptor desc;
        desc.name = "intrafield_only";
        desc.display_name = "Intrafield Only";
        desc.description = "Force intrafield correction only (don't use data from opposite field)";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.required = false;
        descriptors.push_back(desc);
    }
    
    // Reverse field order parameter
    {
        ParameterDescriptor desc;
        desc.name = "reverse_field_order";
        desc.display_name = "Reverse Field Order";
        desc.description = "Use second/first field order instead of first/second";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.required = false;
        descriptors.push_back(desc);
    }
    
    // Max replacement distance parameter
    {
        ParameterDescriptor desc;
        desc.name = "max_replacement_distance";
        desc.display_name = "Max Replacement Distance";
        desc.description = "Maximum distance (in lines) to search for replacement data";
        desc.type = ParameterType::UINT32;
        desc.constraints.min_value = static_cast<uint32_t>(1);
        desc.constraints.max_value = static_cast<uint32_t>(50);
        desc.constraints.default_value = static_cast<uint32_t>(10);
        desc.constraints.required = false;
        descriptors.push_back(desc);
    }
    
    // Match chroma phase parameter
    {
        ParameterDescriptor desc;
        desc.name = "match_chroma_phase";
        desc.display_name = "Match Chroma Phase";
        desc.description = "Match chroma phase when selecting replacement lines (PAL only)";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = true;
        desc.constraints.required = false;
        descriptors.push_back(desc);
    }
    
    // Highlight corrections parameter
    {
        ParameterDescriptor desc;
        desc.name = "highlight_corrections";
        desc.display_name = "Highlight Corrections";
        desc.description = "Fill corrected regions with white IRE level (100) to visualize dropout locations";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.required = false;
        descriptors.push_back(desc);
    }
    
    return descriptors;
}

std::map<std::string, ParameterValue> DropoutCorrectStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["overcorrect_extension"] = config_.overcorrect_extension;
    params["intrafield_only"] = config_.intrafield_only;
    params["reverse_field_order"] = config_.reverse_field_order;
    params["max_replacement_distance"] = config_.max_replacement_distance;
    params["match_chroma_phase"] = config_.match_chroma_phase;
    params["highlight_corrections"] = config_.highlight_corrections;
    return params;
}

bool DropoutCorrectStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    // Validate and apply parameters
    for (const auto& [name, value] : params) {
        if (name == "overcorrect_extension") {
            if (auto* val = std::get_if<uint32_t>(&value)) {
                if (*val <= 48) {
                    config_.overcorrect_extension = *val;
                } else {
                    return false;  // Invalid value
                }
            } else {
                return false;  // Wrong type
            }
        }
        else if (name == "intrafield_only") {
            if (auto* val = std::get_if<bool>(&value)) {
                config_.intrafield_only = *val;
            } else {
                return false;
            }
        }
        else if (name == "reverse_field_order") {
            if (auto* val = std::get_if<bool>(&value)) {
                config_.reverse_field_order = *val;
            } else {
                return false;
            }
        }
        else if (name == "max_replacement_distance") {
            if (auto* val = std::get_if<uint32_t>(&value)) {
                if (*val >= 1 && *val <= 50) {
                    config_.max_replacement_distance = *val;
                } else {
                    return false;
                }
            } else {
                return false;
            }
        }
        else if (name == "match_chroma_phase") {
            if (auto* val = std::get_if<bool>(&value)) {
                config_.match_chroma_phase = *val;
            } else {
                return false;
            }
        }
        else if (name == "highlight_corrections") {
            if (auto* val = std::get_if<bool>(&value)) {
                config_.highlight_corrections = *val;
            } else {
                return false;
            }
        }
        else {
            // Unknown parameter
            return false;
        }
    }
    
    return true;
}

} // namespace orc
