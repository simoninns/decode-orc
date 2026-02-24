/*
 * File:        hex_utils.h
 * Module:      EFM-library
 * Purpose:     Utility functions for hex conversion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef HEX_UTILS_H
#define HEX_UTILS_H

#include <cstdint>
#include <vector>
#include <string>
#include <iomanip>
#include <sstream>
#include <cctype>
#include <algorithm>

namespace HexUtils {
    inline std::string vectorToHex(const std::vector<uint8_t> &data) {
        std::ostringstream oss;
        for (uint8_t byte : data) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return oss.str();
    }

    inline std::string formatTime(int32_t minutes, int32_t seconds, int32_t frames) {
        std::ostringstream oss;
        oss << std::setfill('0') 
            << std::setw(2) << minutes << ':'
            << std::setw(2) << seconds << ':'  
            << std::setw(2) << frames;
        return oss.str();
    }

    inline std::string trim(const std::string& str) {
        auto start = str.begin();
        while (start != str.end() && std::isspace(*start)) {
            ++start;
        }
        auto end = str.end();
        do {
            --end;
        } while (std::distance(start, end) > 0 && std::isspace(*end));
        
        if (start <= end) {
            return std::string(start, end + 1);
        }
        return "";
    }
}

#endif // HEX_UTILS_H
