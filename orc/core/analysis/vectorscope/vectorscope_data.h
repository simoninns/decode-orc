/*
 * File:        vectorscope_data.h
 * Module:      orc-core
 * Purpose:     Vectorscope data structures
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_ANALYSIS_VECTORSCOPE_DATA_H
#define ORC_CORE_ANALYSIS_VECTORSCOPE_DATA_H

#include <vector>
#include <cstdint>
#include "../../include/tbc_metadata.h"

namespace orc {

/**
 * @brief Single U/V sample point for vectorscope display
 */
struct UVSample {
    double u;         // U (Cb) component: -32768 to +32767 range
    double v;         // V (Cr) component: -32768 to +32767 range
    uint8_t field_id; // Field index (0 = first/odd, 1 = second/even)
    
    UVSample() : u(0), v(0), field_id(0) {}
    UVSample(double u_val, double v_val, uint8_t field = 0)
        : u(u_val), v(v_val), field_id(field) {}
};

/**
 * @brief Vectorscope data extracted from a decoded RGB field
 */
struct VectorscopeData {
    std::vector<UVSample> samples;  // All U/V samples from the field
    uint32_t width;                  // Field width
    uint32_t height;                 // Field height
    uint64_t field_number;           // Field number for identification
    // Video parameters for graticule/targets
    orc::VideoSystem system = orc::VideoSystem::Unknown;
    int32_t white_16b_ire = 0;
    int32_t black_16b_ire = 0;
    
    VectorscopeData() : width(0), height(0), field_number(0) {}
};

/**
 * @brief Convert RGB to U/V (YUV color space)
 * 
 * Uses standard ITU-R BT.601 conversion matrix
 * Input: 16-bit RGB (0-65535)
 * Output: U/V approximately in range -32768 to +32767 centered at 0
 */
inline UVSample rgb_to_uv(uint16_t r, uint16_t g, uint16_t b) {
    // Convert to double and normalize to 0-1 range
    double rd = r / 65535.0;
    double gd = g / 65535.0;
    double bd = b / 65535.0;
    
    // ITU-R BT.601 conversion (SD)
    // Y = 0.299*R + 0.587*G + 0.114*B
    // U = -0.147*R - 0.289*G + 0.436*B
    // V = 0.615*R - 0.515*G - 0.100*B
    
    double u = -0.147 * rd - 0.289 * gd + 0.436 * bd;
    double v = 0.615 * rd - 0.515 * gd - 0.100 * bd;

    // Scale to signed range centered at 0
    // Note: u,v are already centered around 0 in [-~0.6, ~0.6].
    // Multiply by 32768 to map roughly to signed 16-bit amplitude without offset.
    return UVSample{ u * 32768.0, v * 32768.0 };
}

} // namespace orc

#endif // ORC_CORE_ANALYSIS_VECTORSCOPE_DATA_H
