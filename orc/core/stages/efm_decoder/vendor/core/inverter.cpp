/*
 * File:        inverter.cpp
 * Module:      EFM-library
 * Purpose:     Parity inversion functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "inverter.h"
#include "core/logging.h"
#include <cstdlib>

Inverter::Inverter() { }

// Invert the P and Q parity bytes in accordance with
// ECMA-130 issue 2 page 35/36
void Inverter::invertParity(std::vector<uint8_t> &inputData)
{
    if (inputData.size() != 32) {
        LOG_ERROR("Inverter::invertParity(): Data must be a std::vector of 32 integers.");
        std::exit(1);
    }

    for (int i = 12; i < 16; ++i) {
        inputData[i] = ~inputData[i] & 0xFF;
    }
    for (int i = 28; i < 32; ++i) {
        inputData[i] = ~inputData[i] & 0xFF;
    }
}