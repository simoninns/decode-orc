/*
 * File:        comb.h
 * Module:      orc-core
 * Purpose:     NN NTSC comb filter decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018 Chad Page
 * SPDX-FileCopyrightText: 2018-2019 Simon Inns
 * SPDX-FileCopyrightText: 2020-2021 Adam Sampson
 * SPDX-FileCopyrightText: 2021 Phillip Blucas
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#pragma once

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include "componentframe.h"
#include "decoder.h"
#include "nn_source_field.h"

namespace orc::nn_ntsc {

class Comb
{
public:
    Comb();

    // Comb filter configuration parameters
    struct Configuration {
        double  chromaGain        = 1.0;
        double  chromaPhase       = 0.0;
        int32_t dimensions        = 2;     // 2 = 2D (fallback), 3 = NN 3D
        bool    adaptive          = true;
        bool    showMap           = false;
        bool    phaseCompensation = false;

        double cNRLevel = 0.0;  // Chroma noise reduction
        double yNRLevel = 0.0;  // Luma noise reduction

        int32_t getLookBehind() const;
        int32_t getLookAhead()  const;
    };

    const Configuration &getConfiguration() const;
    void updateConfiguration(const NnVideoParameters &videoParameters,
                             const Configuration     &configuration);

    // Decode a window of fields into component frames.
    // inputFields covers [startIndex – lookBehind … endIndex + lookAhead].
    void decodeFrames(const std::vector<NnSourceField> &inputFields,
                      int32_t startIndex, int32_t endIndex,
                      std::vector<ComponentFrame>      &componentFrames);

    // Maximum frame dimensions (matches ld-decode field widths)
    static constexpr int32_t MAX_WIDTH  = 910;
    static constexpr int32_t MAX_HEIGHT = 525;  // NTSC frame height

private:
    bool          configurationSet = false;
    Configuration configuration;
    NnVideoParameters videoParameters;

    // -------------------------------------------------------------------------
    // Per-frame working buffer
    // -------------------------------------------------------------------------
    class FrameBuffer {
    public:
        FrameBuffer(const NnVideoParameters &videoParameters_,
                    const Configuration     &configuration_);

        void loadFields(const NnSourceField &firstField,
                        const NnSourceField &secondField);

        // Overlap-add accumulators (written by split3D, finalised by finalizeOLA)
        std::vector<std::vector<double>> accChroma;
        std::vector<std::vector<double>> weightSum;

        void split1D();
        void split2D();

        // 4-field overlap-add NN decode; frameIdx identifies the current frame
        // position in the stream for deterministic tile-sampling.
        void split3D(FrameBuffer &nextFrame, int frameIdx);

        void setComponentFrame(ComponentFrame &_componentFrame) {
            componentFrame = &_componentFrame;
        }

        void finalizeOLA();

        void splitIQ();
        void splitIQlocked();
        void filterIQ();
        void adjustY();
        void doCNR();
        void doYNR();
        void transformIQ(double chromaGain, double chromaPhase);
        void overlayMap(const FrameBuffer &previousFrame,
                        const FrameBuffer &nextFrame);

    private:
        const NnVideoParameters &videoParameters;
        const Configuration     &configuration;

        int32_t frameHeight = 0;
        double  irescale    = 0.0;

        std::vector<uint16_t> rawbuffer;

        int32_t firstFieldPhaseID  = 0;
        int32_t secondFieldPhaseID = 0;

        struct Sample {
            double pixel[MAX_HEIGHT][MAX_WIDTH];
        } clpbuffer[3];

        struct Candidate {
            double penalty = 0.0;
            double sample  = 0.0;
        };

        ComponentFrame *componentFrame = nullptr;

        int32_t getFieldID(int32_t lineNumber) const;
        bool    getLinePhase(int32_t lineNumber) const;

        void getBestCandidate(int32_t lineNumber, int32_t h,
                              const FrameBuffer &previousFrame,
                              const FrameBuffer &nextFrame,
                              int32_t &bestIndex, double &bestSample) const;

        Candidate getCandidate(int32_t refLineNumber, int32_t refH,
                               const FrameBuffer &frameBuffer,
                               int32_t lineNumber, int32_t h,
                               double adjustPenalty) const;
    };
};

}  // namespace orc::nn_ntsc
