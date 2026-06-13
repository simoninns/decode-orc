/*
 * File:        cvbs_sink_stage_deps.h
 * Module:      orc-core
 * Purpose:     CVBSSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_CVBS_SINK_STAGE_DEPS_H
#define ORC_CORE_CVBS_SINK_STAGE_DEPS_H

#include <atomic>
#include <string>
#include <utility>

#include "cvbs_sink_stage_deps_interface.h"

namespace orc {

class CVBSSinkStageDeps : public ICVBSSinkStageDeps {
 public:
  CVBSSinkStageDeps() = default;

  void init(TriggerProgressCallback progress_callback,
            std::atomic<bool>* cancel_requested) override;

  CVBSSinkWriteResult write_cvbs(
      const VideoFrameRepresentation* representation,
      const std::string& output_path) override;

 private:
  TriggerProgressCallback progress_callback_;
  std::atomic<bool>* cancel_requested_{nullptr};
};

}  // namespace orc

#endif  // ORC_CORE_CVBS_SINK_STAGE_DEPS_H
