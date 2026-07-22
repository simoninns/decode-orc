/*
 * File:        ntscdecoder.h
 * Module:      orc-core
 * Purpose:     NTSC decoder wrapper
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2019-2020 Adam Sampson
 */

#ifndef NTSCDECODER_H
#define NTSCDECODER_H

#include <orc/stage/orc_source_parameters.h>

#include <atomic>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "comb.h"
#include "componentframe.h"
#include "decoder.h"
#include "monodecoder.h"
#include "sourcefield.h"

// 2D/3D NTSC decoder using Comb
//
// Thread safety: not thread-safe.  Each worker thread must construct and own
// its own instance, as each holds mutable per-decode decoder state.
class NtscDecoder : public Decoder {
 public:
  NtscDecoder(const Comb::Configuration& combConfig);
  bool configure(const ::orc::SourceParameters& videoParameters) override;
  int32_t getLookBehind() const override;
  int32_t getLookAhead() const override;

  void decodeFrames(const std::vector<SourceField>& inputFields,
                    int32_t startIndex, int32_t endIndex,
                    std::vector<ComponentFrame>& componentFrames) override;

  // Parameters used by NtscDecoder and NtscThread
  struct Configuration : public Decoder::Configuration {
    Comb::Configuration combConfig;
  };

 private:
  // Decode separate Y/C input: mono decoder for luma, Comb for chroma, merged.
  // This is ld-chroma-decoder's separate Y/C output path, not the older
  // preview-only Comb::decodeFramesYC().
  void decodeFramesYc(const std::vector<SourceField>& inputFields,
                      int32_t startIndex, int32_t endIndex,
                      std::vector<ComponentFrame>& componentFrames);

  Configuration config;
  std::unique_ptr<Comb> comb;
  std::unique_ptr<MonoDecoder> ycLumaDecoder;
  bool configurationValid_ = false;
};

#endif  // NTSCDECODER_H
