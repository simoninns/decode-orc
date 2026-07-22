/*
 * File:        paldecoder.h
 * Module:      orc-core
 * Purpose:     PAL decoder wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2019-2020 Adam Sampson
 */

#ifndef PALDECODER_H
#define PALDECODER_H

#include <orc/stage/orc_source_parameters.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "componentframe.h"
#include "decoder.h"
#include "monodecoder.h"
#include "palcolour.h"
#include "sourcefield.h"

// 2D PAL decoder using PALcolour
//
// Thread safety: not thread-safe.  Each worker thread must construct and own
// its own instance, as each holds mutable per-decode decoder state.  Transform
// PAL builds FFTW plans with FFTW_MEASURE during configure(), which is not
// thread-safe, so concurrent configure() calls must be serialised by the
// caller.
class PalDecoder : public Decoder {
 public:
  PalDecoder(const PalColour::Configuration& palConfig);
  bool configure(const ::orc::SourceParameters& videoParameters) override;
  int32_t getLookBehind() const override;
  int32_t getLookAhead() const override;

  void decodeFrames(const std::vector<SourceField>& inputFields,
                    int32_t startIndex, int32_t endIndex,
                    std::vector<ComponentFrame>& componentFrames) override;

  // Parameters used by PalDecoder and PalThread
  struct Configuration : public Decoder::Configuration {
    PalColour::Configuration pal;
  };

 private:
  // Decode separate Y/C input: mono decoder for luma, PALcolour for chroma,
  // merged.  This is ld-chroma-decoder's separate Y/C output path, not the
  // older preview-only PalColour::decodeFieldYC().  The Transform PAL modes
  // are meaningless on already-separated chroma, so the caller must select
  // palColourFilter for Y/C sources.
  void decodeFramesYc(const std::vector<SourceField>& inputFields,
                      int32_t startIndex, int32_t endIndex,
                      std::vector<ComponentFrame>& componentFrames);

  Configuration config;
  std::unique_ptr<PalColour> palColour;
  std::unique_ptr<MonoDecoder> ycLumaDecoder;
  bool configurationValid_ = false;
};

#endif  // PALDECODER
