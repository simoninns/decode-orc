/*
 * File:        hackdac_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Hackdac sink stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "hackdac_sink_stage.h"
#include "hackdac_sink_stage_deps.h"
#include "hackdac_sink_stage_deps_interface.h"
#include "logging.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace orc {

HackdacSinkStage::HackdacSinkStage() = default;

NodeTypeInfo HackdacSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::SINK,
        "hackdac_sink",
        "HackDAC Sink",
        "Exports signed 16-bit field data without half-line padding for HackDAC (.hdac) output.",
        1,
        1,
        0,
        0,
        VideoFormatCompatibility::ALL,
        SinkCategory::THIRD_PARTY,
        "Sink (3rd party)"
    };
}

std::vector<ArtifactPtr> HackdacSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    return {};
}

std::vector<ParameterDescriptor> HackdacSinkStage::get_parameter_descriptors(
    VideoSystem project_format,
    SourceType source_type) const {
    (void)project_format;
    (void)source_type;

    std::vector<ParameterDescriptor> descriptors;

    descriptors.push_back(ParameterDescriptor{
        "output_path",
        "HackDAC Output Path",
        "Destination .hdac file (signed 16-bit). A companion .txt report will be written next to it.",
        ParameterType::FILE_PATH,
        ParameterConstraints{std::nullopt, std::nullopt, std::string(""), {}, true, std::nullopt},
        ".hdac"
    });

    return descriptors;
}

std::map<std::string, ParameterValue> HackdacSinkStage::get_parameters() const {
    return parameters_;
}

bool HackdacSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

HackdacSinkStage::ParsedConfig HackdacSinkStage::parse_config(
    const std::map<std::string, ParameterValue>& parameters) const {
    ParsedConfig cfg;

    auto out_it = parameters.find("output_path");
    if (out_it == parameters.end() || !std::holds_alternative<std::string>(out_it->second)) {
        throw std::runtime_error("output_path parameter is required and must be a string");
    }
    cfg.output_path = std::get<std::string>(out_it->second);
    if (cfg.output_path.empty()) {
        throw std::runtime_error("output_path cannot be empty");
    }

    const std::string ext = ".hdac";
    if (cfg.output_path.size() < ext.size() || cfg.output_path.substr(cfg.output_path.size() - ext.size()) != ext) {
        cfg.output_path += ext;
    }

    cfg.report_path = cfg.output_path;
    auto dot_pos = cfg.report_path.find_last_of('.');
    if (dot_pos != std::string::npos) {
        cfg.report_path = cfg.report_path.substr(0, dot_pos);
    }
    cfg.report_path += ".txt";

    return cfg;
}

bool HackdacSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context) {
    (void)observation_context;

    ORC_LOG_DEBUG("HackdacSink: Trigger started");
    is_processing_.store(true);
    cancel_requested_.store(false);

    try {
        if (inputs.empty()) {
            throw std::runtime_error("No input connected");
        }

        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            throw std::runtime_error("Input is not a VideoFieldRepresentation");
        }

        ParsedConfig cfg = parse_config(parameters);

        HackdacSinkExportOptions options;
        options.output_path = cfg.output_path;
        options.report_path = cfg.report_path;

        std::shared_ptr<IHackdacSinkStageDeps> deps = deps_override_;
        if (!deps) {
            auto deps_impl = std::make_shared<HackdacSinkStageDeps>();
            deps_impl->init(progress_callback_, &cancel_requested_);
            deps = deps_impl;
        }

        const auto export_result = deps->export_hackdac(vfr.get(), options);
        if (!export_result.success) {
            last_status_ = export_result.status_message;
            ORC_LOG_ERROR("HackdacSink: {}", export_result.status_message);
            is_processing_.store(false);
            return false;
        }

        last_status_ = export_result.status_message;
        ORC_LOG_INFO("HackdacSink: {}", last_status_);
        is_processing_.store(false);
        return true;

    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("HackdacSink: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

std::string HackdacSinkStage::get_trigger_status() const {
    return last_status_;
}

} // namespace orc
