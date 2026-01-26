/*
 * File:        palcolour.h
 * Module:      orc-core
 * Purpose:     PAL colour decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2026 Simon Inns
 * SPDX-FileCopyrightText: 2019 Adam Sampson
 */

#ifndef PALCOLOUR_H
#define PALCOLOUR_H

#include <cstdint>
#include <vector>
#include <iostream>
#include <cmath>
#include <memory>

#include <orc_source_parameters.h>

#include "componentframe.h"
#include "decoder.h"
#include "sourcefield.h"
#include "transformpal.h"

class PalColour
{
public:
    PalColour();

    // Specify which filter to use to separate luma and chroma information.
    enum ChromaFilterMode {
        // PALColour's 2D FIR filter
        palColourFilter = 0,
        // 2D Transform PAL frequency-domain filter
        transform2DFilter,
        // 3D Transform PAL frequency-domain filter
        transform3DFilter,
		//mono decoder
		mono
    };

    struct Configuration {
        double chromaGain = 1.0;
        double chromaPhase = 0.0;
        double yNRLevel = 0.0;
        bool simplePAL = false;
        ChromaFilterMode chromaFilter = palColourFilter;
        double transformThreshold = 0.4;
        std::vector<double> transformThresholds;
        bool showFFTs = false;
        int32_t showPositionX = 200;
        int32_t showPositionY = 200;

        int32_t getThresholdsSize() const;
        int32_t getLookBehind() const;
        int32_t getLookAhead() const;
    };

    const Configuration &getConfiguration() const;
    void updateConfiguration(const ::orc::SourceParameters &videoParameters,
                             const Configuration &configuration);

    // Decode a sequence of fields into a sequence of interlaced frames
    void decodeFrames(const std::vector<SourceField> &inputFields, int32_t startIndex, int32_t endIndex,
                      std::vector<ComponentFrame> &outputFrames);

    // Maximum frame size, based on PAL
    static constexpr int32_t MAX_WIDTH = 1135;

private:
    // Information about a line we're decoding.
    struct LineInfo {
        explicit LineInfo(int32_t number);

        int32_t number;
        // detectBurst computes bp, bq = cos(t), sin(t), where t is the burst phase.
        // They're used to build a rotation matrix for the chroma signals in decodeLine.
        double bp, bq;
        double Vsw;
    };

    void buildLookUpTables();
    void decodeField(const SourceField &inputField, const double *chromaData, ComponentFrame &componentFrame);
    void decodeFieldYC(const SourceField &inputField, ComponentFrame &componentFrame);
    void detectBurst(LineInfo &line, const uint16_t *inputData);
    template <typename ChromaSample, bool PREFILTERED_CHROMA>
    void decodeLine(const SourceField &inputField, const ChromaSample *chromaData, const LineInfo &line,
                    ComponentFrame &componentFrame);
    void apply2DChromaFilter(const uint16_t *chromaData, const LineInfo &line,
                            const SourceField &inputField, int32_t fieldLine,
                            int32_t firstLine, int32_t lastLine,
                            double *outU, double *outV);
    void doYNR(double *Yline);

    // Configuration parameters
    bool configurationSet;
    Configuration configuration;
    ::orc::SourceParameters videoParameters;

    // Transform PAL filter
    std::unique_ptr<TransformPal> transformPal;

    // The subcarrier reference signal
    double sine[MAX_WIDTH], cosine[MAX_WIDTH];

    // Coefficients for the three 2D chroma low-pass filters. There are
    // separate filters for U and V, but only the signs differ, so they can
    // share a set of coefficients.
    //
    // The filters are horizontally and vertically symmetrical, so each 2D
    // array represents one quarter of a filter. The zeroth horizontal element
    // is included in the sum twice, so the coefficient is halved to
    // compensate. Each filter is (2 * FILTER_SIZE) + 1 elements wide.
    static constexpr int32_t FILTER_SIZE = 7;
    double cfilt[FILTER_SIZE + 1][4];
    double yfilt[FILTER_SIZE + 1][2];
};

#endif // PALCOLOUR_H
