/*
 * File:        burst_level_analysis_sink_stage.h
 * Module:      orc-core
 * Purpose:     Burst Level Analysis Sink Stage - computes burst statistics and
 * optionally writes CSV
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_CORE_BURST_LEVEL_ANALYSIS_SINK_STAGE_H
#define ORC_CORE_BURST_LEVEL_ANALYSIS_SINK_STAGE_H

#include <node_type.h>

#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../../../sdk/include/orc/plugin/orc_stage_preview.h"
#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "../../../sdk/include/orc/plugin/orc_stage_tooling.h"
#include "burst_level_analysis_sink_deps_interface.h"
#include "burst_level_analysis_types.h"
#include "stage_parameter.h"
#include "triggerable_stage.h"
#include "video_field_representation.h"

namespace orc {

/**
 * @brief Burst Level Analysis Sink Stage
 *
 * Trigger to compute burst level stats across input fields. Optionally writes
 * CSV. Dataset is cached for GUI retrieval after trigger.
 */
class BurstLevelAnalysisSinkStage : public DAGStage,
                                    public ParameterizedStage,
                                    public TriggerableStage,
                                    public StageToolProvider,
                                    public PreviewableStage {
 public:
  BurstLevelAnalysisSinkStage();
  ~BurstLevelAnalysisSinkStage() override = default;

  void set_deps_override(
      std::shared_ptr<IBurstLevelAnalysisSinkStageDeps> deps) {
    deps_override_ = deps;
  }

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
      VideoSystem project_format, SourceType source_type) const override;
  using ParameterizedStage::get_parameter_descriptors;
  std::map<std::string, ParameterValue> get_parameters() const override;
  bool set_parameters(
      const std::map<std::string, ParameterValue>& params) override;

  // TriggerableStage interface
  bool trigger(const std::vector<ArtifactPtr>& inputs,
               const std::map<std::string, ParameterValue>& parameters,
               IObservationContext& observation_context) override;

  std::string get_trigger_status() const override { return last_status_; }

  void set_progress_callback(TriggerProgressCallback callback) override {
    progress_callback_ = callback;
  }
  bool is_trigger_in_progress() const override { return is_processing_.load(); }
  void cancel_trigger() override { cancel_requested_.store(true); }

  // PreviewableStage interface
  bool supports_preview() const override { return true; }
  std::vector<PreviewOption> get_preview_options() const override;
  PreviewImage render_preview(const std::string& option_id, uint64_t index,
                              PreviewNavigationHint hint) const override;

  // Result accessors
  const std::vector<FrameBurstLevelStats>& frame_stats() const {
    return frame_stats_;
  }
  int32_t total_frames() const { return total_frames_; }
  bool has_results() const { return has_results_; }

  std::vector<StageToolDescriptor> get_stage_tools() const override {
    return {StageToolDescriptor{
        "burst_level_analysis", "Burst Level Analysis",
        "Compute and visualize per-frame colour-burst levels.",
        StageToolKind::BatchAnalysis, false,
        "decode-orc.stage-tools.burst-level-analysis.v1"}};
  }

 private:
  struct ParsedConfig {
    std::string output_path;
    bool write_csv = false;
    size_t max_frames =
        1000;  // Default to 1000 frames to avoid GUI memory issues
  };

  ParsedConfig parse_config(
      const std::map<std::string, ParameterValue>& parameters) const;

  mutable std::shared_ptr<const VideoFieldRepresentation> cached_input_;
  std::map<std::string, ParameterValue> parameters_;
  TriggerProgressCallback progress_callback_;
  std::atomic<bool> is_processing_{false};
  std::atomic<bool> cancel_requested_{false};
  std::string last_status_;

  std::vector<FrameBurstLevelStats> frame_stats_;
  int32_t total_frames_ = 0;
  bool has_results_ = false;
  std::shared_ptr<IBurstLevelAnalysisSinkStageDeps> deps_override_;
};

}  // namespace orc

#endif  // ORC_CORE_BURST_LEVEL_ANALYSIS_SINK_STAGE_H
