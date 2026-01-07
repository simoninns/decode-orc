/*
 * File:        monodecoder.h
 * Module:      orc-core
 * Purpose:     Monochrome decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2019-2021 Adam Sampson
 */


#ifndef MONODECODER_H
#define MONODECODER_H

#include <atomic>
#include <thread>
#include <iostream>

#include "componentframe.h"
#include "tbc_metadata.h"

#include "comb.h"
#include "decoder.h"
#include "sourcefield.h"

// Decoder that passes all input through as luma, for purely monochrome sources
class MonoDecoder : public Decoder {
public:

	struct MonoConfiguration {
		double yNRLevel = 0.0;
		bool filterChroma = false;  // If true, use comb filter to remove chroma subcarrier (ld-chroma-decoder -b mode)
		::orc::VideoParameters videoParameters;
	};
	MonoDecoder();
	MonoDecoder(const MonoDecoder::MonoConfiguration &config);
	bool updateConfiguration(const ::orc::VideoParameters &videoParameters, const MonoDecoder::MonoConfiguration &configuration);
	bool configure(const ::orc::VideoParameters &videoParameters) override;

	/// Decode luma-only frames (optionally filtering out chroma)
	void decodeFrames(const std::vector<SourceField>& inputFields,
                    int32_t startIndex,
                    int32_t endIndex,
                    std::vector<ComponentFrame>& componentFrames) override;
	void doYNR(ComponentFrame &componentFrame);				

private:
    MonoConfiguration monoConfig;
    std::unique_ptr<Comb> combFilter;  // Used when filterChroma is true
};

#endif // MONODECODER
