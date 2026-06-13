/*
 * File:        cc_sink_stage.h
 * Module:      orc-core
 * Purpose:     Closed Caption Sink Stage - exports CC data to SCC or plain text
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_CC_SINK_STAGE_H
#define ORC_CORE_CC_SINK_STAGE_H

#include <node_type.h>

#include <atomic>
#include <memory>
#include <string>

#include "../../../sdk/include/orc/plugin/orc_stage_runtime.h"
#include "cc_sink_stage_deps_interface.h"
#include "stage_parameter.h"
#include "triggerable_stage.h"
#include "video_frame_representation.h"

namespace orc {

/**
 * @brief Closed Caption Sink Stage
 *
 * Extracts closed caption data from TBC metadata and exports it in either:
 * - Scenarist SCC V1.0 format: Industry standard subtitle format with timing
 * - Plain text format: Human-readable text with control codes stripped
 *
 * This is a SINK stage - it has inputs but no outputs.
 *
 * The closed caption data is extracted from line 21 (NTSC) or line 22 (PAL)
 * of the VBI area. Each caption consists of two bytes of data, which can be
 * either command bytes (0x10-0x1F) or character bytes (0x20-0x7E).
 *
 * SCC Format:
 * - Header: "Scenarist_SCC V1.0"
 * - Timestamps in format HH:MM:SS:FF (non-drop frame)
 * - Hex byte pairs (e.g., "1441" for bytes 0x14 and 0x41)
 * - Captions separated by blank lines
 *
 * Plain Text Format:
 * - Only printable ASCII characters (0x20-0x7E)
 * - Control codes (0x10-0x1F) are stripped out
 * - Preserves caption timing boundaries with blank lines
 *
 * Parameters:
 * - output_path: Output file path (.scc or .txt)
 * - format: Export format (SCC or PLAIN_TEXT)
 */
class CCSinkStage : public DAGStage,
                    public ParameterizedStage,
                    public TriggerableStage {
 public:
  CCSinkStage();
  ~CCSinkStage() override = default;

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

  void set_deps_override(std::shared_ptr<ICCSinkStageDeps> deps) {
    deps_override_ = deps;
  }

 private:
  struct ParsedConfig {
    std::string output_path;
    CCExportFormat format{CCExportFormat::SCC};
    bool write_csv{false};
  };

  ParsedConfig parse_config(
      const std::map<std::string, ParameterValue>& parameters) const;

  // Member variables
  std::map<std::string, ParameterValue> parameters_;
  TriggerProgressCallback progress_callback_;
  std::atomic<bool> is_processing_{false};
  std::atomic<bool> cancel_requested_{false};
  std::shared_ptr<ICCSinkStageDeps> deps_override_;
};

}  // namespace orc

#endif  // ORC_CORE_CC_SINK_STAGE_H
