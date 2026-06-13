/*
 * File:        ld_sink_stage_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for 'ld-decode Sink Stage dependencies'
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_LD_SINK_STAGE_DEPS_INTERFACE_H
#define ORC_CORE_LD_SINK_STAGE_DEPS_INTERFACE_H

#include <string>

#include "observation_context_interface.h"
#include "video_frame_representation.h"

namespace orc {
class ILDSinkStageDeps {
 public:
  virtual ~ILDSinkStageDeps() = default;

  virtual bool write_tbc_and_metadata(
      const VideoFrameRepresentation* representation,
      const std::string& tbc_path,
      IObservationContext& observation_context) = 0;
};
}  // namespace orc

#endif  // ORC_CORE_LD_SINK_STAGE_DEPS_INTERFACE_H
