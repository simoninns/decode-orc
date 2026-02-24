/*
 * File:        tvalues.h
 * Module:      EFM-library
 * Purpose:     T-values to bit string conversion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef TVALUES_H
#define TVALUES_H

#include <cstdint>
#include <string>
#include <vector>

class Tvalues
{
public:
    Tvalues();

    std::string tvaluesToBitString(const std::vector<uint8_t> &tvalues);

    uint32_t invalidHighTValuesCount() const { return m_invalidHighTValuesCount; }
    uint32_t invalidLowTValuesCount() const { return m_invalidLowTValuesCount; }
    uint32_t validTValuesCount() const { return m_validTValuesCount; }

private:
    uint32_t m_invalidHighTValuesCount;
    uint32_t m_invalidLowTValuesCount;
    uint32_t m_validTValuesCount;
};

#endif // TVALUES_H