/*
 * File:        field_map_stage.cpp
 * Module:      orc-core
 * Purpose:     Field mapping/reordering stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "field_map_stage.h"
#include "stage_registry.h"
#include "preview_helpers.h"
#include "logging.h"
#include <sstream>
#include <algorithm>

namespace orc {

// Register the stage
ORC_REGISTER_STAGE(FieldMapStage)

// Force linker to include this object file
void force_link_FieldMapStage() {}

/**
 * @brief VideoFieldRepresentation wrapper that remaps field IDs
 */
class FieldMappedRepresentation : public VideoFieldRepresentationWrapper {
public:
    FieldMappedRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source,
        std::vector<FieldID> field_mapping,
        const std::string& range_spec)
        : VideoFieldRepresentationWrapper(
            source,
            ArtifactID("field_map_" + source->id().to_string() + "_" + range_spec + 
                      "_" + std::to_string(reinterpret_cast<uintptr_t>(source.get()))),
            Provenance{
                "field_map",
                "1.0",
                {{"ranges", range_spec}},
                {source->id()},
                std::chrono::system_clock::now(),
                "",  // hostname
                "",  // user
                {}   // statistics
            })
        , field_mapping_(std::move(field_mapping))
    {
        // Initialize black line buffer for padding
        // Get video parameters from source to determine line width
        if (source_) {
            auto params = source_->get_video_parameters();
            if (params) {
                // Create black line (all zeros)
                black_line_.resize(params->field_width, 0);
            }
        }
    }
    
    // Explicitly delete copy/move to prevent issues (this object is always used via shared_ptr)
    FieldMappedRepresentation(const FieldMappedRepresentation&) = delete;
    FieldMappedRepresentation& operator=(const FieldMappedRepresentation&) = delete;
    FieldMappedRepresentation(FieldMappedRepresentation&&) = delete;
    FieldMappedRepresentation& operator=(FieldMappedRepresentation&&) = delete;
    
    FieldIDRange field_range() const override {
        if (field_mapping_.empty()) {
            return FieldIDRange{};
        }
        return FieldIDRange{FieldID(0), FieldID(field_mapping_.size())};
    }
    
    size_t field_count() const override {
        return field_mapping_.size();
    }
    
    bool has_field(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return false;
        }
        FieldID source_id = field_mapping_[index];
        
        // Padding fields (INVALID) always exist as black fields
        if (!source_id.is_valid()) {
            return true;
        }
        
        return source_ && source_->has_field(source_id);
    }
    
    std::optional<FieldDescriptor> get_descriptor(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return std::nullopt;
        }
        FieldID source_id = field_mapping_[index];
        
        // For padding fields, create a descriptor from source video parameters
        if (!source_id.is_valid() && source_) {
            auto params = source_->get_video_parameters();
            if (params) {
                FieldDescriptor desc;
                desc.field_id = id;
                desc.width = params->field_width;
                desc.height = params->field_height;
                // Convert VideoSystem to VideoFormat
                desc.format = (params->system == VideoSystem::PAL) ? VideoFormat::PAL : VideoFormat::NTSC;
                desc.parity = FieldParity::Top;  // Arbitrary for black fields
                return desc;
            }
            return std::nullopt;
        }
        
        if (!source_) {
            return std::nullopt;
        }
        
        auto desc = source_->get_descriptor(source_id);
        if (desc) {
            // Update field_id to reflect the remapped position
            desc->field_id = id;
        }
        return desc;
    }
    
    const sample_type* get_line(FieldID id, size_t line) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return nullptr;
        }
        FieldID source_id = field_mapping_[index];
        
        // Return black line for padding fields
        if (!source_id.is_valid()) {
            (void)line;  // All lines same for black field
            return black_line_.empty() ? nullptr : black_line_.data();
        }
        
        if (!source_) {
            return nullptr;
        }
        return source_->get_line(source_id, line);
    }
    
    std::vector<sample_type> get_field(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return {};
        }
        FieldID source_id = field_mapping_[index];
        
        // Return black field for padding
        if (!source_id.is_valid()) {
            auto desc = get_descriptor(id);
            if (desc) {
                return std::vector<sample_type>(desc->width * desc->height, 0);
            }
            return {};
        }
        
        if (!source_) {
            return {};
        }
        return source_->get_field(source_id);
    }
    
    std::vector<DropoutRegion> get_dropout_hints(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return {};
        }
        FieldID source_id = field_mapping_[index];
        
        // Padding fields have no dropouts
        if (!source_id.is_valid()) {
            return {};
        }
        
        if (!source_) {
            return {};
        }
        return source_->get_dropout_hints(source_id);
    }
    
    std::optional<FieldParityHint> get_field_parity_hint(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return std::nullopt;
        }
        FieldID source_id = field_mapping_[index];
        
        // Padding fields have no parity hint
        if (!source_id.is_valid()) {
            return std::nullopt;
        }
        
        if (!source_) {
            return std::nullopt;
        }
        return source_->get_field_parity_hint(source_id);
    }
    
    std::optional<FieldPhaseHint> get_field_phase_hint(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return std::nullopt;
        }
        FieldID source_id = field_mapping_[index];
        
        // Padding fields have no phase hint
        if (!source_id.is_valid()) {
            return std::nullopt;
        }
        
        if (!source_) {
            return std::nullopt;
        }
        return source_->get_field_phase_hint(source_id);
    }
    
    std::vector<std::shared_ptr<Observation>> get_observations(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return {};
        }
        FieldID source_id = field_mapping_[index];
        
        // Padding fields have no observations
        if (!source_id.is_valid()) {
            return {};
        }
        
        if (!source_) {
            return {};
        }
        return source_->get_observations(source_id);
    }
    
    // Audio methods - remap field IDs to follow field reordering
    uint32_t get_audio_sample_count(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return 0;
        }
        FieldID source_id = field_mapping_[index];
        
        // Padding fields have no audio
        if (!source_id.is_valid()) {
            return 0;
        }
        
        if (!source_) {
            return 0;
        }
        return source_->get_audio_sample_count(source_id);
    }
    
    std::vector<int16_t> get_audio_samples(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return {};
        }
        FieldID source_id = field_mapping_[index];
        
        // Padding fields have no audio
        if (!source_id.is_valid()) {
            return {};
        }
        
        if (!source_) {
            return {};
        }
        return source_->get_audio_samples(source_id);
    }
    
    // EFM methods - remap field IDs to follow field reordering
    uint32_t get_efm_sample_count(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return 0;
        }
        FieldID source_id = field_mapping_[index];
        
        // Padding fields have no EFM
        if (!source_id.is_valid()) {
            return 0;
        }
        
        if (!source_) {
            return 0;
        }
        return source_->get_efm_sample_count(source_id);
    }
    
    std::vector<uint8_t> get_efm_samples(FieldID id) const override {
        size_t index = id.value();
        if (index >= field_mapping_.size()) {
            return {};
        }
        FieldID source_id = field_mapping_[index];
        
        // Padding fields have no EFM
        if (!source_id.is_valid()) {
            return {};
        }
        
        if (!source_) {
            return {};
        }
        return source_->get_efm_samples(source_id);
    }

