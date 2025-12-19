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
#include "stage_registry.h"
#include <stdexcept>
#include <algorithm>

namespace orc {

// Simple representation that wraps source metadata but provides overwritten data
class OverwrittenVideoFieldRepresentation : public VideoFieldRepresentation {
public:
    OverwrittenVideoFieldRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source,
        uint16_t constant_value,
        ArtifactID artifact_id,
        Provenance provenance)
        : VideoFieldRepresentation(artifact_id, provenance)
        , source_(source)
        , constant_value_(constant_value)
    {
    }
    
    FieldIDRange field_range() const override {
        return source_->field_range();
    }
    
    size_t field_count() const override {
        return source_->field_count();
    }
    
    bool has_field(FieldID id) const override {
        return source_->has_field(id);
    }
    
    std::optional<FieldDescriptor> get_descriptor(FieldID id) const override {
        return source_->get_descriptor(id);
    }
    
    const sample_type* get_line(FieldID id, size_t line) const override {
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
        
        // Create new line filled with constant value
        std::vector<sample_type> line_data(descriptor->width, constant_value_);
        line_cache_[line_key] = std::move(line_data);
        return line_cache_[line_key].data();
    }
    
    std::vector<sample_type> get_field(FieldID id) const override {
        auto descriptor = get_descriptor(id);
        if (!descriptor) {
            return {};
        }
        
        size_t total_samples = descriptor->height * descriptor->width;
        std::vector<sample_type> field_data(total_samples, constant_value_);
        return field_data;
    }
    
    std::string type_name() const override {
        return "OverwrittenVideoFieldRepresentation";
    }

private:
    std::shared_ptr<const VideoFieldRepresentation> source_;
    uint16_t constant_value_;
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
    // Convert IRE to 16-bit sample value
    // Standard IRE scale: 0 IRE = 0, 100 IRE = 65535 (for 16-bit)
    // But typically: black = 7.5 IRE (4915), white = 100 IRE (65535)
    // We'll use simple linear: 0 IRE = 0, 120 IRE = 65535
    uint16_t sample_value = static_cast<uint16_t>(
        std::clamp(ire_value_ * 65535.0 / 120.0, 0.0, 65535.0)
    );
    
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
