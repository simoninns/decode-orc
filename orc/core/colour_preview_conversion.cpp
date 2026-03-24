/*
 * File:        colour_preview_conversion.cpp
 * Module:      orc-core
 * Purpose:     Render-boundary conversion from colour carriers to PreviewImage.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "include/colour_preview_conversion.h"

#include <algorithm>
#include <cmath>

namespace orc {
namespace {

struct MatrixCoefficients {
    double kr;
    double kb;
};

MatrixCoefficients coefficients_for(ColorimetricMatrixCoefficients matrix)
{
    switch (matrix) {
        case ColorimetricMatrixCoefficients::BT601_625:
            return {0.2990, 0.1140};
        case ColorimetricMatrixCoefficients::BT601_525:
            return {0.2990, 0.1140};
        case ColorimetricMatrixCoefficients::NTSC1953_FCC:
            return {0.3000, 0.1100};
        case ColorimetricMatrixCoefficients::Unspecified:
        default:
            return {0.2990, 0.1140};
    }
}

double decode_transfer_to_linear(double value, ColorimetricTransferCharacteristics transfer)
{
    value = std::clamp(value, 0.0, 1.0);

    switch (transfer) {
        case ColorimetricTransferCharacteristics::Gamma22:
            return std::pow(value, 2.2);
        case ColorimetricTransferCharacteristics::Gamma28:
            return std::pow(value, 2.8);
        case ColorimetricTransferCharacteristics::BT709:
            if (value < 0.081) {
                return value / 4.5;
            }
            return std::pow((value + 0.099) / 1.099, 1.0 / 0.45);
        case ColorimetricTransferCharacteristics::BT1886:
        case ColorimetricTransferCharacteristics::BT1886App1:
            return std::pow(value, 2.4);
        case ColorimetricTransferCharacteristics::Unspecified:
        default:
            return std::pow(value, 2.2);
    }
}

double encode_linear_to_srgb(double linear)
{
    linear = std::clamp(linear, 0.0, 1.0);

    if (linear <= 0.0031308) {
        return 12.92 * linear;
    }

    return 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
}

uint8_t to_u8(double value)
{
    value = std::clamp(value, 0.0, 1.0);
    const double scaled = value * 255.0 + 0.5;
    return static_cast<uint8_t>(scaled);
}

} // namespace

PreviewImage render_preview_from_colour_carrier(const ColourFrameCarrier& carrier)
{
    PreviewImage image{};

    if (!carrier.is_valid()) {
        return image;
    }

    image.width = carrier.width;
    image.height = carrier.height;
    image.rgb_data.resize(static_cast<size_t>(carrier.width) * static_cast<size_t>(carrier.height) * 3);

    const MatrixCoefficients matrix = coefficients_for(carrier.colorimetry.matrix_coefficients);
    const double kg = 1.0 - matrix.kr - matrix.kb;

    const double yuv_range = carrier.white_16b_ire - carrier.black_16b_ire;

    const size_t samples = static_cast<size_t>(carrier.width) * static_cast<size_t>(carrier.height);
    for (size_t i = 0; i < samples; ++i) {
        double y = (carrier.y_plane[i] - carrier.black_16b_ire) / yuv_range;
        double u = carrier.u_plane[i] / yuv_range;
        double v = carrier.v_plane[i] / yuv_range;

        double r_nl = y + (2.0 - 2.0 * matrix.kr) * v;
        double b_nl = y + (2.0 - 2.0 * matrix.kb) * u;
        double g_nl = y
            - ((2.0 * matrix.kb * (1.0 - matrix.kb)) / kg) * u
            - ((2.0 * matrix.kr * (1.0 - matrix.kr)) / kg) * v;

        r_nl = std::clamp(r_nl, 0.0, 1.0);
        g_nl = std::clamp(g_nl, 0.0, 1.0);
        b_nl = std::clamp(b_nl, 0.0, 1.0);

        const double r_linear = decode_transfer_to_linear(r_nl, carrier.colorimetry.transfer_characteristics);
        const double g_linear = decode_transfer_to_linear(g_nl, carrier.colorimetry.transfer_characteristics);
        const double b_linear = decode_transfer_to_linear(b_nl, carrier.colorimetry.transfer_characteristics);

        const double r_srgb = encode_linear_to_srgb(r_linear);
        const double g_srgb = encode_linear_to_srgb(g_linear);
        const double b_srgb = encode_linear_to_srgb(b_linear);

        const size_t pixel = i * 3;
        image.rgb_data[pixel + 0] = to_u8(r_srgb);
        image.rgb_data[pixel + 1] = to_u8(g_srgb);
        image.rgb_data[pixel + 2] = to_u8(b_srgb);
    }

    return image;
}

} // namespace orc