private:
    std::vector<FieldID> field_mapping_;  // Maps output field index -> source FieldID
    mutable std::vector<sample_type> black_line_;  // Cached black line for padding
};

std::vector<ArtifactPtr> FieldMapStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    if (inputs.empty()) {
        throw DAGExecutionError("FieldMapStage requires one input");
    }
    
    // Get the source representation
    auto source = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    if (!source) {
        throw DAGExecutionError("FieldMapStage input must be a VideoFieldRepresentation");
    }
    
    // Get range specification parameter (check if overridden in parameters)
    std::string range_spec = range_spec_;
    std::vector<std::pair<uint64_t, uint64_t>> ranges = cached_ranges_;
    
    auto it = parameters.find("ranges");
    if (it != parameters.end()) {
        if (auto* str_val = std::get_if<std::string>(&it->second)) {
            if (*str_val != range_spec_) {
                // Parameter overridden at execution time - parse it
                range_spec = *str_val;
                ranges = parse_ranges(range_spec);
                if (ranges.empty()) {
                    ORC_LOG_ERROR("FieldMapStage: Failed to parse range specification: {}", range_spec);
                    throw DAGExecutionError("Invalid range specification: " + range_spec);
                }
            }
        }
    }
    
    // If no ranges specified or cached, pass through unchanged
    if (range_spec.empty() || ranges.empty()) {
        ORC_LOG_WARN("FieldMapStage: No range specification provided, passing through unchanged");
        cached_output_ = source;  // Cache the input for preview rendering
        return {inputs[0]};
    }
    
    // Build the field mapping
    auto field_mapping = build_field_mapping(ranges, *source);
    if (field_mapping.empty()) {
        ORC_LOG_WARN("FieldMapStage: Range specification resulted in empty mapping");
        cached_output_ = source;  // Cache the input for preview rendering
        return {inputs[0]};
    }
    
    auto source_range = source->field_range();
    ORC_LOG_DEBUG("FieldMapStage: Input has {} fields (range {}-{}), output will have {} fields based on specification: {}", 
                  source->field_count(), source_range.start.value(), source_range.end.value(),
                  field_mapping.size(), range_spec);
    
    // Create wrapped representation with remapped fields
    auto result = std::make_shared<FieldMappedRepresentation>(source, std::move(field_mapping), range_spec);
    cached_output_ = result;
    return {result};
}

