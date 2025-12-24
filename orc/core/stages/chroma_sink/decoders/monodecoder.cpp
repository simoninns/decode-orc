/*
 * File:        monodecoder.cpp
 * Module:      orc-core
 * Purpose:     Monochrome decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 * SPDX-FileCopyrightText: 2019-2021 Adam Sampson
 */


#include "monodecoder.h"

#include "comb.h"
#include "palcolour.h"

#include "deemp.h"
#include "firfilter.h"

MonoDecoder::MonoDecoder()
{	
}

MonoDecoder::MonoDecoder(const MonoDecoder::MonoConfiguration &config)
{
    monoConfig = config;
}

bool MonoDecoder::updateConfiguration(const ::orc::VideoParameters &videoParameters, const MonoDecoder::MonoConfiguration &configuration) {
    // This decoder works for both PAL and NTSC.
	monoConfig.yNRLevel = configuration.yNRLevel;
    monoConfig.videoParameters = videoParameters;

    return true;
}

bool MonoDecoder::configure(const ::orc::VideoParameters &videoParameters) {
    // This decoder works for both PAL and NTSC.

    monoConfig.videoParameters = videoParameters;

    return true;
}

void MonoDecoder::decodeFrames(const std::vector<SourceField>& inputFields,
                               int32_t startIndex,
                               int32_t endIndex,
                               std::vector<ComponentFrame>& componentFrames)
{
	const ::orc::VideoParameters &videoParameters = monoConfig.videoParameters;
	bool ignoreUV = false;
	
	
	for (int32_t fieldIndex = startIndex, frameIndex = 0; fieldIndex < endIndex; fieldIndex += 2, frameIndex++) {
		componentFrames[frameIndex].init(videoParameters, ignoreUV);
		for (int32_t y = videoParameters.first_active_frame_line; y < videoParameters.last_active_frame_line; y++) {
			const std::vector<uint16_t> &inputFieldData = (y % 2) == 0 ? inputFields[fieldIndex].data : inputFields[fieldIndex+1].data;
			const uint16_t *inputLine = inputFieldData.data() + ((y / 2) * videoParameters.field_width);

			// Copy the whole composite signal to Y (leaving U and V blank)
			double *outY = componentFrames[frameIndex].y(y);
			for (int32_t x = videoParameters.active_video_start; x < videoParameters.active_video_end; x++) {
				outY[x] = inputLine[x];
			}
		}
		doYNR(componentFrames[frameIndex]);
    }
}

void MonoDecoder::doYNR(ComponentFrame &componentFrame) {
    if (monoConfig.yNRLevel == 0.0)
        return;

    // 1. Compute coring level (same formula in both existing routines)
    double irescale = (monoConfig.videoParameters.white_16b_ire
                     - monoConfig.videoParameters.black_16b_ire) / 100.0;
    double nr_y     = monoConfig.yNRLevel * irescale;

    // 2. Choose filter taps & descriptor based on system
    bool usePal = (monoConfig.videoParameters.system == orc::VideoSystem::PAL || monoConfig.videoParameters.system == orc::VideoSystem::PAL_M);
    const auto& taps       = usePal ? c_nrpal_b
                                    : c_nr_b;
    const auto& descriptor = usePal ? f_nrpal
                                    : f_nr;

    const int delay = static_cast<int>(taps.size()) / 2;

    // 3. Process each active scanline in the frame
    for (int line = monoConfig.videoParameters.first_active_frame_line;
             line < monoConfig.videoParameters.last_active_frame_line;
           ++line)
    {
        double* Y = componentFrame.y(line);

        // 4. High‑pass buffer & FIR filter
        std::vector<double> hpY(monoConfig.videoParameters.active_video_end + delay);
        auto yFilter(descriptor);  // uses the chosen taps internally

        // Flush zeros before active start
        for (int x = monoConfig.videoParameters.active_video_start - delay;
                 x < monoConfig.videoParameters.active_video_start;
               ++x)
        {
            yFilter.feed(0.0);
        }
        // Filter active region
        for (int x = monoConfig.videoParameters.active_video_start;
                 x < monoConfig.videoParameters.active_video_end;
               ++x)
        {
            hpY[x] = yFilter.feed(Y[x]);
        }
        // Flush zeros after active end
        for (int x = monoConfig.videoParameters.active_video_end;
                 x < monoConfig.videoParameters.active_video_end + delay;
               ++x)
        {
            yFilter.feed(0.0);
        }

        // 5. Clamp & subtract
        for (int x = monoConfig.videoParameters.active_video_start;
                 x < monoConfig.videoParameters.active_video_end;
               ++x)
        {
            double a = hpY[x + delay];
            if (std::fabs(a) > nr_y)
                a = (a > 0.0) ? nr_y : -nr_y;
            Y[x] -= a;
        }
    }
}


