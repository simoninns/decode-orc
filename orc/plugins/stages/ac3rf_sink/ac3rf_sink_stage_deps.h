/*
 * File:        ac3rf_sink_stage_deps.h
 * Module:      orc-core
 * Purpose:     AC3RFSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_AC3RF_SINK_STAGE_DEPS_H
#define ORC_CORE_AC3RF_SINK_STAGE_DEPS_H

#include <atomic>

#include "ac3rf_sink_stage_deps_interface.h"
#include "triggerable_stage.h"

namespace orc {
class AC3RFSinkStageDeps : public IAC3RFSinkStageDeps {
 public:
  AC3RFSinkStageDeps() = default;

  void init(TriggerProgressCallback progress_callback,
            std::atomic<bool>* cancel_requested);

  AC3RFSinkDecodeResult decode_and_write_ac3(
      const VideoFieldRepresentation* representation,
      const std::string& output_path) override;

 private:
  TriggerProgressCallback progress_callback_;
  std::atomic<bool>* cancel_requested_{nullptr};
};
}  // namespace orc

#endif  // ORC_CORE_AC3RF_SINK_STAGE_DEPS_H