std::vector<ParameterDescriptor> FieldMapStage::get_parameter_descriptors(VideoSystem project_format) const
{
    (void)project_format;  // Unused - field map works with all formats
    return {
        ParameterDescriptor{
            "ranges",
            "Field Ranges",
            "Comma-separated list of field ranges (e.g., '0-10,20-30,11-19'). "
            "Output fields will be in the order specified.",
            ParameterType::STRING,
            ParameterConstraints{
                std::nullopt,  // no min
                std::nullopt,  // no max
                ParameterValue{std::string("")},  // default: empty (pass-through)
                {},  // no allowed strings
                false,  // not required
                std::nullopt  // no dependency
            }
        },
        ParameterDescriptor{
            "seed",
            "Random Seed",
            "Random seed used to generate field corruption pattern (for reproducibility)",
            ParameterType::INT32,
            ParameterConstraints{
                std::nullopt,  // no min
                std::nullopt,  // no max
                ParameterValue{int32_t(0)},  // default: 0 (not set)
                {},  // no allowed strings
                false,  // not required
                std::nullopt  // no dependency
            }
        }
    };
}

std::map<std::string, ParameterValue> FieldMapStage::get_parameters() const
{
    return {
        {"ranges", ParameterValue{range_spec_}},
        {"seed", ParameterValue{seed_}}
    };
}

bool FieldMapStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    for (const auto& [key, value] : params) {
        if (key == "ranges") {
            if (auto* str_val = std::get_if<std::string>(&value)) {
                range_spec_ = *str_val;
                
                // Parse and cache the ranges immediately for validation and efficiency
                if (!range_spec_.empty()) {
                    cached_ranges_ = parse_ranges(range_spec_);
                    if (cached_ranges_.empty()) {
                        ORC_LOG_ERROR("FieldMapStage: Invalid range specification: {}", range_spec_);
                        return false;  // Invalid range specification
                    }
                    ORC_LOG_DEBUG("FieldMapStage: Cached {} range(s) from specification: {}", 
                                 cached_ranges_.size(), range_spec_);
                } else {
                    cached_ranges_.clear();
                }
            } else {
                return false;
            }
        } else if (key == "seed") {
            if (auto* int_val = std::get_if<int32_t>(&value)) {
                seed_ = *int_val;
            } else {
                return false;
            }
        } else {
            // Unknown parameter
            return false;
        }
    }
    return true;
}

