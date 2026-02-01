/*
 * File:        luma_filter.h
 * Module:      orc-core
 * Purpose:     Simple FIR filter for luma/chroma separation in dropout correction
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <vector>
#include <cstdint>
#include <algorithm>
#include <cmath>

namespace orc {

/// Simple FIR filter for extracting low-frequency (luma) component
/// Used to separate luma and chroma during dropout correction
class LumaFirFilter {
public:
    /// Create a PAL luma filter
    /// Low-pass filter to extract frequencies below ~5.5 MHz
    static LumaFirFilter create_pal_filter() {
        // Simple 9-tap low-pass filter for PAL
        // Designed to pass luma (~5.5 MHz) and attenuate chroma (~4.43 MHz subcarrier)
        std::vector<double> coeffs = {
            0.0118, 0.0618, 0.1618, 0.2618, 0.3218,
            0.2618, 0.1618, 0.0618, 0.0118
        };
        // Normalize to sum to 1.0
        double sum = 0.0;
        for (double c : coeffs) sum += c;
        for (double& c : coeffs) c /= sum;
        
        return LumaFirFilter(coeffs);
    }
    
    /// Create an NTSC luma filter
    /// Low-pass filter to extract frequencies below ~3.6 MHz
    static LumaFirFilter create_ntsc_filter() {
        // Simple 9-tap low-pass filter for NTSC
        // Designed to pass luma (~3.6 MHz) and attenuate chroma (~3.58 MHz subcarrier)
        std::vector<double> coeffs = {
            0.0085, 0.0515, 0.1515, 0.2515, 0.3115,
            0.2515, 0.1515, 0.0515, 0.0085
        };
        // Normalize to sum to 1.0
        double sum = 0.0;
        for (double c : coeffs) sum += c;
        for (double& c : coeffs) c /= sum;
        
        return LumaFirFilter(coeffs);
    }
    
    /// Create a PAL-M luma filter
    static LumaFirFilter create_pal_m_filter() {
        // PAL-M uses similar characteristics to NTSC
        return create_ntsc_filter();
    }
    
    /// Apply filter to a line of data
    /// Input/output buffer must be same size
    void apply(const uint16_t* input, uint16_t* output, size_t width) const {
        if (width == 0 || coeffs_.empty()) return;
        
        const int32_t delay = static_cast<int32_t>(coeffs_.size()) / 2;
        
        // Process each sample
        for (size_t i = 0; i < width; ++i) {
            double sum = 0.0;
            
            // Apply convolution with boundary handling
            for (int32_t j = 0; j < static_cast<int32_t>(coeffs_.size()); ++j) {
                int32_t idx = static_cast<int32_t>(i) + j - delay;
                
                // Boundary handling: clamp to edges
                if (idx < 0) idx = 0;
                if (idx >= static_cast<int32_t>(width)) idx = width - 1;
                
                sum += coeffs_[j] * input[idx];
            }
            
            // Clamp to valid range and convert back to uint16_t
            sum = std::max(0.0, std::min(65535.0, sum));
            output[i] = static_cast<uint16_t>(sum);
        }
    }
    
private:
    explicit LumaFirFilter(const std::vector<double>& coeffs) 
        : coeffs_(coeffs) {}
    
    std::vector<double> coeffs_;
};

} // namespace orc
