/*
 * File:        ld_sink_stage.h
 * Module:      orc-core
 * Purpose:     ld-decode sink Stage - writes TBC and metadata to disk
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_LD_SINK_STAGE_H
#define ORC_CORE_LD_SINK_STAGE_H

#include <node_type.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

#include "../../../sdk/include/orc/plugin/orc_stage_preview.h"
#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "observation_schema.h"
#include "stage_parameter.h"
#include "triggerable_stage.h"
#include "video_field_representation.h"

namespace orc {

class IStageServices;
class ILDSinkStageDeps;

/**
 * @brief ld-decode Sink Stage
 *
 * Writes TBC fields and metadata to disk in format compatible with legacy
 * tools. This is a SINK stage - it has inputs but no outputs.
 *
 * When triggered, it reads all fields from its input and writes them to:
 * - TBC file: Raw field data
 * - .db file: Metadata including all observations and hints
 *
 * This sink supports preview - it shows what will be written to disk.
 *
 * Parameters:
 * - output_path: Output file path (metadata will be output_path + ".db")
 */
class LDSinkStage : public DAGStage,
                    public ParameterizedStage,
                    public TriggerableStage,
                    public PreviewableStage {
 public:
  explicit LDSinkStage(IStageServices* stage_services);

  /// Testing seam: inject a pre-built deps instance to substitute concrete dep
  /// creation in trigger().
  void set_deps_override(std::shared_ptr<ILDSinkStageDeps> deps) {
    deps_override_ = std::move(deps);
  }
  ~LDSinkStage() override = default;

  // DAGStage interface
  std::string version() const override { return "1.0"; }
  NodeTypeInfo get_node_type_info() const override;
  std::vector<ObservationKey> get_provided_observations() const override {
    return {ObservationKey{"export", "seq_no", ObservationType::INT64,
                           "1-based sequence number for field", false},
            ObservationKey{"export", "is_first_field", ObservationType::BOOL,
                           "Field parity: first field (true) or second (false)",
                           true}};
  }

  std::vector<ArtifactPtr> execute(
      const std::vector<ArtifactPtr>& inputs,
      const std::map<std::string, ParameterValue>& parameters,
      ObservationContext& observation_context) override;

  size_t required_input_count() const override { return 1; }
  size_t output_count() const override { return 0; }  // Sink has no outputs

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

  std::string get_trigger_status() const override;

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

 private:
  std::string output_path_;
  std::string trigger_status_;
  mutable std::shared_ptr<const VideoFieldRepresentation>
      cached_input_;  // For preview
  TriggerProgressCallback
      progress_callback_;  // Progress callback for trigger operations
  std::atomic<bool> is_processing_{false};
  std::atomic<bool> cancel_requested_{false};
  IStageServices* stage_services_{nullptr};
  std::shared_ptr<ILDSinkStageDeps> deps_override_;
};

}  // namespace orc

#endif  // ORC_CORE_LD_SINK_STAGE_H
