/*
 * File:        efm_sink_stage.h
 * Module:      orc-core
 * Purpose:     EFM Decoder Sink Stage - decodes EFM t-values to audio WAV or data sectors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_EFM_DECODE_SINK_STAGE_H
#define ORC_CORE_EFM_DECODE_SINK_STAGE_H

#include "dag_executor.h"
#include "stage_parameter.h"
#include <node_type.h>
#include "video_field_representation.h"
#include "../triggerable_stage.h"
#include <string>
#include <memory>
#include <functional>
#include <atomic>

namespace orc {

/**
 * @brief EFM Decoder Sink Stage
 *
 * Accumulates EFM t-values from a VideoFieldRepresentation and decodes them
 * to either a PCM/WAV audio file or ECMA-130 binary sector data using the
 * full EFM decode pipeline (EfmProcessor).
 *
 * This is a SINK stage - it has inputs but no outputs.
 *
 * Parameters:
 * - output_path           : Output file path
 * - decode_mode           : "audio" (default) or "data"
 * - no_timecodes          : Disable timecode output (audio mode)
 * - audacity_labels       : Write Audacity label file  (audio only)
 * - no_audio_concealment  : Disable audio concealment  (audio only)
 * - zero_pad              : Zero-pad short/missing audio (audio only)
 * - no_wav_header         : Output raw PCM without WAV header (audio only)
 * - output_metadata       : Write bad-sector map (data only)
 * - report                : Write decode report file
 */
class EFMSinkStage : public DAGStage,
                     public ParameterizedStage,
                     public TriggerableStage {
public:
    EFMSinkStage();
    ~EFMSinkStage() override = default;

    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override;

    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context) override;

    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 0; }  // Sink has no outputs

    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(
        VideoSystem project_format = VideoSystem::Unknown,
        SourceType source_type = SourceType::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

    // TriggerableStage interface
    bool trigger(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        IObservationContext& observation_context) override;

    std::string get_trigger_status() const override;

    void set_progress_callback(TriggerProgressCallback callback) override {
        progress_callback_ = callback;
    }

    bool is_trigger_in_progress() const override {
        return is_processing_.load();
    }

    void cancel_trigger() override {
        cancel_requested_.store(true);
    }

private:
    std::map<std::string, ParameterValue> parameters_;

    TriggerProgressCallback progress_callback_;
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};
    std::string last_status_;
};

} // namespace orc

#endif // ORC_CORE_EFM_DECODE_SINK_STAGE_H
