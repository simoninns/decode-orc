/*
 * File:        dropout_analysis_sink_stage.h
 * Module:      orc-core
 * Purpose:     Dropout Analysis Sink Stage - computes dropout statistics and optionally writes CSV
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_CORE_DROPOUT_ANALYSIS_SINK_STAGE_H
#define ORC_CORE_DROPOUT_ANALYSIS_SINK_STAGE_H

#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "stage_parameter.h"
#include <node_type.h>
#include "video_field_representation.h"
#include "triggerable_stage.h"
#include "dropout_analysis_types.h"
#include "dropout_analysis_sink_deps_interface.h"
#include "../../../sdk/include/orc/plugin/orc_stage_tooling.h"
#include "../../../sdk/include/orc/plugin/orc_stage_preview.h"
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orc {

/**
 * @brief Dropout Analysis Sink Stage
 *
 * Trigger to compute dropout statistics across input fields. Optionally writes CSV.
 * The computed dataset is cached in the stage instance and can be requested by the GUI
 * after a trigger completes.
 */
class DropoutAnalysisSinkStage : public DAGStage,
                                 public ParameterizedStage,
                                 public TriggerableStage,
                                 public StageToolProvider,
                                 public PreviewableStage {
public:
    DropoutAnalysisSinkStage();
    ~DropoutAnalysisSinkStage() override = default;

    void set_deps_override(std::shared_ptr<IDropoutAnalysisSinkStageDeps> deps) { deps_override_ = deps; }

    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override;

    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context) override;

    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 0; }

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

    std::string get_trigger_status() const override { return last_status_; }

    void set_progress_callback(TriggerProgressCallback callback) override { progress_callback_ = callback; }
    bool is_trigger_in_progress() const override { return is_processing_.load(); }
    void cancel_trigger() override { cancel_requested_.store(true); }

    // PreviewableStage interface
    bool supports_preview() const override { return true; }
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(const std::string& option_id, uint64_t index,
                               PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;

    // Result accessors (for MVP requests after trigger)
    const std::vector<FrameDropoutStats>& frame_stats() const { return frame_stats_; }
    int32_t total_frames() const { return total_frames_; }
    bool has_results() const { return has_results_; }
    DropoutAnalysisMode last_mode() const { return last_mode_; }

    std::vector<StageToolDescriptor> get_stage_tools() const override {
        return {
            StageToolDescriptor{
                "dropout_analysis",
                "Dropout Analysis",
                "Compute and visualize per-frame dropout metrics.",
                StageToolKind::BatchAnalysis,
                false,
                "decode-orc.stage-tools.dropout-analysis.v1"
            }
        };
    }

private:
    struct ParsedConfig {
        std::string output_path;
        bool write_csv = false;
        DropoutAnalysisMode mode = DropoutAnalysisMode::FULL_FIELD;
        size_t max_frames = 0;  // 0 = auto-bin to ~1000 points
    };

    ParsedConfig parse_config(const std::map<std::string, ParameterValue>& parameters) const;

    mutable std::shared_ptr<const VideoFieldRepresentation> cached_input_;
    std::map<std::string, ParameterValue> parameters_;
    TriggerProgressCallback progress_callback_;
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};
    std::string last_status_;

    std::vector<FrameDropoutStats> frame_stats_;
    int32_t total_frames_ = 0;
    bool has_results_ = false;
    DropoutAnalysisMode last_mode_ = DropoutAnalysisMode::FULL_FIELD;
    std::shared_ptr<IDropoutAnalysisSinkStageDeps> deps_override_;
};

} // namespace orc


#endif // ORC_CORE_DROPOUT_ANALYSIS_SINK_STAGE_H
