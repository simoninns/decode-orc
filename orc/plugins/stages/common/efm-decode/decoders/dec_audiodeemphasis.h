/*
 * File:        dec_audiodeemphasis.h
 * Purpose:     efm-decoder-audio - 50/15 us audio de-emphasis (Q-8)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_AUDIODEEMPHASIS_H
#define DEC_AUDIODEEMPHASIS_H

#include <cstdint>

#include "decoders.h"
#include "section.h"

// Q-8: applies 50/15 us de-emphasis to audio sections whose subcode CONTROL
// field flags pre-emphasis (IEC 60908 §17.5: "00X1: 2 audio channels with
// pre-emphasis 50/15 us"). Pre-emphasised discs boost the high frequencies at
// mastering; without the inverse network on decode the PCM plays back too
// bright. The filter is a first-order IIR (bilinear-transformed analogue
// de-emphasis network) run at the 44.1 kHz CD sample rate, so it must sit in
// the audio path before any resampling.
//
// The filter is applied in place, section by section, in absolute-time order.
// State is carried across consecutive pre-emphasised sections so the IIR runs
// continuously; a section without the flag passes through untouched and resets
// the state so a later pre-emphasised run (the flag may change per track)
// starts cleanly.
//
// Thread safety: not thread-safe; a single instance holds per-channel filter
// state and must be driven from one thread (the decode pipeline owns one
// instance per EfmProcessor).
class AudioDeemphasis : public Decoder {
 public:
  AudioDeemphasis();

  // De-emphasise the section in place when its metadata flags pre-emphasis.
  void applySection(AudioSection& section);

  void showStatistics() const override;

  // Accessors for the curated decode report.
  uint64_t deemphasisedSections() const { return m_deemphasisedSectionCount; }
  uint64_t passThroughSections() const { return m_passThroughSectionCount; }

 private:
  // Direct-form-I first-order IIR state (one previous input/output per
  // channel).
  struct BiquadState {
    double x1{0.0};
    double y1{0.0};
  };

  int16_t filterSample(BiquadState& state, int16_t sample) const;
  void resetState();

  BiquadState m_left;
  BiquadState m_right;

  // Tracks whether the previous section was pre-emphasised so the filter state
  // can be reset on the transition into a pre-emphasised run.
  bool m_previousSectionPreemphasised;

  // Statistics (64-bit: cumulative counters must not wrap on long captures).
  uint64_t m_deemphasisedSectionCount;
  uint64_t m_passThroughSectionCount;
};

#endif  // DEC_AUDIODEEMPHASIS_H
