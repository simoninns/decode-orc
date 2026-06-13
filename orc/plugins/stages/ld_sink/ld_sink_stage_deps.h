/*
 * File:        ld_sink_stage_deps.h
 * Module:      orc-core
 * Purpose:     ld-decode Sink Stage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_LD_SINK_STAGE_DEPS_H
#define ORC_CORE_LD_SINK_STAGE_DEPS_H

#include <atomic>
#include <functional>
#include <memory>
#include <utility>

#include "ld_sink_stage_deps_interface.h"
#include "observation_context_interface.h"
#include "tbc_metadata_writer_interface.h"
#include "triggerable_stage.h"
#include "video_frame_representation.h"

namespace orc {
class IStageServices;

class LDSinkStageDeps : public ILDSinkStageDeps {
 public:
  LDSinkStageDeps(IStageServices* stage_services,
                  std::shared_ptr<ITBCMetadataWriter> metadata_writer)
      : stage_services_(stage_services),
        metadata_writer_(std::move(metadata_writer)) {}

  void init(TriggerProgressCallback progress_callback,
            std::atomic<bool>* pIsProcessing,
            std::atomic<bool>* pCancelRequested);

  bool write_tbc_and_metadata(
      const VideoFrameRepresentation* representation,
      const std::string& tbc_path,
      IObservationContext& observation_context) override;

 private:
  TriggerProgressCallback progress_callback_;
  std::atomic<bool>* pIsProcessing_{};
  std::atomic<bool>* pCancelRequested_{};
  IStageServices* stage_services_{};
  std::shared_ptr<ITBCMetadataWriter> metadata_writer_;
};
}  // namespace orc

#endif  // ORC_CORE_LD_SINK_STAGE_DEPS_H
