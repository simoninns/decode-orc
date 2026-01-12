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
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include <dropout_correct_stage.h>
#include "tbc_metadata.h"
#include "tbc_video_field_representation.h"
#include "stage_registry.h"
#include "preview_helpers.h"
#include "logging.h"
#include <algorithm>
#include <cmath>
#include <iostream>

namespace orc {

// Register the stage
ORC_REGISTER_STAGE(DropoutCorrectStage)

// Force linker to include this object file
void force_link_DropoutCorrectStage() {}

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
    
    cached_output_ = corrected;
    ORC_LOG_DEBUG("DropoutCorrectStage::execute - Set cached_output_ on instance {} to {}", 
                  static_cast<const void*>(this), static_cast<const void*>(corrected.get()));
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
    , stage_(stage)
    , highlight_corrections_(highlight_corrections)
    , corrected_fields_(MAX_CACHED_FIELDS)  // Initialize LRU cache with max size
    , corrected_luma_fields_(MAX_CACHED_FIELDS)  // For YC sources
    , corrected_chroma_fields_(MAX_CACHED_FIELDS)  // For YC sources
{
}

void CorrectedVideoFieldRepresentation::ensure_field_corrected(FieldID field_id) const {
    // Check if field data is in cache - if so, nothing to do
    // For YC sources, check both luma and chroma caches
    // The LRU cache handles access tracking automatically
    
    if (source_->has_separate_channels()) {
        // YC source - check dual caches
        if (corrected_luma_fields_.contains(field_id) && corrected_chroma_fields_.contains(field_id)) {
            return;  // Both channels already processed
        }
    } else {
        // Composite source - check single cache
        if (corrected_fields_.contains(field_id)) {
            return;  // Already processed
        }
    }
    
    ORC_LOG_DEBUG("CorrectedVideoFieldRepresentation: processing field {} (NOT in cache)", field_id.value());
    
    // Process only the requested field
    stage_->correct_single_field(const_cast<CorrectedVideoFieldRepresentation*>(this), source_, field_id);
}

const uint16_t* CorrectedVideoFieldRepresentation::get_line(FieldID id, size_t line) const {
    // Ensure this field has been corrected (lazy processing + batch prefetch)
    ensure_field_corrected(id);
    
    // Check if we have a corrected version of this field in LRU cache
    const auto* cached_field = corrected_fields_.get_ptr(id);
    if (cached_field && !cached_field->empty()) {
        // Return pointer to line within the cached corrected field data
        // (empty vector is a marker for "no corrections applied", fetch from source instead)
        auto descriptor = source_->get_descriptor(id);
        if (descriptor && line < descriptor->height) {
            return &(*cached_field)[line * descriptor->width];
        }
    }
    
    // Return original line (no corrections for this field, or cache miss)
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

// ========================================================================
// DUAL-CHANNEL ACCESS - For YC sources
// ========================================================================

const uint16_t* CorrectedVideoFieldRepresentation::get_line_luma(FieldID id, size_t line) const {
    // If source doesn't have separate channels, use default behavior
    if (!source_ || !source_->has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_line_luma(id, line);
    }
    
    // Ensure this field has been corrected (lazy processing)
    ensure_field_corrected(id);
    
    // Check if we have a corrected version of this field in luma cache
    const auto* cached_field = corrected_luma_fields_.get_ptr(id);
    if (cached_field && !cached_field->empty()) {
        // Return pointer to line within the cached corrected luma field data
        auto descriptor = source_->get_descriptor(id);
        if (descriptor && line < descriptor->height) {
            return &(*cached_field)[line * descriptor->width];
        }
    }
    
    // Return original luma line (no corrections for this field, or cache miss)
    return source_->get_line_luma(id, line);
}

const uint16_t* CorrectedVideoFieldRepresentation::get_line_chroma(FieldID id, size_t line) const {
    // If source doesn't have separate channels, use default behavior
    if (!source_ || !source_->has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_line_chroma(id, line);
    }
    
    // Ensure this field has been corrected (lazy processing)
    ensure_field_corrected(id);
    
    // Check if we have a corrected version of this field in chroma cache
    const auto* cached_field = corrected_chroma_fields_.get_ptr(id);
    if (cached_field && !cached_field->empty()) {
        // Return pointer to line within the cached corrected chroma field data
        auto descriptor = source_->get_descriptor(id);
        if (descriptor && line < descriptor->height) {
            return &(*cached_field)[line * descriptor->width];
        }
    }
    
    // Return original chroma line (no corrections for this field, or cache miss)
    return source_->get_line_chroma(id, line);
}

std::vector<uint16_t> CorrectedVideoFieldRepresentation::get_field_luma(FieldID id) const {
    // If source doesn't have separate channels, use default behavior
    if (!source_ || !source_->has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_field_luma(id);
    }
    
    // Get descriptor to know field dimensions
    auto desc_opt = source_->get_descriptor(id);
    if (!desc_opt) {
        return {};
    }
    
    std::vector<uint16_t> field_data;
    field_data.reserve(desc_opt->width * desc_opt->height);
    
    // Assemble field from individual lines (corrected where applicable)
    for (size_t line = 0; line < desc_opt->height; ++line) {
        const uint16_t* line_data = get_line_luma(id, line);
        field_data.insert(field_data.end(), line_data, line_data + desc_opt->width);
    }
    
    return field_data;
}

std::vector<uint16_t> CorrectedVideoFieldRepresentation::get_field_chroma(FieldID id) const {
    // If source doesn't have separate channels, use default behavior
    if (!source_ || !source_->has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_field_chroma(id);
    }
    
    // Get descriptor to know field dimensions
    auto desc_opt = source_->get_descriptor(id);
    if (!desc_opt) {
        return {};
    }
    
    std::vector<uint16_t> field_data;
    field_data.reserve(desc_opt->width * desc_opt->height);
    
    // Assemble field from individual lines (corrected where applicable)
    for (size_t line = 0; line < desc_opt->height; ++line) {
        const uint16_t* line_data = get_line_chroma(id, line);
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
    bool intrafield,
    Channel channel) const
{
    ReplacementLine best;
    best.found = false;
    best.quality = -1.0;
    best.cached_data = nullptr;
    
    auto descriptor_opt = source.get_descriptor(field_id);
    if (!descriptor_opt) {
        return best;
    }
    const auto& descriptor = *descriptor_opt;
    
    // Helper lambda to get line data based on channel type
    auto get_line_for_channel = [&source, channel](FieldID fid, size_t line_num) -> const uint16_t* {
        switch (channel) {
            case Channel::LUMA:
                return source.get_line_luma(fid, line_num);
            case Channel::CHROMA:
                return source.get_line_chroma(fid, line_num);
            case Channel::COMPOSITE:
            default:
                return source.get_line(fid, line_num);
        }
    };
    
    if (intrafield) {
        // Search nearby lines in the same field
        for (uint32_t dist = 1; dist <= config_.max_replacement_distance; ++dist) {
            // Try line above
            if (line >= dist) {
                uint32_t candidate_line = line - dist;
                const uint16_t* candidate_data = get_line_for_channel(field_id, candidate_line);
                if (candidate_data) {  // Check for null (important for YC sources)
                    double quality = calculate_line_quality(candidate_data, descriptor.width, dropout);
                    
                    if (quality > best.quality) {
                        best.found = true;
                        best.source_field = field_id;
                        best.source_line = candidate_line;
                        best.quality = quality;
                        best.distance = dist;
                        best.cached_data = candidate_data;  // OPTIMIZATION: Cache the data pointer
                    }
                }
            }
            
            // Try line below
            if (line + dist < descriptor.height) {
                uint32_t candidate_line = line + dist;
                const uint16_t* candidate_data = get_line_for_channel(field_id, candidate_line);
                if (candidate_data) {  // Check for null (important for YC sources)
                    double quality = calculate_line_quality(candidate_data, descriptor.width, dropout);
                    
                    if (quality > best.quality) {
                        best.found = true;
                        best.source_field = field_id;
                        best.source_line = candidate_line;
                        best.quality = quality;
                        best.distance = dist;
                        best.cached_data = candidate_data;  // OPTIMIZATION: Cache the data pointer
                    }
                }
            }
        }
    } else {
        // Interfield correction: use same line from other field
        // For now, use the adjacent field (field_id +/- 1)
        FieldID other_field = (field_id.value() > 0 ? FieldID(field_id.value() - 1) : FieldID(field_id.value() + 1));
        
        auto other_descriptor_opt = source.get_descriptor(other_field);
        if (other_descriptor_opt && line < other_descriptor_opt->height) {
            const uint16_t* candidate_data = get_line_for_channel(other_field, line);
            if (candidate_data) {  // Check for null (important for YC sources)
                double quality = calculate_line_quality(candidate_data, descriptor.width, dropout);
                
                best.found = true;
                best.source_field = other_field;
                best.source_line = line;
                best.quality = quality;
                best.distance = 1;  // One field away
                best.cached_data = candidate_data;  // OPTIMIZATION: Cache the data pointer
            }
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
    // OPTIMIZATION: Use mean absolute deviation instead of full variance for faster quality calculation.
    // This is a good approximation for stability that avoids expensive squaring operations.
    
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
    
    // Calculate mean absolute deviation (simpler and faster than variance)
    double mad_sum = 0.0;
    for (uint32_t i = dropout.start_sample; i < dropout.end_sample; ++i) {
        mad_sum += std::abs(line_data[i] - mean);
    }
    double mad = mad_sum / count;
    
    // Return inverse of MAD (higher = better quality, more stable signal)
    // Add epsilon to avoid division by zero
    return 1.0 / (mad + 1.0);
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
    ORC_LOG_DEBUG("DropoutCorrectStage: field {} has {} dropout hints from source type '{}'", 
                  field_id.value(), dropouts.size(), source->type_name());
    
    // Log first few dropouts for debugging
    for (size_t i = 0; i < std::min(dropouts.size(), size_t(3)); ++i) {
        ORC_LOG_DEBUG("  Source Dropout {}: line {}, samples {}-{}", 
                      i, dropouts[i].line, dropouts[i].start_sample, dropouts[i].end_sample);
    }
    
    // OPTIMIZATION: If there are no dropouts, skip the expensive full field copy
    // Instead, just mark field as processed without storing anything
    if (dropouts.empty()) {
        ORC_LOG_DEBUG("DropoutCorrectStage: field {} has no dropouts - storing empty marker", field_id.value());
        // Store a marker (empty vector) to indicate this field was processed but has no corrections
        if (source->has_separate_channels()) {
            corrected->corrected_luma_fields_.put(field_id, std::vector<uint16_t>());
            corrected->corrected_chroma_fields_.put(field_id, std::vector<uint16_t>());
        } else {
            corrected->corrected_fields_.put(field_id, std::vector<uint16_t>());
        }
        return;
    }
    
    // Check if source has separate channels (YC source)
    if (source->has_separate_channels()) {
        // YC SOURCE PATH - process Y and C independently
        ORC_LOG_DEBUG("DropoutCorrectStage: YC source detected - processing Y and C independently");
        
        // Initialize luma field data
        std::vector<uint16_t> luma_field_data;
        luma_field_data.reserve(descriptor.width * descriptor.height);
        for (uint32_t line = 0; line < descriptor.height; ++line) {
            const uint16_t* line_data = source->get_line_luma(field_id, line);
            if (line_data) {
                luma_field_data.insert(luma_field_data.end(), line_data, line_data + descriptor.width);
            } else {
                // Fallback: insert black line if no data
                luma_field_data.insert(luma_field_data.end(), descriptor.width, 0);
            }
        }
        
        // Initialize chroma field data
        std::vector<uint16_t> chroma_field_data;
        chroma_field_data.reserve(descriptor.width * descriptor.height);
        for (uint32_t line = 0; line < descriptor.height; ++line) {
            const uint16_t* line_data = source->get_line_chroma(field_id, line);
            if (line_data) {
                chroma_field_data.insert(chroma_field_data.end(), line_data, line_data + descriptor.width);
            } else {
                // Fallback: insert black line if no data
                chroma_field_data.insert(chroma_field_data.end(), descriptor.width, 0);
            }
        }
        
        // Apply overcorrection if configured
        std::vector<DropoutRegion> processed_dropouts = dropouts;
        if (config_.overcorrect_extension > 0) {
            for (auto& dropout : processed_dropouts) {
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
        auto split_dropouts = split_dropout_regions(processed_dropouts, descriptor);
        ORC_LOG_DEBUG("DropoutCorrectStage: split into {} dropout regions", split_dropouts.size());
        
        // Process each dropout on both Y and C channels
        size_t luma_corrections = 0;
        size_t chroma_corrections = 0;
        
        for (const auto& dropout : split_dropouts) {
            // Correct luma channel
            uint16_t* luma_line_data = &luma_field_data[dropout.line * descriptor.width];
            bool use_intrafield = config_.intrafield_only;
            
            // Find replacement using luma channel
            auto luma_replacement = find_replacement_line(*source, field_id, dropout.line, dropout, use_intrafield, Channel::LUMA);
            if (!luma_replacement.found && !config_.intrafield_only) {
                luma_replacement = find_replacement_line(*source, field_id, dropout.line, dropout, false, Channel::LUMA);
            }
            
            if (luma_replacement.found) {
                // Get replacement luma line data
                const uint16_t* replacement_luma = source->get_line_luma(luma_replacement.source_field, luma_replacement.source_line);
                
                for (uint32_t sample = dropout.start_sample; sample <= dropout.end_sample && sample < descriptor.width; ++sample) {
                    if (corrected->highlight_corrections_) {
                        luma_line_data[sample] = 65535;
                    } else {
                        luma_line_data[sample] = replacement_luma[sample];
                    }
                }
                luma_corrections++;
            }
            
            // Correct chroma channel (same dropout map, independent replacement search)
            uint16_t* chroma_line_data = &chroma_field_data[dropout.line * descriptor.width];
            
            // Find replacement using chroma channel
            auto chroma_replacement = find_replacement_line(*source, field_id, dropout.line, dropout, use_intrafield, Channel::CHROMA);
            if (!chroma_replacement.found && !config_.intrafield_only) {
                chroma_replacement = find_replacement_line(*source, field_id, dropout.line, dropout, false, Channel::CHROMA);
            }
            
            if (chroma_replacement.found) {
                // Get replacement chroma line data
                const uint16_t* replacement_chroma = source->get_line_chroma(chroma_replacement.source_field, chroma_replacement.source_line);
                
                for (uint32_t sample = dropout.start_sample; sample <= dropout.end_sample && sample < descriptor.width; ++sample) {
                    if (corrected->highlight_corrections_) {
                        chroma_line_data[sample] = 65535;
                    } else {
                        chroma_line_data[sample] = replacement_chroma[sample];
                    }
                }
                chroma_corrections++;
            }
        }
        
        // Store corrected Y and C fields in dual caches
        corrected->corrected_luma_fields_.put(field_id, std::move(luma_field_data));
        corrected->corrected_chroma_fields_.put(field_id, std::move(chroma_field_data));
        
        ORC_LOG_DEBUG("DropoutCorrectStage: YC field {} complete - Y: {}/{} corrections, C: {}/{} corrections",
                      field_id.value(), luma_corrections, split_dropouts.size(), 
                      chroma_corrections, split_dropouts.size());
        return;
    }
    
    // COMPOSITE SOURCE PATH (existing code)
    // OPTIMIZATION: Only copy field data when corrections are actually needed
    // Initialize field data - copy ALL source lines (needed for corrections)
    std::vector<uint16_t> field_data;
    field_data.reserve(descriptor.width * descriptor.height);
    for (uint32_t line = 0; line < descriptor.height; ++line) {
        const uint16_t* line_data = source->get_line(field_id, line);
        field_data.insert(field_data.end(), line_data, line_data + descriptor.width);
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
        // Get pointer to the line within field_data
        uint16_t* line_data = &field_data[dropout.line * descriptor.width];
        
        // Find replacement line
        bool use_intrafield = config_.intrafield_only;
        auto replacement = find_replacement_line(*source, field_id, dropout.line, dropout, use_intrafield);
        
        // If no intrafield replacement and not forced, try interfield
        if (!replacement.found && !config_.intrafield_only) {
            replacement = find_replacement_line(*source, field_id, dropout.line, dropout, false);
        }
        
        if (replacement.found) {
            // OPTIMIZATION: Use cached pointer instead of calling get_line() again
            // The replacement.cached_data was already retrieved during find_replacement_line()
            const uint16_t* replacement_data = replacement.cached_data;
            if (!replacement_data) {
                // Fallback if cache missed (shouldn't happen)
                replacement_data = source->get_line(replacement.source_field, replacement.source_line);
            }
            
            // Copy samples from replacement line to field data
            for (uint32_t sample = dropout.start_sample; sample <= dropout.end_sample && sample < descriptor.width; ++sample) {
                if (corrected->highlight_corrections_) {
                    line_data[sample] = 65535;  // Highlight correction
                } else {
                    line_data[sample] = replacement_data[sample];
                }
            }
            corrections_applied++;
            
            ORC_LOG_DEBUG("  Applied correction to line {} samples {}-{} from field {} line {} (quality={:.2f})",
                          dropout.line, dropout.start_sample, dropout.end_sample,
                          replacement.source_field.value(), replacement.source_line,
                          replacement.quality);
        } else {
            ORC_LOG_DEBUG("  No replacement found for line {} samples {}-{}",
                          dropout.line, dropout.start_sample, dropout.end_sample);
        }
    }
    
    // Store the corrected field in LRU cache
    // LRUCache automatically handles eviction when size exceeds max
    corrected->corrected_fields_.put(field_id, std::move(field_data));
    
    ORC_LOG_DEBUG("DropoutCorrectStage: field {} complete - applied {} corrections out of {} regions (cache: {} fields)",
                  field_id.value(), corrections_applied, split_dropouts.size(), corrected->corrected_fields_.size());
}

// Parameter interface implementation

std::vector<ParameterDescriptor> DropoutCorrectStage::get_parameter_descriptors(VideoSystem project_format, SourceType source_type) const
{
    (void)project_format;  // Unused - dropout correction works with all formats
    (void)source_type;     // Unused - dropout correction works with all source types
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

std::vector<PreviewOption> DropoutCorrectStage::get_preview_options() const
{
    ORC_LOG_DEBUG("DropoutCorrectStage::get_preview_options - Called on instance {}, cached_output_ = {}", 
                  static_cast<const void*>(this), static_cast<const void*>(cached_output_.get()));
    return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage DropoutCorrectStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = PreviewHelpers::render_standard_preview(cached_output_, option_id, index, hint);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    ORC_LOG_DEBUG("DropoutCorrect PREVIEW: option '{}' index {} rendered in {} ms (hint={})",
                 option_id, index, duration_ms, hint == PreviewNavigationHint::Sequential ? "Sequential" : "Random");
    return result;
}

} // namespace orc
