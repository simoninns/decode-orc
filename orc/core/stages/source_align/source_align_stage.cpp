/*
 * File:        source_align_stage.cpp
 * Module:      orc-core
 * Purpose:     Source alignment stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "source_align_stage.h"
#include "stage_registry.h"
#include "preview_helpers.h"
#include "logging.h"
#include "../observers/biphase_observer.h"
#include <algorithm>
#include <limits>
#include <sstream>

namespace orc {

// Register the stage
ORC_REGISTER_STAGE(SourceAlignStage)

// Force linker to include this object file
void force_link_SourceAlignStage() {}

/**
 * @brief VideoFieldRepresentation wrapper that drops leading fields
 */
class AlignedSourceRepresentation : public VideoFieldRepresentationWrapper {
public:
    AlignedSourceRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source,
        FieldID offset,
        size_t source_index)
        : VideoFieldRepresentationWrapper(
            source,
            ArtifactID("aligned_source_" + std::to_string(source_index) + "_offset_" + std::to_string(offset.value())),
            Provenance{
                "source_align",
                "1.0",
                {{"offset", std::to_string(offset.value())},
                 {"source_index", std::to_string(source_index)}},
                {source->id()},
                std::chrono::system_clock::now(),
                "",  // hostname
                "",  // user
                {}   // statistics
            })
        , offset_(offset)
    {
    }
    
    FieldIDRange field_range() const override {
        if (!source_) {
            return FieldIDRange{};
        }
        auto source_range = source_->field_range();
        if (offset_.value() >= source_range.size()) {
            return FieldIDRange{};  // Offset beyond source range
        }
        // New range starts at 0 and has reduced size (end is exclusive)
        size_t new_size = source_range.size() - offset_.value();
        return FieldIDRange{FieldID(0), FieldID(new_size)};
    }
    
    size_t field_count() const override {
        if (!source_) {
            return 0;
        }
        auto source_range = source_->field_range();
        if (offset_.value() >= source_range.size()) {
            return 0;
        }
        return source_range.size() - offset_.value();
    }
    
    bool has_field(FieldID id) const override {
        if (!source_) {
            return false;
        }
        // Map output field_id to source field_id
        FieldID source_id(id.value() + offset_.value());
        return source_->has_field(source_id);
    }
    
    std::optional<FieldDescriptor> get_descriptor(FieldID id) const override {
        if (!source_) {
            return std::nullopt;
        }
        // Map output field_id to source field_id
        FieldID source_id(id.value() + offset_.value());
        auto desc = source_->get_descriptor(source_id);
        if (desc) {
            // Update field_id to reflect the aligned position
            desc->field_id = id;
        }
        return desc;
    }
    
    const sample_type* get_line(FieldID id, size_t line) const override {
        if (!source_) {
            return nullptr;
        }
        // Map output field_id to source field_id
        FieldID source_id(id.value() + offset_.value());
        return source_->get_line(source_id, line);
    }
    
    std::vector<sample_type> get_field(FieldID id) const override {
        if (!source_) {
            return {};
        }
        // Map output field_id to source field_id
        FieldID source_id(id.value() + offset_.value());
        return source_->get_field(source_id);
    }
    
    std::vector<DropoutRegion> get_dropout_hints(FieldID id) const override {
        if (!source_) {
            return {};
        }
        // Map output field_id to source field_id
        FieldID source_id(id.value() + offset_.value());
        return source_->get_dropout_hints(source_id);
    }
    
    std::optional<FieldParityHint> get_field_parity_hint(FieldID id) const override {
        if (!source_) {
            return std::nullopt;
        }
        FieldID source_id(id.value() + offset_.value());
        return source_->get_field_parity_hint(source_id);
    }
    
    std::optional<FieldPhaseHint> get_field_phase_hint(FieldID id) const override {
        if (!source_) {
            return std::nullopt;
        }
        FieldID source_id(id.value() + offset_.value());
        return source_->get_field_phase_hint(source_id);
    }
    
    std::vector<std::shared_ptr<Observation>> get_observations(FieldID id) const override {
        if (!source_) {
            return {};
        }
        FieldID source_id(id.value() + offset_.value());
        return source_->get_observations(source_id);
    }

private:
    FieldID offset_;  // Number of fields to skip from the beginning
};

