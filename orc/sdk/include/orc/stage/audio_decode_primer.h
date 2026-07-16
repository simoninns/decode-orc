/*
 * File:        audio_decode_primer.h
 * Module:      orc-plugin-sdk
 * Purpose:     Optional capability interface letting a sink force a deferred,
 *              whole-stream audio decode to run up front with progress
 *              reporting, instead of stalling silently on first sample access
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace orc {

// Optional capability implemented by representations whose audio is produced
// by an expensive whole-stream decode that is deferred (lazily run on first
// sample access). Such a decode cannot be metered per-frame from a sink's
// export loop — it fires monolithically inside the first get_audio_samples()
// call and freezes the progress dialog until it completes.
//
// A representation that implements this interface lets a sink "prime" that
// decode before its export loop, routing the decode's internal progress to the
// sink's progress dialog (mirroring how closed-caption/VBI pre-passes report).
// The mechanism is additive: it introduces no member or vtable change to any
// existing SDK type, so it does not affect the plugin host ABI. Sinks reach it
// with dynamic_cast on their input representation; a representation that does
// not implement it (or an older plugin build) simply casts to nullptr and the
// sink falls back to the deferred-on-first-access behaviour.
class IAudioDecodePrimer {
 public:
  virtual ~IAudioDecodePrimer() = default;

  // Progress callback: (done, total, message). |total| may be 0 when the work
  // size is not yet known, in which case only |message| is meaningful.
  using ProgressFn =
      std::function<void(uint64_t done, uint64_t total, const std::string&)>;

  // Force any deferred whole-stream audio decode backing this representation to
  // run now, reporting progress through |progress|. Idempotent: after the first
  // call subsequent calls (including the implicit one from get_audio_samples())
  // return immediately. Implementations must tolerate a null/empty |progress|.
  virtual void prime_audio_decode(const ProgressFn& progress) const = 0;
};

}  // namespace orc
