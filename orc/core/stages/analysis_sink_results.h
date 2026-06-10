/*
 * File:        analysis_sink_results.h
 * Module:      orc-core
 * Purpose:     Interfaces for accessing analysis sink stage results across
 *              shared library boundaries
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_SINK_RESULTS_H
#define ORC_CORE_ANALYSIS_SINK_RESULTS_H

#include <common_types.h>

#include <cstdint>
#include <vector>

namespace orc {

// On macOS, dynamic_cast to a concrete plugin class fails when that class is
// defined in a dylib that is also included in the host binary (duplicate
// type_info pointers across DSOs). Casting to these interfaces — defined in
// orc-core, which is always a single shared symbol source — works reliably on
// all platforms.

class IDropoutAnalysisResults {
 public:
  virtual bool has_results() const = 0;
  virtual const std::vector<FrameDropoutStats>& frame_stats() const = 0;
  virtual int32_t total_frames() const = 0;
  virtual ~IDropoutAnalysisResults() = default;
};

class ISNRAnalysisResults {
 public:
  virtual bool has_results() const = 0;
  virtual const std::vector<FrameSNRStats>& frame_stats() const = 0;
  virtual int32_t total_frames() const = 0;
  virtual ~ISNRAnalysisResults() = default;
};

class IBurstLevelAnalysisResults {
 public:
  virtual bool has_results() const = 0;
  virtual const std::vector<FrameBurstLevelStats>& frame_stats() const = 0;
  virtual int32_t total_frames() const = 0;
  virtual ~IBurstLevelAnalysisResults() = default;
};

}  // namespace orc

#endif  // ORC_CORE_ANALYSIS_SINK_RESULTS_H
