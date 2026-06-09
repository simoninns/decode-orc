/*
 * File:        cc_sink_stage_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for CCSinkStage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_CC_SINK_STAGE_DEPS_INTERFACE_H
#define ORC_CORE_CC_SINK_STAGE_DEPS_INTERFACE_H

#include <atomic>
#include <cstdint>
#include <string>

#include "observation_context_interface.h"
#include "triggerable_stage.h"
#include "video_field_representation.h"

namespace orc {
/**
 * @brief Closed Caption output format
 */
enum class CCExportFormat {
  SCC,        ///< Scenarist SCC V1.0 format (industry standard)
  PLAIN_TEXT  ///< Plain text with control codes stripped
};

struct CCExportOptions {
  std::string output_path;
  CCExportFormat export_format{CCExportFormat::SCC};
  bool write_csv{false};
};

struct CCExportResult {
  bool success{false};
  std::string message;
  int32_t cc_frames_exported{0};
};

class ICCSinkStageDeps {
 public:
  virtual ~ICCSinkStageDeps() = default;

  virtual void init(TriggerProgressCallback progress_callback,
                    std::atomic<bool>* cancel_requested) = 0;

  virtual CCExportResult export_cc(VideoFieldRepresentation* representation,
                                   IObservationContext& observation_context,
                                   CCExportOptions options) = 0;
};
}  // namespace orc

#endif  // ORC_CORE_CC_SINK_STAGE_DEPS_INTERFACE_H
