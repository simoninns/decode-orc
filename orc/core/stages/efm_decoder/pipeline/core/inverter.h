/*
 * File:        inverter.h
 * Module:      EFM-library
 * Purpose:     Parity inversion functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef INVERTER_H
#define INVERTER_H

#include <cstdint>
#include <vector>

class Inverter
{
public:
    Inverter();
    void invertParity(std::vector<uint8_t> &inputData);
};

#endif // INVERTER_H