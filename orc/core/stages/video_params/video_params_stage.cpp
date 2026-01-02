/*
 * File:        video_params_stage.cpp
 * Module:      orc-core
 * Purpose:     Video parameters override stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include <video_params_stage.h>
#include <stage_registry.h>
#include <preview_helpers.h>
#include <logging.h>

namespace orc {

// Register the stage
ORC_REGISTER_STAGE(VideoParamsStage)

// Force linker to include this object file
void force_link_VideoParamsStage() {}

std::vector<ArtifactPtr> VideoParamsStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    if (inputs.empty()) {
        throw DAGExecutionError("VideoParamsStage requires one input");
    }
    
    auto input_artifact = inputs[0];
    if (!input_artifact) {
        throw DAGExecutionError("VideoParamsStage received null input artifact");
    }
    
    // Cast to VideoFieldRepresentation
    auto input_vfr = std::dynamic_pointer_cast<const VideoFieldRepresentation>(input_artifact);
    if (!input_vfr) {
        throw DAGExecutionError("VideoParamsStage input must be VideoFieldRepresentation");
    }
    
    // Update parameters if provided
    if (!parameters.empty()) {
        set_parameters(parameters);
    }
    
    // Process and return
    auto output_vfr = process(input_vfr);
    cached_output_ = output_vfr;  // Cache for preview
    ORC_LOG_DEBUG("VideoParamsStage::execute - Set cached_output_ on instance {} to {}", 
                  static_cast<const void*>(this), static_cast<const void*>(output_vfr.get()));
    
    std::vector<ArtifactPtr> outputs;
    outputs.push_back(std::const_pointer_cast<VideoFieldRepresentation>(output_vfr));
    return outputs;
}

std::shared_ptr<const VideoFieldRepresentation> VideoParamsStage::process(
    std::shared_ptr<const VideoFieldRepresentation> source) const
{
    if (!source) {
        return nullptr;
    }
    
    // Get source video parameters
    auto source_params = source->get_video_parameters();
    
    // Build override parameters
    auto override_params = build_video_parameters(source_params);
    
    // Create a wrapper that overrides video parameters
    return std::make_shared<VideoParamsOverrideRepresentation>(source, override_params);
}

std::optional<VideoParameters> VideoParamsStage::build_video_parameters(
    const std::optional<VideoParameters>& source_params) const
{
    // Start with source parameters if available, otherwise create new
    VideoParameters params;
    if (source_params.has_value()) {
        params = *source_params;
    }
    
    // Apply overrides for each parameter (only if set, i.e., != -1)
    if (field_width_ >= 0) {
        params.field_width = field_width_;
    }
    if (field_height_ >= 0) {
        params.field_height = field_height_;
    }
    if (colour_burst_start_ >= 0) {
        params.colour_burst_start = colour_burst_start_;
    }
    if (colour_burst_end_ >= 0) {
        params.colour_burst_end = colour_burst_end_;
    }
    if (active_video_start_ >= 0) {
        params.active_video_start = active_video_start_;
    }
    if (active_video_end_ >= 0) {
        params.active_video_end = active_video_end_;
    }
    if (first_active_field_line_ >= 0) {
        params.first_active_field_line = first_active_field_line_;
    }
    if (last_active_field_line_ >= 0) {
        params.last_active_field_line = last_active_field_line_;
    }
    if (white_16b_ire_ >= 0) {
        params.white_16b_ire = white_16b_ire_;
    }
    if (black_16b_ire_ >= 0) {
        params.black_16b_ire = black_16b_ire_;
    }
    
    return params;
}

std::vector<ParameterDescriptor> VideoParamsStage::get_parameter_descriptors(VideoSystem project_format) const
{
    (void)project_format;  // Unused - video params works with all formats
    
    return {
        ParameterDescriptor{
            "fieldWidth",
            "Field Width",
            "Override field width in samples. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},  // min: -1 (unset)
                ParameterValue{int32_t(10000)},  // max: reasonable upper bound
                ParameterValue{int32_t(-1)},  // default: -1 (use source)
                {},  // no allowed strings
                false,  // not required
                std::nullopt  // no dependency
            }
        },
        ParameterDescriptor{
            "fieldHeight",
            "Field Height",
            "Override field height in lines. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},  // min
                ParameterValue{int32_t(1200)},  // max
                ParameterValue{int32_t(-1)},  // default
                {},
                false,
                std::nullopt
            }
        },
        ParameterDescriptor{
            "colourBurstStart",
            "Colour Burst Start",
            "Override colour burst start sample position. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},
                ParameterValue{int32_t(10000)},
                ParameterValue{int32_t(-1)},
                {},
                false,
                std::nullopt
            }
        },
        ParameterDescriptor{
            "colourBurstEnd",
            "Colour Burst End",
            "Override colour burst end sample position. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},
                ParameterValue{int32_t(10000)},
                ParameterValue{int32_t(-1)},
                {},
                false,
                std::nullopt
            }
        },
        ParameterDescriptor{
            "activeVideoStart",
            "Active Video Start",
            "Override active video start sample position. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},
                ParameterValue{int32_t(10000)},
                ParameterValue{int32_t(-1)},
                {},
                false,
                std::nullopt
            }
        },
        ParameterDescriptor{
            "activeVideoEnd",
            "Active Video End",
            "Override active video end sample position. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},
                ParameterValue{int32_t(10000)},
                ParameterValue{int32_t(-1)},
                {},
                false,
                std::nullopt
            }
        },
        ParameterDescriptor{
            "firstActiveFieldLine",
            "First Active Field Line",
            "Override first active field line number. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},
                ParameterValue{int32_t(1200)},
                ParameterValue{int32_t(-1)},
                {},
                false,
                std::nullopt
            }
        },
        ParameterDescriptor{
            "lastActiveFieldLine",
            "Last Active Field Line",
            "Override last active field line number. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},
                ParameterValue{int32_t(1200)},
                ParameterValue{int32_t(-1)},
                {},
                false,
                std::nullopt
            }
        },
        ParameterDescriptor{
            "white16bIRE",
            "White 16-bit IRE",
            "Override white level in 16-bit IRE units. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},
                ParameterValue{int32_t(65535)},
                ParameterValue{int32_t(-1)},
                {},
                false,
                std::nullopt
            }
        },
        ParameterDescriptor{
            "black16bIRE",
            "Black 16-bit IRE",
            "Override black level in 16-bit IRE units. Set to -1 to use source value.",
            ParameterType::INT32,
            ParameterConstraints{
                ParameterValue{int32_t(-1)},
                ParameterValue{int32_t(65535)},
                ParameterValue{int32_t(-1)},
                {},
                false,
                std::nullopt
            }
        }
    };
}

std::map<std::string, ParameterValue> VideoParamsStage::get_parameters() const
{
    return {
        {"fieldWidth", ParameterValue{field_width_}},
        {"fieldHeight", ParameterValue{field_height_}},
        {"colourBurstStart", ParameterValue{colour_burst_start_}},
        {"colourBurstEnd", ParameterValue{colour_burst_end_}},
        {"activeVideoStart", ParameterValue{active_video_start_}},
        {"activeVideoEnd", ParameterValue{active_video_end_}},
        {"firstActiveFieldLine", ParameterValue{first_active_field_line_}},
        {"lastActiveFieldLine", ParameterValue{last_active_field_line_}},
        {"white16bIRE", ParameterValue{white_16b_ire_}},
        {"black16bIRE", ParameterValue{black_16b_ire_}}
    };
}

bool VideoParamsStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    for (const auto& [key, value] : params) {
        if (key == "fieldWidth") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                field_width_ = *val;
            } else {
                return false;
            }
        } else if (key == "fieldHeight") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                field_height_ = *val;
            } else {
                return false;
            }
        } else if (key == "colourBurstStart") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                colour_burst_start_ = *val;
            } else {
                return false;
            }
        } else if (key == "colourBurstEnd") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                colour_burst_end_ = *val;
            } else {
                return false;
            }
        } else if (key == "activeVideoStart") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                active_video_start_ = *val;
            } else {
                return false;
            }
        } else if (key == "activeVideoEnd") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                active_video_end_ = *val;
            } else {
                return false;
            }
        } else if (key == "firstActiveFieldLine") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                first_active_field_line_ = *val;
            } else {
                return false;
            }
        } else if (key == "lastActiveFieldLine") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                last_active_field_line_ = *val;
            } else {
                return false;
            }
        } else if (key == "white16bIRE") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                white_16b_ire_ = *val;
            } else {
                return false;
            }
        } else if (key == "black16bIRE") {
            if (auto* val = std::get_if<int32_t>(&value)) {
                black_16b_ire_ = *val;
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

std::vector<PreviewOption> VideoParamsStage::get_preview_options() const
{
    ORC_LOG_DEBUG("VideoParamsStage::get_preview_options - Called on instance {}, cached_output_ = {}", 
                  static_cast<const void*>(this), static_cast<const void*>(cached_output_.get()));
    if (!cached_output_) {
        return {};
    }
    
    return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage VideoParamsStage::render_preview(
    const std::string& option_id,
    uint64_t index,
    PreviewNavigationHint hint) const
{
    if (!cached_output_) {
        PreviewImage result{};
        return result;
    }
    
    return PreviewHelpers::render_standard_preview(cached_output_, option_id, index, hint);
}

} // namespace orc
