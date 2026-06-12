/*
 * File:        frame_id.h
 * Module:      orc-core
 * Purpose:     Frame identifier types for CVBS_U10_4FSC frame-based pipeline
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <cstdint>
#include <functional>

namespace orc {

// FrameID is a monotonically increasing 0-based index into the frame sequence.
// Unlike the old FieldID, FrameID is a plain integer alias — no wrapper class.
// All time-varying data (video samples, audio, EFM, dropouts) is addressed by
// FrameID in the CVBS_U10_4FSC pipeline.
using FrameID = uint64_t;

// Half-open range [first, last] of frame IDs (inclusive on both ends).
// Use last + 1 == first to represent an empty range.
struct FrameIDRange {
  FrameID first = 0;
  FrameID last = 0;  // inclusive

  constexpr bool contains(FrameID id) const noexcept {
    return id >= first && id <= last;
  }

  constexpr uint64_t count() const noexcept {
    return (last >= first) ? (last - first + 1) : 0;
  }

  constexpr bool empty() const noexcept { return last < first; }
};

}  // namespace orc

// No std::hash<FrameID> specialisation is needed: FrameID is uint64_t, so
// std::hash<uint64_t> (defined by the standard library) is found automatically.
// std::unordered_map<FrameID, T> compiles without any additional hash.
