/*
 * File:        efm_decoder_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     EFM Decoder Sink Stage - decodes EFM t-values to audio/data
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "efm_decoder_sink_stage.h"
#include "logging.h"
#include "stage_parameter.h"
#include <stage_registry.h>
#include <stdexcept>

namespace orc {

// Register this stage with the registry
ORC_REGISTER_STAGE(EFMDecoderSinkStage)

// Force linker to include this object file
void force_link_EFMDecoderSinkStage() {}

EFMDecoderSinkStage::EFMDecoderSinkStage()
    : parameters_(efm_decoder_config::default_parameters())
{
    efm_decoder_config::ParsedParameters parsed;
    std::string error_message;
    if (efm_decoder_config::parse_parameters(parameters_, parsed, error_message)) {
        parsed_parameters_ = parsed;
    }
}

NodeTypeInfo EFMDecoderSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::SINK,
        "EFMDecoderSink",
        "EFM Decoder Sink",
        "Decodes EFM t-values from VFR to audio/data outputs with configurable decode parameters",
        1, 1,  // One input (VideoFieldRepresentation)
        0, 0,  // No outputs (sink writes to disk)
        VideoFormatCompatibility::ALL
    };
}

std::vector<ParameterDescriptor> EFMDecoderSinkStage::get_parameter_descriptors(
    VideoSystem project_format, 
    SourceType source_type
) const {
    (void)project_format;
    (void)source_type;

    return efm_decoder_config::get_parameter_descriptors();
}

std::map<std::string, ParameterValue> EFMDecoderSinkStage::get_parameters() const {
    return parameters_;
}

bool EFMDecoderSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    efm_decoder_config::ParsedParameters parsed;
    std::string error_message;
    if (!efm_decoder_config::parse_parameters(params, parsed, error_message)) {
        last_status_ = std::string("Error: ") + error_message;
        ORC_LOG_ERROR("EFMDecoderSink: {}", error_message);
        return false;
    }

    parameters_ = parsed.normalized_parameters;
    parsed_parameters_ = parsed;
    return true;
}

std::vector<ArtifactPtr> EFMDecoderSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context
) {
    // Sink stages don't produce outputs in execute()
    // Actual work happens in trigger()
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    return {};
}

bool EFMDecoderSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context
) {
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    
    is_processing_.store(true);
    cancel_requested_.store(false);
    
    try {
        efm_decoder_config::ParsedParameters parsed;
        std::string error_message;
        if (!efm_decoder_config::parse_parameters(parameters, parsed, error_message)) {
            throw std::runtime_error(error_message);
        }

        parsed_parameters_ = parsed;

        // Phase 4: Implement decode pipeline integration:
        // 1. Validate inputs (check VFR has EFM)
        // 2. Translate parameters to decoder config
        // 3. Extract EFM t-values from VFR
        // 4. Run decoder pipeline
        // 5. Write output files according to mode
        // 6. Generate and optionally persist report
        
        const std::string mode = parsed.decoder_config.global.mode == DecoderMode::Audio ? "audio" : "data";
        last_status_ = "Phase 3: Parameters validated and parsed (mode=" + mode + ")";
        ORC_LOG_INFO("EFMDecoderSink: {}", last_status_);
        is_processing_.store(false);
        return true;
        
    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("EFMDecoderSink: {}", last_status_);
        is_processing_.store(false);
        return false;
    }
}

std::string EFMDecoderSinkStage::get_trigger_status() const {
    return last_status_;
}

} // namespace orc