std::vector<std::pair<size_t, size_t>> SourceAlignStage::parse_alignment_map(const std::string& alignment_spec)
{
    std::vector<std::pair<size_t, size_t>> result;
    
    if (alignment_spec.empty()) {
        return result;
    }
    
    std::istringstream iss(alignment_spec);
    std::string entry;
    
    // Split by comma
    while (std::getline(iss, entry, ',')) {
        // Trim whitespace
        entry.erase(0, entry.find_first_not_of(" \t"));
        entry.erase(entry.find_last_not_of(" \t") + 1);
        
        if (entry.empty()) {
            continue;
        }
        
        // Parse "input_id+offset" format
        size_t plus_pos = entry.find('+');
        if (plus_pos == std::string::npos) {
            ORC_LOG_ERROR("Invalid alignment map entry (missing '+'): {}", entry);
            return {};  // Invalid format
        }
        
        try {
            size_t input_id = std::stoull(entry.substr(0, plus_pos));
            size_t offset = std::stoull(entry.substr(plus_pos + 1));
            result.push_back({input_id, offset});
        } catch (...) {
            ORC_LOG_ERROR("Invalid alignment map entry (parse error): {}", entry);
            return {};  // Invalid format
        }
    }
    
    return result;
}

std::vector<FieldID> SourceAlignStage::apply_field_order_enforcement(
    std::vector<FieldID> offsets,
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const
{
    if (!enforce_field_order_ || sources.empty() || offsets.empty()) {
        return offsets;
    }
    
    // Check the first output field from each source
    // We want all first output fields to have the same parity
    // The first one encountered determines the target parity
    std::optional<bool> target_is_first_field;
    
    for (size_t i = 0; i < sources.size() && i < offsets.size(); ++i) {
        if (!sources[i] || !offsets[i].is_valid()) {
            continue;
        }
        
        // Get parity hint for the first output field
        auto parity_hint = sources[i]->get_field_parity_hint(offsets[i]);
        if (parity_hint.has_value()) {
            bool is_first = parity_hint.value().is_first_field;
            
            if (!target_is_first_field.has_value()) {
                target_is_first_field = is_first;
                ORC_LOG_DEBUG("Field order enforcement: target parity set to {}", 
                             is_first ? "FIRST_FIELD" : "SECOND_FIELD");
            } else if (target_is_first_field.value() != is_first) {
                // This source has wrong parity - add one to offset
                offsets[i] = FieldID(offsets[i].value() + 1);
                ORC_LOG_DEBUG("Field order enforcement: adjusted source {} offset from {} to {} to match parity",
                            i, offsets[i].value() - 1, offsets[i].value());
            }
        }
    }
    
    return offsets;
}

int32_t SourceAlignStage::get_frame_number_from_vbi(
    const VideoFieldRepresentation& source,
    FieldID field_id) const
{
    // Get VBI observations for this field
    auto observations = source.get_observations(field_id);
    
    for (const auto& obs : observations) {
        auto biphase_obs = std::dynamic_pointer_cast<BiphaseObservation>(obs);
        if (!biphase_obs) {
            continue;
        }
        
        // Check for CAV picture number (preferred)
        if (biphase_obs->picture_number.has_value()) {
            return biphase_obs->picture_number.value();
        }
        
        // Check for CLV timecode
        if (biphase_obs->clv_timecode.has_value()) {
            // Convert CLV timecode to frame number
            // CLV timecode format: HH:MM:SS:FF (hours, minutes, seconds, picture_number)
            // Assume 30 fps for NTSC, 25 fps for PAL
            const auto& tc = biphase_obs->clv_timecode.value();
            
            // Determine frame rate from video format
            auto params = source.get_video_parameters();
            bool is_pal = params && params->system == VideoSystem::PAL;
            int32_t fps = is_pal ? 25 : 30;
            
            // Convert to total frame number
            int32_t frame_num = tc.hours * 3600 * fps +
                               tc.minutes * 60 * fps +
                               tc.seconds * fps +
                               tc.picture_number;
            return frame_num;
        }
    }
    
    return -1;  // No VBI frame number found
}

std::vector<FieldID> SourceAlignStage::find_alignment_offsets(
    const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const
{
    if (sources.empty()) {
        return {};
    }
    
    // Single source - no alignment needed
    if (sources.size() == 1) {
        return {FieldID(0)};
    }
    
    ORC_LOG_DEBUG("SourceAlignStage: Finding alignment for {} sources", sources.size());
    
    // Build a map of frame_number -> field_id for each source
    struct FrameLocation {
        FieldID field_id;
        size_t source_index;
    };
    
    std::map<int32_t, std::vector<FrameLocation>> frame_map;
    
    // Scan each source and build the frame map
    for (size_t src_idx = 0; src_idx < sources.size(); ++src_idx) {
        const auto& source = sources[src_idx];
        if (!source) {
            continue;
        }
        
        auto range = source->field_range();
        ORC_LOG_DEBUG("  Source {}: scanning {} fields (range {}-{})",
                     src_idx, source->field_count(), range.start.value(), range.end.value() - 1);
        
        size_t fields_with_vbi = 0;
        for (FieldID field_id = range.start; field_id < range.end; ++field_id) {
            if (!source->has_field(field_id)) {
                continue;
            }
            
            int32_t frame_num = get_frame_number_from_vbi(*source, field_id);
            if (frame_num >= 0) {
                frame_map[frame_num].push_back({field_id, src_idx});
                fields_with_vbi++;
            }
        }
        
        ORC_LOG_DEBUG("    Found VBI data in {} fields", fields_with_vbi);
    }
    
    // Find the first frame number that exists in ALL sources
    int32_t first_common_frame = -1;
    std::vector<FieldID> alignment_offsets(sources.size());  // Default constructor creates invalid FieldIDs
    
    for (const auto& [frame_num, locations] : frame_map) {
        // Check if this frame exists in all sources
        std::vector<bool> source_present(sources.size(), false);
        for (const auto& loc : locations) {
            source_present[loc.source_index] = true;
        }
        
        bool all_present = std::all_of(source_present.begin(), source_present.end(),
                                       [](bool p) { return p; });
        
        if (all_present) {
            first_common_frame = frame_num;
            
            // Record the field_id for each source at this frame
            for (const auto& loc : locations) {
                alignment_offsets[loc.source_index] = loc.field_id;
            }
            
            ORC_LOG_INFO("  Found first common frame: VBI frame #{}", first_common_frame);
            for (size_t i = 0; i < sources.size(); ++i) {
                ORC_LOG_INFO("    Source {}: starts at field_id {}", i, alignment_offsets[i].value());
            }
            
            break;
        }
    }
    
    if (first_common_frame < 0) {
        ORC_LOG_WARN("SourceAlignStage: No common frame found across all sources!");
        ORC_LOG_WARN("  This may indicate sources are from different discs or have no VBI data");
        // Return zero offsets (no alignment)
        return std::vector<FieldID>(sources.size(), FieldID(0));
    }
    
    return alignment_offsets;
}

std::vector<ArtifactPtr> SourceAlignStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    if (inputs.empty()) {
        throw DAGExecutionError("SourceAlignStage requires at least 1 input");
    }
    
    if (inputs.size() > 16) {
        throw DAGExecutionError("SourceAlignStage supports maximum 16 inputs");
    }
    
    ORC_LOG_DEBUG("SourceAlignStage: Processing {} input source(s)", inputs.size());
    
    // Convert inputs to VideoFieldRepresentation
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> sources;
    for (const auto& input : inputs) {
        auto source = std::dynamic_pointer_cast<const VideoFieldRepresentation>(input);
        if (!source) {
            throw DAGExecutionError("SourceAlignStage input must be VideoFieldRepresentation");
        }
        sources.push_back(source);
    }
    
    // Update parameters if provided
    if (!parameters.empty()) {
        set_parameters(parameters);
    }
    
    // Store input sources for reporting
    input_sources_ = sources;
    
    // Determine alignment offsets
    std::vector<FieldID> offsets;
    
    if (!alignment_map_.empty()) {
        // Use manual alignment map
        auto alignment_entries = parse_alignment_map(alignment_map_);
        if (alignment_entries.empty()) {
            throw DAGExecutionError("Invalid alignment map specification: " + alignment_map_);
        }
        
        // Build offsets array from alignment map
        // Initialize all to INVALID (excluded by default)
        offsets.resize(sources.size(), FieldID());  // Default constructor creates INVALID
        
        for (const auto& [input_id, offset_val] : alignment_entries) {
            // Input IDs in the alignment map are 1-indexed
            if (input_id < 1 || input_id > sources.size()) {
                throw DAGExecutionError("Alignment map references invalid input ID: " + 
                                      std::to_string(input_id));
            }
            size_t idx = input_id - 1;  // Convert to 0-indexed
            offsets[idx] = FieldID(offset_val);
        }
        
        ORC_LOG_INFO("Using manual alignment map: {}", alignment_map_);
        for (size_t i = 0; i < offsets.size(); ++i) {
            if (offsets[i].is_valid()) {
                ORC_LOG_INFO("  Input {}: offset = {}", i + 1, offsets[i].value());
            } else {
                ORC_LOG_INFO("  Input {}: EXCLUDED", i + 1);
            }
        }
    } else {
        // Auto-detect alignment from VBI
        ORC_LOG_INFO("Auto-detecting alignment from VBI data");
        offsets = find_alignment_offsets(sources);
    }
    
    // Apply field order enforcement if enabled
    if (enforce_field_order_) {
        ORC_LOG_DEBUG("Applying field order enforcement");
        offsets = apply_field_order_enforcement(std::move(offsets), sources);
    }
    
    // Store alignment information for reporting
    alignment_offsets_ = offsets;
    
    // Create aligned outputs - only for sources with valid offsets
    std::vector<ArtifactPtr> outputs;
    cached_outputs_.clear();
    
    for (size_t i = 0; i < sources.size(); ++i) {
        if (!offsets[i].is_valid()) {
            // Source is excluded - add null to maintain indexing for preview
            cached_outputs_.push_back(nullptr);
            ORC_LOG_DEBUG("  Source {}: EXCLUDED from output", i);
            continue;
        }
        
        auto aligned = std::make_shared<AlignedSourceRepresentation>(
            sources[i], offsets[i], i);
        outputs.push_back(aligned);
        cached_outputs_.push_back(aligned);
        
        ORC_LOG_DEBUG("  Source {}: offset by {} fields, new range has {} fields",
                     i, offsets[i].value(), aligned->field_count());
    }
    
    return outputs;
}

std::vector<ParameterDescriptor> SourceAlignStage::get_parameter_descriptors(VideoSystem project_format) const
{
    (void)project_format;  // Unused - source align works with all formats
    return {
        ParameterDescriptor{
            "alignmentMap",
            "Alignment Map",
            "Manual alignment specification (e.g., '1+2, 2+2, 3+1, 4+1'). "
            "Format: input_id+offset for each input. Empty = auto-detect from VBI.",
            ParameterType::STRING,
            ParameterConstraints{
                std::nullopt,  // no min
                std::nullopt,  // no max
                ParameterValue{std::string("")},  // default: empty (auto-detect)
                {},  // no allowed strings
                false,  // not required
                std::nullopt  // no dependency
            }
        },
        ParameterDescriptor{
            "enforceFieldOrder",
            "Enforce Field Order",
            "When enabled, ensures the first output field is always a first field "
            "(adds extra field if needed). Recommended for proper interlaced output.",
            ParameterType::BOOL,
            ParameterConstraints{
                std::nullopt,  // no min
                std::nullopt,  // no max
                ParameterValue{true},  // default: true (enabled)
                {},  // no allowed strings
                false,  // not required
                std::nullopt  // no dependency
            }
        }
    };
}

std::map<std::string, ParameterValue> SourceAlignStage::get_parameters() const
{
    return {
        {"alignmentMap", ParameterValue{alignment_map_}},
        {"enforceFieldOrder", ParameterValue{enforce_field_order_}}
    };
}

bool SourceAlignStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    for (const auto& [key, value] : params) {
        if (key == "alignmentMap") {
            if (auto* str_val = std::get_if<std::string>(&value)) {
                alignment_map_ = *str_val;
            } else {
                return false;
            }
        } else if (key == "enforceFieldOrder") {
            if (auto* bool_val = std::get_if<bool>(&value)) {
                enforce_field_order_ = *bool_val;
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

std::optional<StageReport> SourceAlignStage::generate_report() const {
    StageReport report;
    report.summary = "Source Alignment Report";
    
    if (input_sources_.empty() || alignment_offsets_.empty()) {
        report.items.push_back({"Status", "Not yet executed"});
        report.items.push_back({"Info", "Execute the DAG to see alignment details"});
        return report;
    }
    
    // Add information for each source
    for (size_t i = 0; i < input_sources_.size(); ++i) {
        const auto& source = input_sources_[i];
        const auto& offset = alignment_offsets_[i];
        
        if (!source) {
            continue;
        }
        
        std::string source_label = "Source " + std::to_string(i);
        
        // Check if source is excluded
        if (!offset.is_valid()) {
            report.items.push_back({source_label + " Status", "EXCLUDED"});
            auto range = source->field_range();
            size_t input_count = source->field_count();
            report.items.push_back({source_label + " Input Range", 
                std::to_string(range.start.value()) + "-" + std::to_string(range.end.value())});
            report.items.push_back({source_label + " Input Fields", std::to_string(input_count)});
        } else {
            auto range = source->field_range();
            size_t input_count = source->field_count();
            size_t dropped = offset.value();
            size_t output_count = input_count - dropped;
            
            report.items.push_back({source_label + " Status", "INCLUDED"});
            report.items.push_back({source_label + " Input Range", 
                std::to_string(range.start.value()) + "-" + std::to_string(range.end.value())});
            report.items.push_back({source_label + " Input Fields", std::to_string(input_count)});
            report.items.push_back({source_label + " Alignment Offset", std::to_string(dropped)});
            report.items.push_back({source_label + " Dropped Fields", std::to_string(dropped)});
            report.items.push_back({source_label + " Output Fields", std::to_string(output_count)});
            
            // Add VBI frame number at alignment point if available
            int32_t vbi_frame = get_frame_number_from_vbi(*source, offset);
            if (vbi_frame >= 0) {
                report.items.push_back({source_label + " First Common VBI Frame", std::to_string(vbi_frame)});
            }
        }
        
        // Add separator between sources
        if (i < input_sources_.size() - 1) {
            report.items.push_back({"", ""});
        }
    }
    
    // Metrics
    report.metrics["source_count"] = static_cast<int64_t>(input_sources_.size());
    
    size_t total_dropped = 0;
    size_t excluded_count = 0;
    for (const auto& offset : alignment_offsets_) {
        if (offset.is_valid()) {
            total_dropped += offset.value();
        } else {
            excluded_count++;
        }
    }
    report.metrics["total_dropped_fields"] = static_cast<int64_t>(total_dropped);
    report.metrics["excluded_sources"] = static_cast<int64_t>(excluded_count);
    report.metrics["included_sources"] = static_cast<int64_t>(input_sources_.size() - excluded_count);
    
    return report;
}

std::vector<PreviewOption> SourceAlignStage::get_preview_options() const
{
    std::vector<PreviewOption> options;
    
    // Offer preview for each aligned source
    for (size_t i = 0; i < cached_outputs_.size(); ++i) {
        const auto& output = cached_outputs_[i];
        if (output && output->field_count() > 0) {
            auto params = output->get_video_parameters();
            uint32_t width = params ? params->field_width : 928;
            uint32_t height = params ? params->field_height : 625;
            double dar = 0.75;  // Standard aspect correction
            
            options.push_back({
                "source_" + std::to_string(i),
                "Aligned Source " + std::to_string(i),
                false,  // is_rgb
                width,
                height,
                output->field_count(),
                dar
            });
        }
    }
    
    return options;
}

PreviewImage SourceAlignStage::render_preview(
    const std::string& option_id,
    uint64_t index,
    PreviewNavigationHint hint) const
{
    (void)hint;  // Not used for field preview
    
    // Parse source index from option_id (format: "source_N")
    size_t source_idx = 0;
    const std::string prefix = "source_";
    if (option_id.compare(0, prefix.length(), prefix) == 0) {
        try {
            source_idx = std::stoull(option_id.substr(7));
        } catch (...) {
            throw std::runtime_error("Invalid preview option_id: " + option_id);
        }
    }
    
    if (source_idx >= cached_outputs_.size()) {
        throw std::runtime_error("Invalid source index in preview option_id");
    }
    
    const auto& output = cached_outputs_[source_idx];
    if (!output) {
        throw std::runtime_error("No cached output for preview");
    }
    
    // Render the field using default IRE scaling
    return PreviewHelpers::render_field_preview(output, FieldID(index), true);
}

} // namespace orc
