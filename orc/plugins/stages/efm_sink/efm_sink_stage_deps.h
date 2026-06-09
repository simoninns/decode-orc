/*
 * File:        efm_sink_stage_deps.h
 * Module:      orc-core
 * Purpose:     EFMSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_EFM_SINK_STAGE_DEPS_H
#define ORC_CORE_EFM_SINK_STAGE_DEPS_H

#include "efm_sink_stage_deps_interface.h"

namespace orc {
class EFMSinkStageDeps : public IEFMSinkStageDeps {
 public:
  EFMSinkStageDeps() = default;

  void init(TriggerProgressCallback progress_callback,
            std::atomic<bool>* cancel_requested) override;

  EFMSinkDecodeResult decode_efm(const VideoFieldRepresentation* representation,
                                 const EFMSinkOptions& options) override;

 private:
  TriggerProgressCallback progress_callback_;
  std::atomic<bool>* cancel_requested_{nullptr};
};
}  // namespace orc

#endif  // ORC_CORE_EFM_SINK_STAGE_DEPS_H
