/*
 * File:        efm_decoder_sink_stage.h
 * Module:      orc-core
 * Purpose:     EFM Decoder Sink Stage - decodes EFM t-values to audio/data with mapped parameters
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_EFM_DECODER_SINK_STAGE_H
#define ORC_CORE_EFM_DECODER_SINK_STAGE_H

#include "dag_executor.h"
#include "stage_parameter.h"
#include <node_type.h>
#include "video_field_representation.h"
#include "../ld_sink/ld_sink_stage.h"  // For TriggerableStage interface
#include "config/efm_decoder_parameter_contract.h"
#include <map>
#include <memory>
#include <atomic>
#include <optional>
#include <string>

namespace orc {

/**
 * @class EFMDecoderSinkStage
 * @brief Sink stage that decodes EFM t-values from VFR into audio/data outputs
 *
 * This stage:
 * - Accepts a VideoFieldRepresentation input containing EFM t-value data
 * - Translates mapped Orc parameters to decoder configuration
 * - Triggers on-demand decoding via the trigger() interface
 * - Produces audio (WAV/PCM) or data outputs with optional metadata/labels
 * - Generates a textual decode report
 *
 * Phase 1 deliverable: Stage integration contract with frozen parameter schema and
 * validation behavior. Decode execution pipeline remains a later phase.
 */
class EFMDecoderSinkStage : public DAGStage,
                            public ParameterizedStage,
                            public TriggerableStage {
public:
    EFMDecoderSinkStage();
    ~EFMDecoderSinkStage() override = default;

    // DAGStage interface
    std::string version() const override {
        return "0.2.0";  // Phase 1 contract freeze
    }
    
    NodeTypeInfo get_node_type_info() const override;
    
    // Parameter handling (Phase 3 will implement full translation)
    std::vector<ParameterDescriptor> get_parameter_descriptors(
        VideoSystem project_format = VideoSystem::Unknown,
        SourceType source_type = SourceType::Unknown
    ) const override;
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context
    ) override;

    size_t required_input_count() const override {
        return 1;  // Requires one VFR input
    }
    
    size_t output_count() const override {
        return 0;  // Sink produces no artifacts (writes to disk)
    }

    // ParameterizedStage interface
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

    // TriggerableStage interface
    bool trigger(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context
    ) override;
    
    std::string get_trigger_status() const override;
    
    void set_progress_callback(TriggerProgressCallback callback) override {
        progress_callback_ = callback;
    }

private:
    std::map<std::string, ParameterValue> parameters_;
    std::optional<efm_decoder_config::ParsedParameters> parsed_parameters_;
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};
    std::string last_status_;
    TriggerProgressCallback progress_callback_;
};

} // namespace orc

#endif // ORC_CORE_EFM_DECODER_SINK_STAGE_H
