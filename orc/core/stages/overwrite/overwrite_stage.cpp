/*
 * File:        overwrite_stage.cpp
 * Module:      orc-core
 * Purpose:     Overwrite processing stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "overwrite_stage.h"
#include "tbc_video_field_representation.h"
#include "tbc_metadata.h"
#include "stage_registry.h"
#include "logging.h"
#include <stdexcept>
#include <algorithm>

namespace orc {

// Simple representation that wraps source metadata but provides overwritten data
// Inherits from VideoFieldRepresentationWrapper to automatically propagate hints
class OverwrittenVideoFieldRepresentation : public VideoFieldRepresentationWrapper {
public:
    OverwrittenVideoFieldRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source,
        uint16_t constant_value,
        size_t active_video_start,
        size_t active_video_end,
        size_t first_active_line,
        size_t last_active_line,
        ArtifactID artifact_id,
        Provenance provenance)
        : VideoFieldRepresentationWrapper(source, artifact_id, provenance)
        , constant_value_(constant_value)
        , active_video_start_(active_video_start)
        , active_video_end_(active_video_end)
        , first_active_line_(first_active_line)
        , last_active_line_(last_active_line)
    {
        ORC_LOG_DEBUG("OverwrittenVideoFieldRepresentation created: value={}, active_start={}, active_end={}, first_line={}, last_line={}",
                      constant_value_, active_video_start_, active_video_end_, first_active_line_, last_active_line_);
    }
    
    // Only override methods that are actually modified by this stage
    const sample_type* get_line(FieldID id, size_t line) const override {
        ORC_LOG_DEBUG("OverwrittenVideoFieldRepresentation::get_line called: field={}, line={}", id.value(), line);
        
        if (!has_field(id)) {
            return nullptr;
        }
        
        auto descriptor = get_descriptor(id);
        if (!descriptor || line >= descriptor->height) {
            return nullptr;
        }
        
        // Lazy-create the overwritten line buffer
        size_t line_key = (id.value() << 16) | line;
        auto it = line_cache_.find(line_key);
        if (it != line_cache_.end()) {
            return it->second.data();
        }
        
        // Get the source line data
        const sample_type* source_line = source_->get_line(id, line);
        if (!source_line) {
            return nullptr;
        }
        
        // Create line data by copying source
        std::vector<sample_type> line_data(source_line, source_line + descriptor->width);
        
        // Only overwrite the visible area
        // Check if we're within active video lines
        if (line >= first_active_line_ && line <= last_active_line_) {
            // Overwrite the active video sample range
            size_t start_sample = std::max(static_cast<size_t>(0), static_cast<size_t>(active_video_start_));
            size_t end_sample = std::min(descriptor->width, static_cast<size_t>(active_video_end_));
            
            for (size_t i = start_sample; i < end_sample; ++i) {
                line_data[i] = constant_value_;
            }
        }
        
        line_cache_[line_key] = std::move(line_data);
        return line_cache_[line_key].data();
    }
    
    std::vector<sample_type> get_field(FieldID id) const override {
        auto descriptor = get_descriptor(id);
        if (!descriptor) {
            return {};
        }
        
        // Get source field data
        std::vector<sample_type> field_data = source_->get_field(id);
        if (field_data.empty()) {
            return {};
        }
        
        // Only overwrite visible area
        for (size_t line = 0; line < descriptor->height; ++line) {
            if (line >= first_active_line_ && line <= last_active_line_) {
                size_t line_offset = line * descriptor->width;
                size_t start_sample = std::max(static_cast<size_t>(0), static_cast<size_t>(active_video_start_));
                size_t end_sample = std::min(descriptor->width, static_cast<size_t>(active_video_end_));
                
                for (size_t i = start_sample; i < end_sample; ++i) {
                    field_data[line_offset + i] = constant_value_;
                }
            }
        }
        
        return field_data;
    }
    
    std::string type_name() const override {
        return "OverwrittenVideoFieldRepresentation";
    }

private:
    uint16_t constant_value_;
    size_t active_video_start_;
    size_t active_video_end_;
    size_t first_active_line_;
    size_t last_active_line_;
    mutable std::map<size_t, std::vector<sample_type>> line_cache_;
};

OverwriteStage::OverwriteStage()
    : ire_value_(50.0)
{
}

std::vector<ArtifactPtr> OverwriteStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, std::string>& parameters)
{
    if (inputs.size() != 1) {
        throw std::runtime_error("OverwriteStage requires exactly one input");
    }
    
    auto source = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    if (!source) {
        throw std::runtime_error("OverwriteStage input must be a VideoFieldRepresentation");
    }
    
    // Parse parameters if provided
    auto it = parameters.find("ire_value");
    if (it != parameters.end()) {
        try {
            double value = std::stod(it->second);
            if (value < 0.0 || value > 120.0) {
                throw std::runtime_error("IRE value must be between 0 and 120");
            }
            ire_value_ = value;
        } catch (const std::invalid_argument&) {
            throw std::runtime_error("Invalid IRE value parameter");
        }
    }
    
    auto result = process(source);
    std::vector<ArtifactPtr> outputs;
    outputs.push_back(std::const_pointer_cast<Artifact>(std::static_pointer_cast<const Artifact>(result)));
    return outputs;
}

std::shared_ptr<const VideoFieldRepresentation> OverwriteStage::process(
    std::shared_ptr<const VideoFieldRepresentation> source) const
{
    ORC_LOG_DEBUG("OverwriteStage::process called with IRE value: {}", ire_value_);
    
    // Convert IRE to 16-bit sample value
    // Standard IRE scale: 0 IRE = 0, 100 IRE = 65535 (for 16-bit)
    // But typically: black = 7.5 IRE (4915), white = 100 IRE (65535)
    // We'll use simple linear: 0 IRE = 0, 120 IRE = 65535
    uint16_t sample_value = static_cast<uint16_t>(
        std::clamp(ire_value_ * 65535.0 / 120.0, 0.0, 65535.0)
    );
    
    ORC_LOG_DEBUG("OverwriteStage sample_value: {}", sample_value);
    
    // Try to get video parameters from source if it's a TBC representation
    size_t active_video_start = 0;
    size_t active_video_end = 0;
    size_t first_active_line = 0;
    size_t last_active_line = 0;
    
    // Get field descriptor for defaults
    auto descriptor = source->get_descriptor(source->field_range().start);
    
    // Get video parameters from source (propagated through DAG chain)
    auto video_params_opt = source->get_video_parameters();
    
    if (video_params_opt && descriptor) {
        const auto& video_params = *video_params_opt;
        
        ORC_LOG_DEBUG("OverwriteStage raw video params: first_active_field_line={}, last_active_field_line={}, active_video_start={}, active_video_end={}, field_height={}, field_width={}",
                      video_params.first_active_field_line, video_params.last_active_field_line,
                      video_params.active_video_start, video_params.active_video_end,
                      video_params.field_height, video_params.field_width);
        
        // Horizontal boundaries from metadata
        if (video_params.active_video_start > 0 && video_params.active_video_end > 0) {
            active_video_start = static_cast<size_t>(video_params.active_video_start);
            active_video_end = static_cast<size_t>(video_params.active_video_end);
        } else {
            // Fallback to full width
            active_video_start = 0;
            active_video_end = descriptor->width;
        }
        
        // Vertical boundaries must be inferred from format (not in metadata)
        if (descriptor->height >= 300) {
            // PAL: 625 lines total, 313 per field, active video approximately lines 23-310
            first_active_line = 23;
            last_active_line = 310;
        } else {
            // NTSC: 525 lines total, 263 per field, active video approximately lines 22-259
            first_active_line = 22;
            last_active_line = 259;
        }
        
        ORC_LOG_DEBUG("OverwriteStage active area: lines {}-{}, samples {}-{}", 
                      first_active_line, last_active_line, active_video_start, active_video_end);
    } else if (descriptor) {
        // If no video parameters available, use full field
        active_video_end = descriptor->width;
        last_active_line = descriptor->height - 1;
        ORC_LOG_DEBUG("OverwriteStage using full field: lines {}-{}, samples {}-{}", 
                      first_active_line, last_active_line, active_video_start, active_video_end);
    }
    
    // Create provenance
    Provenance prov;
    prov.stage_name = "overwrite";
    prov.stage_version = version();
    prov.input_artifacts = {source->id()};
    prov.parameters["ire_value"] = std::to_string(ire_value_);
    prov.created_at = std::chrono::system_clock::now();
    
    // Generate artifact ID (simple hash based on inputs and parameters)
    std::string id_str = "overwrite_" + source->id().to_string() + "_" + std::to_string(ire_value_);
    ArtifactID artifact_id(id_str);
    
    return std::make_shared<OverwrittenVideoFieldRepresentation>(
        source,
        sample_value,
        active_video_start,
        active_video_end,
        first_active_line,
        last_active_line,
        artifact_id,
        prov
    );
}

std::vector<ParameterDescriptor> OverwriteStage::get_parameter_descriptors() const
{
    std::vector<ParameterDescriptor> descriptors;
    
    ParameterDescriptor desc;
    desc.name = "ire_value";
    desc.display_name = "IRE Value";
    desc.description = "Constant IRE value to fill all samples (0 = black, 100 = white, 120 = super-white)";
    desc.type = ParameterType::DOUBLE;
    desc.constraints.min_value = 0.0;
    desc.constraints.max_value = 120.0;
    desc.constraints.default_value = 50.0;
    desc.constraints.required = false;
    descriptors.push_back(desc);
    
    return descriptors;
}

std::map<std::string, ParameterValue> OverwriteStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["ire_value"] = ire_value_;
    return params;
}

bool OverwriteStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    auto it = params.find("ire_value");
    if (it != params.end()) {
        if (auto* val = std::get_if<double>(&it->second)) {
            if (*val >= 0.0 && *val <= 120.0) {
                ire_value_ = *val;
                return true;
            }
        }
        return false;
    }
    return true;
}

// Register the stage
static StageRegistration reg([]() {
    return std::make_shared<OverwriteStage>();
});

} // namespace orc
