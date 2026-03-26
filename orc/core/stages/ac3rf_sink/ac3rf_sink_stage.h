/*
 * File:        ac3rf_sink_stage.h
 * Module:      orc-core
 * Purpose:     AC3 RF Sink Stage - decodes AC3 RF samples and writes AC3 frames
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_AC3RF_SINK_STAGE_H
#define ORC_CORE_AC3RF_SINK_STAGE_H

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
 * @brief AC3 RF Sink Stage
 *
 * Decodes AC3 RF samples from the VideoFieldRepresentation and writes the
 * resulting AC3 frames to an output file.
 *
 * This is a SINK stage — it takes one VFR input and produces no outputs.
 * Actual decoding is deferred to trigger(), which the GUI invokes when the
 * user clicks the export button.
 *
 * The AC3 RF data must be present in the source stage's VFR (i.e. the source
 * stage must have been given an AC3 RF input file so that has_ac3_rf() returns
 * true).
 *
 * Parameters:
 * - output_path: Output .ac3 file path
 */
class AC3RFSinkStage : public DAGStage,
                       public ParameterizedStage,
                       public TriggerableStage {
public:
    AC3RFSinkStage();
    ~AC3RFSinkStage() override = default;

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

#endif // ORC_CORE_AC3RF_SINK_STAGE_H