std::vector<std::pair<uint64_t, uint64_t>> FieldMapStage::parse_ranges(const std::string& range_spec)
{
    std::vector<std::pair<uint64_t, uint64_t>> ranges;
    
    if (range_spec.empty()) {
        return ranges;
    }
    
    std::istringstream iss(range_spec);
    std::string range_str;
    
    // Split by comma
    while (std::getline(iss, range_str, ',')) {
        // Trim whitespace
        range_str.erase(0, range_str.find_first_not_of(" \t"));
        range_str.erase(range_str.find_last_not_of(" \t") + 1);
        
        if (range_str.empty()) {
            continue;
        }
        
        // Check for PAD_N token
        if (range_str.substr(0, 4) == "PAD_") {
            try {
                uint64_t pad_count = std::stoull(range_str.substr(4));
                // Use UINT64_MAX to signal padding
                ranges.emplace_back(UINT64_MAX, pad_count);
                ORC_LOG_DEBUG("FieldMapStage: Parsed padding directive: {} frames", pad_count);
                continue;
            } catch (...) {
                ORC_LOG_ERROR("FieldMapStage: Invalid padding directive: {}", range_str);
                return {};
            }
        }
        
        // Find the dash separator
        size_t dash_pos = range_str.find('-');
        if (dash_pos == std::string::npos) {
            // Single field (e.g., "5")
            try {
                uint64_t field = std::stoull(range_str);
                ranges.emplace_back(field, field);
            } catch (...) {
                ORC_LOG_ERROR("FieldMapStage: Invalid field number: {}", range_str);
                return {};
            }
        } else {
            // Range (e.g., "0-10")
            std::string start_str = range_str.substr(0, dash_pos);
            std::string end_str = range_str.substr(dash_pos + 1);
            
            // Trim whitespace around the numbers
            start_str.erase(0, start_str.find_first_not_of(" \t"));
            start_str.erase(start_str.find_last_not_of(" \t") + 1);
            end_str.erase(0, end_str.find_first_not_of(" \t"));
            end_str.erase(end_str.find_last_not_of(" \t") + 1);
            
            try {
                uint64_t start = std::stoull(start_str);
                uint64_t end = std::stoull(end_str);
                
                if (start > end) {
                    ORC_LOG_ERROR("FieldMapStage: Invalid range (start > end): {}-{}", start, end);
                    return {};
                }
                
                ranges.emplace_back(start, end);
            } catch (...) {
                ORC_LOG_ERROR("FieldMapStage: Invalid range format: {}", range_str);
                return {};
            }
        }
    }
    
    return ranges;
}

std::vector<FieldID> FieldMapStage::build_field_mapping(
    const std::vector<std::pair<uint64_t, uint64_t>>& ranges,
    const VideoFieldRepresentation& source)
{
    std::vector<FieldID> mapping;
    
    auto source_range = source.field_range();
    uint64_t source_end = source_range.end.value();
    
    // Build the mapping by expanding each range
    for (const auto& [start, end] : ranges) {
        // Check for padding directive (signaled by UINT64_MAX)
        if (start == UINT64_MAX) {
            // This is a PAD_N directive, 'end' contains the count
            for (uint64_t i = 0; i < end; ++i) {
                mapping.push_back(FieldID());  // Invalid FieldID = black field
            }
            ORC_LOG_DEBUG("FieldMapStage: Inserted {} padding fields", end);
            continue;
        }
        
        // Normal field range (inclusive on both ends)
        for (uint64_t field_id = start; field_id <= end; ++field_id) {
            // Use field_id directly as absolute field ID, not as offset from source_start
            FieldID fid(field_id);
            
            // Check if this field exists in the source (source_end is exclusive)
            if (field_id >= source_end) {
                ORC_LOG_WARN("FieldMapStage: Field {} out of source range (0-{}), skipping",
                               field_id, source_end - 1);
                continue;
            }
            
            if (source.has_field(fid)) {
                mapping.push_back(fid);
            } else {
                ORC_LOG_WARN("FieldMapStage: Field {} not available in source", field_id);
            }
        }
    }
    
    return mapping;
}

std::vector<PreviewOption> FieldMapStage::get_preview_options() const
{
    return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage FieldMapStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    (void)hint;  // Unused for now
    return PreviewHelpers::render_standard_preview(cached_output_, option_id, index);
}

} // namespace orc
