/*
 * File:        dropout_analysis_sink_deps.h
 * Module:      orc-core
 * Purpose:     DropoutAnalysisSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_DROPOUT_ANALYSIS_SINK_DEPS_H
#define ORC_CORE_DROPOUT_ANALYSIS_SINK_DEPS_H

#include <atomic>
#include <utility>

#include "dropout_analysis_sink_deps_interface.h"
#include "logging.h"

namespace orc {
class DropoutAnalysisSinkStageDeps : public IDropoutAnalysisSinkStageDeps {
 public:
  DropoutAnalysisSinkStageDeps() = default;

  void init(TriggerProgressCallback progress_callback,
            std::atomic<bool>* cancel_requested) override;

  DropoutAnalysisComputeResult compute_and_analyze(
      VideoFieldRepresentation* representation,
      IObservationContext& observation_context,
      DropoutAnalysisComputeOptions options) override;

  bool write_csv(const std::string& path,
                 const std::vector<FrameDropoutStats>& frame_stats) override;

 private:
  class SpdlogLoggerAdapter {
   public:
    template <typename... Args>
    void trace(const char* fmt, Args&&... args) const {
      ORC_LOG_TRACE(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void debug(const char* fmt, Args&&... args) const {
      ORC_LOG_DEBUG(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void info(const char* fmt, Args&&... args) const {
      ORC_LOG_INFO(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void warn(const char* fmt, Args&&... args) const {
      ORC_LOG_WARN(fmt, std::forward<Args>(args)...);
    }

    template <typename... Args>
    void error(const char* fmt, Args&&... args) const {
      ORC_LOG_ERROR(fmt, std::forward<Args>(args)...);
    }
  };

  TriggerProgressCallback progress_callback_;
  std::atomic<bool>* cancel_requested_{nullptr};
  SpdlogLoggerAdapter logger_;
};
}  // namespace orc

#endif  // ORC_CORE_DROPOUT_ANALYSIS_SINK_DEPS_H
