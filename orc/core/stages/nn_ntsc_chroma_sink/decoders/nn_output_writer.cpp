/*
 * File:        nn_output_writer.cpp
 * Module:      orc-core
 * Purpose:     Output format writer for NN NTSC chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2021 Chad Page
 * SPDX-FileCopyrightText: 2020-2021 Adam Sampson
 * SPDX-FileCopyrightText: 2021 Phillip Blucas
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#include "nn_output_writer.h"
#include "componentframe.h"
#include "logging.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <sstream>
#include <string>

namespace orc::nn_ntsc {

// ============================================================================
// Y'CbCr scaling constants [Poynton ch25 p305] [BT.601-7 sec 2.5.3]
// ============================================================================
static constexpr double Y_MIN   = 1.0    * 256.0;
static constexpr double Y_ZERO  = 16.0   * 256.0;
static constexpr double Y_SCALE = 219.0  * 256.0;
static constexpr double Y_MAX   = 254.75 * 256.0;
static constexpr double C_MIN   = 1.0    * 256.0;
static constexpr double C_ZERO  = 128.0  * 256.0;
static constexpr double C_SCALE = 112.0  * 256.0;
static constexpr double C_MAX   = 254.75 * 256.0;

// ITU-R BT.601-7 [Poynton eq 25.1 p303 and eq 25.5 p307]
static constexpr double ONE_MINUS_Kb = 1.0 - 0.114;
static constexpr double ONE_MINUS_Kr = 1.0 - 0.299;

// kB = sqrt(209556997.0 / 96146491.0) / 3.0
// kR = sqrt(221990474.0 / 288439473.0)  [Poynton eq 28.1 p336]
static constexpr double kB = 0.49211104112248356308804691718185;
static constexpr double kR = 0.87728321993817866838972487283129;

// ============================================================================

void NnOutputWriter::updateConfiguration(NnVideoParameters      &_videoParameters,
                                          const Configuration    &_config)
{
    config         = _config;
    videoParameters = _videoParameters;
    topPadLines    = 0;
    bottomPadLines = 0;

    activeWidth  = videoParameters.activeVideoEnd  - videoParameters.activeVideoStart;
    activeHeight = videoParameters.lastActiveFrameLine - videoParameters.firstActiveFrameLine;
    outputHeight = activeHeight;

    if (config.paddingAmount > 1) {
        // Expand horizontal active region so width is divisible by paddingAmount.
        while ((activeWidth % config.paddingAmount) != 0) {
            activeWidth = videoParameters.activeVideoEnd -
                          videoParameters.activeVideoStart;
            if ((activeWidth % config.paddingAmount) == 0) break;
            if ((activeWidth % 2) == 0)
                videoParameters.activeVideoEnd++;
            else
                videoParameters.activeVideoStart--;
        }
        activeWidth = videoParameters.activeVideoEnd -
                      videoParameters.activeVideoStart;

        // Insert blank lines so the height is divisible by paddingAmount.
        while (true) {
            outputHeight = topPadLines + activeHeight + bottomPadLines;
            if ((outputHeight % config.paddingAmount) == 0) break;
            if ((outputHeight % 2) == 0) bottomPadLines++;
            else                         topPadLines++;
        }

        _videoParameters = videoParameters;
    }
}

const char *NnOutputWriter::getPixelName() const
{
    switch (config.pixelFormat) {
    case RGB48:     return "RGB48";
    case YUV444P16: return "YUV444P16";
    case GRAY16:    return "GRAY16";
    default:        return "unknown";
    }
}

void NnOutputWriter::printOutputInfo() const
{
    const int32_t frameHeight = (videoParameters.fieldHeight * 2) - 1;
    ORC_LOG_INFO("NnOutputWriter: Input {}x{} → {}x{} {}",
                 videoParameters.fieldWidth, frameHeight,
                 activeWidth, outputHeight, getPixelName());
}

std::string NnOutputWriter::getStreamHeader() const
{
    if (!config.outputY4m) return {};

    std::ostringstream hdr;
    hdr << "YUV4MPEG2";
    hdr << " W" << activeWidth;
    hdr << " H" << outputHeight;

    // NTSC frame rate
    hdr << " F30000:1001";

    // Field order: top-first or bottom-first
    if ((videoParameters.firstActiveFrameLine % 2) ^
        (topPadLines % 2))
        hdr << " Ib";  // bottom-field-first
    else
        hdr << " It";  // top-field-first

    // Pixel aspect ratio (NTSC, follows SMPTE RP 187 / 4fSC sampling)
    if (videoParameters.isWidescreen)
        hdr << " A25:22";   // (16/9) * (480 / (708 × 4fSC / 13.5))
    else
        hdr << " A352:413"; // (4/3)  * (480 / (708 × 4fSC / 13.5))

    switch (config.pixelFormat) {
    case YUV444P16:
        hdr << " C444p16 XCOLORRANGE=LIMITED";
        break;
    case GRAY16:
        hdr << " Cmono16 XCOLORRANGE=LIMITED";
        break;
    default:
        // RGB48 is not representable in yuv4mpeg; caller should prevent this.
        ORC_LOG_ERROR("NnOutputWriter: RGB48 pixel format is not supported in Y4M");
        assert(false);
        break;
    }

    hdr << "\n";
    return hdr.str();
}

std::string NnOutputWriter::getFrameHeader() const
{
    if (!config.outputY4m) return {};
    return "FRAME\n";
}

void NnOutputWriter::convert(const ComponentFrame &componentFrame,
                              NnOutputFrame        &outputFrame) const
{
    int32_t totalSize = activeWidth * outputHeight;
    switch (config.pixelFormat) {
    case RGB48:
    case YUV444P16:
        totalSize *= 3;
        break;
    case GRAY16:
        break;
    }
    outputFrame.resize(totalSize);

    clearPadLines(0, topPadLines, outputFrame);
    clearPadLines(outputHeight - bottomPadLines, bottomPadLines, outputFrame);

    for (int32_t y = 0; y < activeHeight; y++)
        convertLine(y, componentFrame, outputFrame);
}

void NnOutputWriter::clearPadLines(int32_t firstLine, int32_t numLines,
                                    NnOutputFrame &outputFrame) const
{
    switch (config.pixelFormat) {
    case RGB48: {
        uint16_t *out = outputFrame.data() + activeWidth * firstLine * 3;
        std::fill(out, out + numLines * activeWidth * 3, 0);
        break;
    }
    case YUV444P16: {
        uint16_t *outY  = outputFrame.data() + activeWidth * firstLine;
        uint16_t *outCB = outY + activeWidth * outputHeight;
        uint16_t *outCR = outCB + activeWidth * outputHeight;
        for (int32_t i = 0; i < numLines * activeWidth; i++) {
            outY[i]  = static_cast<uint16_t>(Y_ZERO);
            outCB[i] = static_cast<uint16_t>(C_ZERO);
            outCR[i] = static_cast<uint16_t>(C_ZERO);
        }
        break;
    }
    case GRAY16: {
        uint16_t *out = outputFrame.data() + activeWidth * firstLine;
        for (int32_t i = 0; i < numLines * activeWidth; i++)
            out[i] = static_cast<uint16_t>(Y_ZERO);
        break;
    }
    }
}

void NnOutputWriter::convertLine(int32_t lineNumber,
                                  const ComponentFrame &componentFrame,
                                  NnOutputFrame        &outputFrame) const
{
    const int32_t inputLine =
        videoParameters.firstActiveFrameLine + lineNumber;
    const double *inY =
        componentFrame.y(inputLine) + videoParameters.activeVideoStart;
    const double *inU = (config.pixelFormat != GRAY16)
        ? (componentFrame.u(inputLine) + videoParameters.activeVideoStart)
        : nullptr;
    const double *inV = (config.pixelFormat != GRAY16)
        ? (componentFrame.v(inputLine) + videoParameters.activeVideoStart)
        : nullptr;

    const int32_t outputLine = topPadLines + lineNumber;

    const double yOffset = videoParameters.black16bIre;
    const double yRange  = videoParameters.white16bIre - videoParameters.black16bIre;
    const double uvRange = yRange;

    switch (config.pixelFormat) {
    case RGB48: {
        // Convert Y'UV to full-range R'G'B' [Poynton eq 28.6 p337]
        uint16_t *out = outputFrame.data() + activeWidth * outputLine * 3;
        const double yScale  = 65535.0 / yRange;
        const double uvScale = 65535.0 / uvRange;

        for (int32_t x = 0; x < activeWidth; x++) {
            const double rY = std::clamp((inY[x] - yOffset) * yScale, 0.0, 65535.0);
            const double rU = inU[x] * uvScale;
            const double rV = inV[x] * uvScale;
            const int32_t pos = x * 3;
            out[pos]     = static_cast<uint16_t>(
                std::clamp(rY + (1.139883 * rV), 0.0, 65535.0));
            out[pos + 1] = static_cast<uint16_t>(
                std::clamp(rY + (-0.394642 * rU) + (-0.580622 * rV), 0.0, 65535.0));
            out[pos + 2] = static_cast<uint16_t>(
                std::clamp(rY + (2.032062 * rU), 0.0, 65535.0));
        }
        break;
    }
    case YUV444P16: {
        // Convert Y'UV to Y'CbCr [Poynton eq 25.5 p307]
        uint16_t *outY  = outputFrame.data() + activeWidth * outputLine;
        uint16_t *outCB = outY  + activeWidth * outputHeight;
        uint16_t *outCR = outCB + activeWidth * outputHeight;
        const double yScale  = Y_SCALE / yRange;
        const double cbScale = (C_SCALE / (ONE_MINUS_Kb * kB)) / uvRange;
        const double crScale = (C_SCALE / (ONE_MINUS_Kr * kR)) / uvRange;

        for (int32_t x = 0; x < activeWidth; x++) {
            outY[x]  = static_cast<uint16_t>(std::clamp(
                (inY[x] - yOffset) * yScale + Y_ZERO, Y_MIN, Y_MAX));
            outCB[x] = static_cast<uint16_t>(std::clamp(
                inU[x] * cbScale + C_ZERO, C_MIN, C_MAX));
            outCR[x] = static_cast<uint16_t>(std::clamp(
                inV[x] * crScale + C_ZERO, C_MIN, C_MAX));
        }
        break;
    }
    case GRAY16: {
        // Discard UV; convert Y' to the same scale as Y'CbCr.
        uint16_t *out = outputFrame.data() + activeWidth * outputLine;
        const double yScale = Y_SCALE / yRange;
        for (int32_t x = 0; x < activeWidth; x++)
            out[x] = static_cast<uint16_t>(std::clamp(
                (inY[x] - yOffset) * yScale + Y_ZERO, Y_MIN, Y_MAX));
        break;
    }
    }
}

}  // namespace orc::nn_ntsc
