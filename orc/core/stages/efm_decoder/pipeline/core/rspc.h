/*
 * File:        rspc.h
 * Module:      EFM-library
 * Purpose:     Reed-Solomon Product-like Code (RSPC) functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef RSPC_H
#define RSPC_H

#include <cstdint>
#include <vector>

class Rspc
{
public:
    Rspc();
    void qParityEcc(std::vector<uint8_t> &inputData, std::vector<uint8_t> &errorData);
    void pParityEcc(std::vector<uint8_t> &inputData, std::vector<uint8_t> &errorData);
};

#endif // RSPC_H