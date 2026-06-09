/*
 * File:        snr_analysis_sink_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for SNRAnalysisSinkStage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_SNR_ANALYSIS_SINK_DEPS_INTERFACE_H
#define ORC_CORE_SNR_ANALYSIS_SINK_DEPS_INTERFACE_H

#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "observation_context_interface.h"
#include "snr_analysis_types.h"
#include "triggerable_stage.h"
#include "video_field_representation.h"

namespace orc {
struct SNRAnalysisComputeOptions {
  std::string output_path;
  bool write_csv{false};
  size_t max_frames{0};
  SNRAnalysisMode snr_mode{SNRAnalysisMode::BOTH};
};

struct SNRAnalysisComputeResult {
  bool success{false};
  std::string message;
  std::vector<FrameSNRStats> frame_stats;
  int32_t total_frames{0};
};

class ISNRAnalysisSinkStageDeps {
 public:
  virtual ~ISNRAnalysisSinkStageDeps() = default;

  virtual void init(TriggerProgressCallback progress_callback,
                    std::atomic<bool>* cancel_requested) = 0;

  virtual SNRAnalysisComputeResult compute_and_analyze(
      VideoFieldRepresentation* representation,
      IObservationContext& observation_context,
      SNRAnalysisComputeOptions options) = 0;

  virtual bool write_csv(const std::string& path,
                         const std::vector<FrameSNRStats>& frame_stats) = 0;
};
}  // namespace orc

#endif  // ORC_CORE_SNR_ANALYSIS_SINK_DEPS_INTERFACE_H
