/*
 * File:        transformpal2d.h
 * Module:      orc-core
 * Purpose:     Transform PAL 2D decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 */


#ifndef TRANSFORMPAL2D_H
#define TRANSFORMPAL2D_H

#include <fftw3.h>

#include "componentframe.h"
#include "outputwriter.h"
#include "sourcefield.h"
#include "transformpal.h"

class TransformPal2D : public TransformPal {
public:
    TransformPal2D();
    virtual ~TransformPal2D();

    // Return the expected size of the thresholds array.
    static int32_t getThresholdsSize();

    void filterFields(const std::vector<SourceField> &inputFields, int32_t startIndex, int32_t endIndex,
                      std::vector<const double *> &outputFields) override;

protected:
    void filterField(const SourceField& inputField, int32_t outputIndex);
    void forwardFFTTile(int32_t tileX, int32_t tileY, int32_t startY, int32_t endY, const SourceField &inputField);
    void inverseFFTTile(int32_t tileX, int32_t tileY, int32_t startY, int32_t endY, int32_t outputIndex);
    void applyFilter();
    void overlayFFTFrame(int32_t positionX, int32_t positionY,
                         const std::vector<SourceField> &inputFields, int32_t fieldIndex,
                         ComponentFrame &componentFrame) override;

    // FFT input and output sizes.
    // The input field is divided into tiles of XTILE x YTILE, with adjacent
    // tiles overlapping by HALFXTILE/HALFYTILE.
    static constexpr int32_t YTILE = 16;
    static constexpr int32_t HALFYTILE = YTILE / 2;
    static constexpr int32_t XTILE = 32;
    static constexpr int32_t HALFXTILE = XTILE / 2;

    // Each tile is converted to the frequency domain using forwardPlan, which
    // gives a complex result of size XCOMPLEX x YCOMPLEX (roughly half the
    // size of the input, because the input data was real, i.e. contained no
    // negative frequencies).
    static constexpr int32_t YCOMPLEX = YTILE;
    static constexpr int32_t XCOMPLEX = (XTILE / 2) + 1;

    // Window function applied before the FFT
    double windowFunction[YTILE][XTILE];

    // FFT input/output buffers
    double *fftReal;
    fftw_complex *fftComplexIn;
    fftw_complex *fftComplexOut;

    // FFT plans
    fftw_plan forwardPlan, inversePlan;

    // The combined result of all the FFT processing for each input field.
    // Inverse-FFT results are accumulated into these buffers.
    std::vector<std::vector<double>> chromaBuf;
};

#endif
