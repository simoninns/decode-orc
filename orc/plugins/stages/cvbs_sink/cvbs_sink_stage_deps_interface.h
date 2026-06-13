/*
 * File:        cvbs_sink_stage_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for CVBSSinkStage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_CVBS_SINK_STAGE_DEPS_INTERFACE_H
#define ORC_CORE_CVBS_SINK_STAGE_DEPS_INTERFACE_H

#include <atomic>
#include <string>

#include "triggerable_stage.h"
#include "video_frame_representation.h"

namespace orc {

struct CVBSSinkWriteResult {
  bool success{false};
  uint64_t frames_written{0};
  std::string status_message;
};

class ICVBSSinkStageDeps {
 public:
  virtual ~ICVBSSinkStageDeps() = default;

  virtual void init(TriggerProgressCallback progress_callback,
                    std::atomic<bool>* cancel_requested) = 0;

  virtual CVBSSinkWriteResult write_cvbs(
      const VideoFrameRepresentation* representation,
      const std::string& output_path) = 0;
};

}  // namespace orc

#endif  // ORC_CORE_CVBS_SINK_STAGE_DEPS_INTERFACE_H
