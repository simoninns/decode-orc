/************************************************************************

    monodecoder.h

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2019-2021 Adam Sampson

    This file is part of ld-decode-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

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
		::orc::VideoParameters videoParameters;
	};
	MonoDecoder();
	MonoDecoder(const MonoDecoder::MonoConfiguration &config);
	bool updateConfiguration(const ::orc::VideoParameters &videoParameters, const MonoDecoder::MonoConfiguration &configuration);
	bool configure(const ::orc::VideoParameters &videoParameters) override;

	/// Decode luma-only frames (no filtering)
	void decodeFrames(const std::vector<SourceField>& inputFields,
                    int32_t startIndex,
                    int32_t endIndex,
                    std::vector<ComponentFrame>& componentFrames) override;
	void doYNR(ComponentFrame &componentFrame);				

private:
    MonoConfiguration monoConfig;
};

#endif // MONODECODER
