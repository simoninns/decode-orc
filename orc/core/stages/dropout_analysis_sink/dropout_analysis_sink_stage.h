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

#include "dag_executor.h"
#include "stage_parameter.h"
#include "node_type.h"
#include "video_field_representation.h"
#include "../ld_sink/ld_sink_stage.h"  // For TriggerableStage interface
#include "dropout_analysis_types.h"
#include <atomic>
#include <map>
#include <optional>
#include <string>
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
                                 public TriggerableStage {
public:
    DropoutAnalysisSinkStage();
    ~DropoutAnalysisSinkStage() override = default;

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
        ObservationContext& observation_context) override;

    std::string get_trigger_status() const override { return last_status_; }

    void set_progress_callback(TriggerProgressCallback callback) override { progress_callback_ = callback; }
    bool is_trigger_in_progress() const override { return is_processing_.load(); }
    void cancel_trigger() override { cancel_requested_.store(true); }

    // Result accessors (for MVP requests after trigger)
    const std::vector<FrameDropoutStats>& frame_stats() const { return frame_stats_; }
    int32_t total_frames() const { return total_frames_; }
    bool has_results() const { return has_results_; }
    DropoutAnalysisMode last_mode() const { return last_mode_; }

private:
    struct ParsedConfig {
        std::string output_path;
        bool write_csv = false;
        DropoutAnalysisMode mode = DropoutAnalysisMode::FULL_FIELD;
        size_t max_frames = 0;  // 0 = auto-bin to ~1000 points
    };

    ParsedConfig parse_config(const std::map<std::string, ParameterValue>& parameters) const;

    bool write_csv(const std::string& path) const;
    void compute_stats(const VideoFieldRepresentation& vfr, const ParsedConfig& cfg);

    std::map<std::string, ParameterValue> parameters_;
    TriggerProgressCallback progress_callback_;
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};
    std::string last_status_;

    std::vector<FrameDropoutStats> frame_stats_;
    int32_t total_frames_ = 0;
    bool has_results_ = false;
    DropoutAnalysisMode last_mode_ = DropoutAnalysisMode::FULL_FIELD;
};

} // namespace orc

// Force linker to include this object file
namespace orc { void force_link_DropoutAnalysisSinkStage(); }

#endif // ORC_CORE_DROPOUT_ANALYSIS_SINK_STAGE_H
