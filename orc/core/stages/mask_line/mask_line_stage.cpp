/*
 * File:        mask_line_stage.cpp
 * Module:      orc-core
 * Purpose:     Line masking stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "mask_line_stage.h"
#include <stage_registry.h>
#include <preview_helpers.h>
#include <logging.h>
#include <sstream>
#include <algorithm>

namespace orc {

// Register the stage
ORC_REGISTER_STAGE(MaskLineStage)

// Force linker to include this object file
void force_link_MaskLineStage() {}

// ===== MaskedLineRepresentation Implementation =====

MaskedLineRepresentation::MaskedLineRepresentation(
    std::shared_ptr<const VideoFieldRepresentation> source,
    const std::string& line_spec,
    double mask_ire)
    : VideoFieldRepresentationWrapper(
        source,
        ArtifactID("masked_line"),
        Provenance{}),
      mask_ire_(mask_ire)
{
    parse_line_spec(line_spec);
}

void MaskedLineRepresentation::parse_line_spec(const std::string& line_spec) {
    line_ranges_.clear();
    
    if (line_spec.empty()) {
        return;
    }
    
    // Parse comma-separated ranges like "F:21" or "S:15-17,A:21,F:30-32"
    std::istringstream ss(line_spec);
    std::string token;
    
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        
        if (token.empty()) {
            continue;
        }
        
        // Parse parity prefix (F:, S:, or A:)
        char parity = 'A';  // Default to all fields
        std::string range_str = token;
        
        size_t colon_pos = token.find(':');
        if (colon_pos != std::string::npos && colon_pos > 0) {
            char prefix = std::toupper(token[0]);
            if (prefix == 'F' || prefix == 'S' || prefix == 'A') {
                parity = prefix;
                range_str = token.substr(colon_pos + 1);
            }
        }
        
        // Check if it's a range (contains '-')
        size_t dash_pos = range_str.find('-');
        if (dash_pos != std::string::npos) {
            // Parse range
            std::string start_str = range_str.substr(0, dash_pos);
            std::string end_str = range_str.substr(dash_pos + 1);
            
            try {
                size_t start = std::stoul(start_str);
                size_t end = std::stoul(end_str);
                line_ranges_.push_back({parity, start, end});
                ORC_LOG_DEBUG("MaskLine: Added line range {}:{}-{}", parity, start, end);
            } catch (const std::exception& e) {
                ORC_LOG_WARN("MaskLine: Invalid line range '{}': {}", token, e.what());
            }
        } else {
            // Single line number
            try {
                size_t line = std::stoul(range_str);
                line_ranges_.push_back({parity, line, line});
                ORC_LOG_DEBUG("MaskLine: Added single line {}:{}", parity, line);
            } catch (const std::exception& e) {
                ORC_LOG_WARN("MaskLine: Invalid line number '{}': {}", token, e.what());
            }
        }
    }
}

uint16_t MaskedLineRepresentation::ire_to_sample(double ire) const {
    // Get video parameters for black and white levels
    if (!source_) {
        return 0;
    }
    
    auto video_params = source_->get_video_parameters();
    if (!video_params.has_value() || !video_params->is_valid()) {
        // Fallback: use standard 16-bit range
        // IRE 0 = 0, IRE 100 = 65535
        return static_cast<uint16_t>((ire / 100.0) * 65535.0);
    }
    
    // Convert IRE to 16-bit sample using video parameters
    // black_16b_ire corresponds to 0 IRE, white_16b_ire corresponds to 100 IRE
    uint16_t black_level = video_params->black_16b_ire;
    uint16_t white_level = video_params->white_16b_ire;
    
    // Linear interpolation
    double sample = black_level + (ire / 100.0) * (white_level - black_level);
    return static_cast<uint16_t>(std::clamp(sample, 0.0, 65535.0));
}

bool MaskedLineRepresentation::should_mask_line(FieldID field_id, size_t line_num) const {
    // Get field parity for this field
    bool is_first_field = false;
    if (source_) {
        auto parity_hint = source_->get_field_parity_hint(field_id);
        if (parity_hint.has_value()) {
            is_first_field = parity_hint->is_first_field;
        }
    }
    
    // Check if line is in any of the specified ranges with matching parity
    for (const auto& range : line_ranges_) {
        // Check if parity matches
        if (range.parity == 'F' && !is_first_field) continue;
        if (range.parity == 'S' && is_first_field) continue;
        // 'A' matches all fields
        
        // Check if line is in range
        if (line_num >= range.start && line_num <= range.end) {
            return true;
        }
    }
    
    return false;
}

const MaskedLineRepresentation::sample_type* 
MaskedLineRepresentation::get_line(FieldID id, size_t line) const {
    if (!source_) {
        return nullptr;
    }
    
    // If this line shouldn't be masked, return source data
    if (!should_mask_line(id, line)) {
        return source_->get_line(id, line);
    }
    
    // Need to create and cache masked line
    auto& field_cache = masked_line_cache_[id];
    auto cache_it = field_cache.find(line);
    
    if (cache_it != field_cache.end()) {
        return cache_it->second.data();
    }
    
    // Get source line to determine width
    const sample_type* source_line = source_->get_line(id, line);
    if (!source_line) {
        return nullptr;
    }
    
    // Get field descriptor for width
    auto descriptor = source_->get_descriptor(id);
    if (!descriptor.has_value()) {
        return nullptr;
    }
    
    // Create masked line filled with mask value (converted from IRE)
    uint16_t mask_sample = ire_to_sample(mask_ire_);
    std::vector<uint16_t> masked_line(descriptor->width, mask_sample);
    
    // Cache it
    field_cache[line] = std::move(masked_line);
    
    return field_cache[line].data();
}

std::vector<MaskedLineRepresentation::sample_type> 
MaskedLineRepresentation::get_field(FieldID id) const {
    if (!source_) {
        return {};
    }
    
    auto descriptor = source_->get_descriptor(id);
    if (!descriptor.has_value()) {
        return {};
    }
    
    std::vector<sample_type> field_data;
    field_data.reserve(descriptor->width * descriptor->height);
    
    for (size_t line = 0; line < descriptor->height; ++line) {
        const sample_type* line_data = get_line(id, line);
        if (line_data) {
            field_data.insert(field_data.end(), line_data, line_data + descriptor->width);
        } else {
            // If we can't get the line, fill with zeros
            field_data.insert(field_data.end(), descriptor->width, 0);
        }
    }
    
    return field_data;
}

// Dual-channel support for YC sources

const MaskedLineRepresentation::sample_type* 
MaskedLineRepresentation::get_line_luma(FieldID id, size_t line) const {
    if (!source_ || !source_->has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_line_luma(id, line);
    }
    
    // If this line shouldn't be masked, return source data
    if (!should_mask_line(id, line)) {
        return source_->get_line_luma(id, line);
    }
    
    // Need to create and cache masked line
    auto& field_cache = masked_luma_cache_[id];
    auto cache_it = field_cache.find(line);
    
    if (cache_it != field_cache.end()) {
        return cache_it->second.data();
    }
    
    // Get source line to determine width
    const sample_type* source_line = source_->get_line_luma(id, line);
    if (!source_line) {
        return nullptr;
    }
    
    // Get field descriptor for width
    auto descriptor = source_->get_descriptor(id);
    if (!descriptor.has_value()) {
        return nullptr;
    }
    
    // Create masked line filled with mask value
    uint16_t mask_sample = ire_to_sample(mask_ire_);
    std::vector<uint16_t> masked_line(descriptor->width, mask_sample);
    
    // Cache it
    field_cache[line] = std::move(masked_line);
    
    return field_cache[line].data();
}

const MaskedLineRepresentation::sample_type* 
MaskedLineRepresentation::get_line_chroma(FieldID id, size_t line) const {
    if (!source_ || !source_->has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_line_chroma(id, line);
    }
    
    // If this line shouldn't be masked, return source data
    if (!should_mask_line(id, line)) {
        return source_->get_line_chroma(id, line);
    }
    
    // Need to create and cache masked line
    auto& field_cache = masked_chroma_cache_[id];
    auto cache_it = field_cache.find(line);
    
    if (cache_it != field_cache.end()) {
        return cache_it->second.data();
    }
    
    // Get source line to determine width
    const sample_type* source_line = source_->get_line_chroma(id, line);
    if (!source_line) {
        return nullptr;
    }
    
    // Get field descriptor for width
    auto descriptor = source_->get_descriptor(id);
    if (!descriptor.has_value()) {
        return nullptr;
    }
    
    // Create masked line filled with mask value
    uint16_t mask_sample = ire_to_sample(mask_ire_);
    std::vector<uint16_t> masked_line(descriptor->width, mask_sample);
    
    // Cache it
    field_cache[line] = std::move(masked_line);
    
    return field_cache[line].data();
}

std::vector<MaskedLineRepresentation::sample_type> 
MaskedLineRepresentation::get_field_luma(FieldID id) const {
    if (!source_ || !source_->has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_field_luma(id);
    }
    
    auto descriptor = source_->get_descriptor(id);
    if (!descriptor.has_value()) {
        return {};
    }
    
    std::vector<sample_type> field_data;
    field_data.reserve(descriptor->width * descriptor->height);
    
    for (size_t line = 0; line < descriptor->height; ++line) {
        const sample_type* line_data = get_line_luma(id, line);
        if (line_data) {
            field_data.insert(field_data.end(), line_data, line_data + descriptor->width);
        } else {
            field_data.insert(field_data.end(), descriptor->width, 0);
        }
    }
    
    return field_data;
}

std::vector<MaskedLineRepresentation::sample_type> 
MaskedLineRepresentation::get_field_chroma(FieldID id) const {
    if (!source_ || !source_->has_separate_channels()) {
        return VideoFieldRepresentationWrapper::get_field_chroma(id);
    }
    
    auto descriptor = source_->get_descriptor(id);
    if (!descriptor.has_value()) {
        return {};
    }
    
    std::vector<sample_type> field_data;
    field_data.reserve(descriptor->width * descriptor->height);
    
    for (size_t line = 0; line < descriptor->height; ++line) {
        const sample_type* line_data = get_line_chroma(id, line);
        if (line_data) {
            field_data.insert(field_data.end(), line_data, line_data + descriptor->width);
        } else {
            field_data.insert(field_data.end(), descriptor->width, 0);
        }
    }
    
    return field_data;
}

// ===== MaskLineStage Implementation =====

std::vector<ArtifactPtr> MaskLineStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context)
{
    (void)observation_context; // Unused for now
    if (inputs.empty()) {
        throw DAGExecutionError("MaskLineStage requires one input");
    }
    
    auto input_artifact = inputs[0];
    if (!input_artifact) {
        throw DAGExecutionError("MaskLineStage received null input artifact");
    }
    
    // Cast to VideoFieldRepresentation
    auto input_vfr = std::dynamic_pointer_cast<const VideoFieldRepresentation>(input_artifact);
    if (!input_vfr) {
        throw DAGExecutionError("MaskLineStage input must be VideoFieldRepresentation");
    }
    
    // Set parameters from the execute call
    if (!parameters.empty()) {
        // Create a mutable copy of this to set parameters
        const_cast<MaskLineStage*>(this)->set_parameters(parameters);
    }
    
    // Process and return
    auto output_vfr = process(input_vfr);
    cached_output_ = output_vfr;  // Cache for preview
    
    std::vector<ArtifactPtr> outputs;
    outputs.push_back(std::const_pointer_cast<VideoFieldRepresentation>(output_vfr));
    return outputs;
}

std::shared_ptr<const VideoFieldRepresentation> MaskLineStage::process(
    std::shared_ptr<const VideoFieldRepresentation> source) const
{
    if (!source) {
        return nullptr;
    }
    
    // If no lines specified, just pass through
    if (line_spec_.empty()) {
        ORC_LOG_DEBUG("MaskLine: No lines specified, passing through unchanged");
        return source;
    }
    
    // Create a wrapper that masks the specified lines
    return std::make_shared<MaskedLineRepresentation>(
        source, line_spec_, mask_ire_);
}

std::vector<ParameterDescriptor> MaskLineStage::get_parameter_descriptors(VideoSystem project_format, SourceType source_type) const
{
    (void)project_format;  // Unused - mask line works with all formats
    (void)source_type;     // Unused - mask line works with all source types
    
    return {
        ParameterDescriptor{
            "lineSpec",
            "Line Specification",
            "Lines to mask with parity prefix. Format: PARITY:LINE or PARITY:START-END. "
            "Parity: F (first field), S (second field), A (all fields). "
            "Examples: 'F:21' (NTSC closed captions), 'S:6-22' (second field lines 6-22), 'A:10,F:21' (line 10 all fields + line 21 first field). "
            "Line numbers are 0-based field line numbers.",
            ParameterType::STRING,
            ParameterConstraints{
                std::nullopt,  // no min
                std::nullopt,  // no max
                ParameterValue{std::string("")},  // default: empty (no masking)
                {},  // no allowed values (free-form)
                false,  // not required
                std::nullopt  // no dependency
            }
        },
        ParameterDescriptor{
            "maskIRE",
            "Mask IRE Level",
            "IRE level to write to masked pixels (0 = black, 100 = white). Default is 0 (black).",
            ParameterType::DOUBLE,
            ParameterConstraints{
                ParameterValue{0.0},  // min
                ParameterValue{100.0},  // max
                ParameterValue{0.0},  // default
                {},  // no allowed strings
                false,  // not required
                std::nullopt  // no dependency
            }
        }
    };
}

std::map<std::string, ParameterValue> MaskLineStage::get_parameters() const
{
    return {
        {"lineSpec", line_spec_},
        {"maskIRE", mask_ire_}
    };
}

bool MaskLineStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    for (const auto& [key, value] : params) {
        if (key == "lineSpec" && std::holds_alternative<std::string>(value)) {
            line_spec_ = std::get<std::string>(value);
        } else if (key == "maskIRE" && std::holds_alternative<double>(value)) {
            mask_ire_ = std::get<double>(value);
        }
    }
    
    return true;
}

std::vector<PreviewOption> MaskLineStage::get_preview_options() const
{
    if (!cached_output_) {
        return {};
    }
    
    return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage MaskLineStage::render_preview(
    const std::string& option_id,
    uint64_t index,
    PreviewNavigationHint hint) const
{
    if (!cached_output_) {
        return PreviewImage{};
    }
    
    return PreviewHelpers::render_standard_preview(cached_output_, option_id, index, hint);
}

} // namespace orc
