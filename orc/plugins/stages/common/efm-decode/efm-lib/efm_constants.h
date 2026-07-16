/*
 * File:        efm_constants.h
 * Purpose:     EFM-library - shared EFM/CIRC geometry constants
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 *
 * These are the recurring "magic numbers" of the EFM / CIRC pipeline, gathered
 * here with their IEC 60908 / ECMA-130 references so the geometry is defined
 * once. Values are unchanged from the inline literals they replaced.
 */

#ifndef EFM_CONSTANTS_H
#define EFM_CONSTANTS_H

#include <cstdint>

namespace efm {

// A single EFM channel frame is 588 channel bits: a 24-bit sync header, then
// 33 x (14-bit symbol + 3 merging bits) for the subcode symbol plus 32 data
// symbols. (IEC 60908 §18 "Channel frame".)
constexpr int kEfmFrameChannelBits = 588;

// Plausibility band around kEfmFrameChannelBits used when the t-value state
// machine decides whether a run of t-values between two sync headers is a
// single valid frame rather than an over-/undershoot. A well-formed frame is
// exactly 588 bits; this open interval (min, max) accepts the near-misses that
// survive a couple of corrupted merging-bit runs.
constexpr int kFrameBitCountAcceptMin = 550;
constexpr int kFrameBitCountAcceptMax = 600;

// A subcode section / audio block is 98 consecutive frames. (IEC 60908 §19
// "Control and display - Subcoding": one subcode byte per frame, 98 frames per
// section including the S0/S1 sync frames.)
constexpr int kFramesPerSection = 98;

// A raw (unscrambled) CD sector is 2352 bytes. (ECMA-130 §14 "Sector".)
constexpr int kRawSectorSize = 2352;

// The EFM frame sync is two consecutive T11 pulses (an 11-bit run of the same
// polarity). 0x0B == 11 is the t-value that represents one such pulse.
constexpr uint8_t kSyncSymbolT11 = 0x0B;

// The t-value input buffer is kept trimmed to at most two frames' worth of
// t-values (2 x 191 = 382) so the sync search never scans an unbounded backlog.
constexpr int kMaxTvalueBufferSize = 382;

// When a candidate sync is reached, it is rejected as a false positive only if
// BOTH the accumulated error-byte count and padding-byte count exceed this many
// bytes - a high padding count alone is expected after gap-filling and does not
// by itself indicate a spurious sync.
constexpr int kSectorFalsePositiveByteThreshold = 1000;

}  // namespace efm

#endif  // EFM_CONSTANTS_H
