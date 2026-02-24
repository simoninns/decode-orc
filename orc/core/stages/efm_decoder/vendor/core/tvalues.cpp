/*
 * File:        tvalues.cpp
 * Module:      EFM-library
 * Purpose:     T-values to bit string conversion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "tvalues.h"

Tvalues::Tvalues()
    : m_invalidHighTValuesCount(0),
      m_invalidLowTValuesCount(0),
      m_validTValuesCount(0)
{
}

std::string Tvalues::tvaluesToBitString(const std::vector<uint8_t> &tvalues)
{
    std::string bitString;

    // For every T-value in the input array reserve 11 bits in the output bit string
    // Note: This is just to increase speed
    bitString.reserve(tvalues.size() * 11);

    for (int32_t i = 0; i < tvalues.size(); ++i) {
        // Convert the T-value to a bit string

        // Range check
        int32_t tValue = static_cast<int32_t>(tvalues[i]);
        if (tValue > 11) {
            m_invalidHighTValuesCount++;
            tValue = 11;
        } else if (tValue < 3) {
            m_invalidLowTValuesCount++;
            tValue = 3;
        } else {
            m_validTValuesCount++;
        }

        // T3 = 100, T4 = 1000, ... , T11 = 10000000000
        bitString += '1';
        bitString += std::string(tValue - 1, '0');
    }

    return bitString;
}
