/*
 * File:        efm.h
 * Module:      EFM-library
 * Purpose:     EFM conversion functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef EFM_H
#define EFM_H

#include <cstdint>
#include <unordered_map>
#include <string>

class Efm
{
public:
    Efm() noexcept;
    ~Efm() = default;

    // Delete copy operations to prevent accidental copies
    Efm(const Efm&) = delete;
    Efm& operator=(const Efm&) = delete;

    // Make move operations default
    Efm(Efm&&) = default;
    Efm& operator=(Efm&&) = default;

    // Convert methods made const as they don't modify state
    uint16_t fourteenToEight(uint16_t efm) const noexcept;
    std::string eightToFourteen(uint16_t value) const;

private:
    static constexpr size_t EFM_LUT_SIZE = 258; // 256 + 2 sync symbols
    static constexpr uint16_t INVALID_EFM = 300;
    std::unordered_map<uint16_t, uint16_t> m_efmHash;
};

#endif // EFM_H