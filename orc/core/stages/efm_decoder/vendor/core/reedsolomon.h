/*
 * File:        reedsolomon.h
 * Module:      EFM-library
 * Purpose:     Reed-Solomon CIRC functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef REEDSOLOMON_H
#define REEDSOLOMON_H

#include <cstdint>
#include <vector>

// ezpwd configuration is:
// NAME, TYPE, SYMBOLS, PAYLOAD, POLY, INIT, FCR, AGR

// To find the integer representation of the polynomial P(x)=x^8+x^4+x^3+x^2+1
// treat the coefficients as binary digits, where each coefficient corresponds to a power of x,
// starting from x^0 on the rightmost side. If there is no term for a specific power of x, its
// coefficient is 0.
//
// Steps:
//     Write the polynomial in terms of its binary representation:
//     P(x)=x^8+x^4+x^3+x^2+1
//
//     The coefficients from x^8 down to x^0 are: 1,0,0,0,1,1,1,0,1.
//
//     Form the binary number from the coefficients:
//     Binary representation: 100011101

//     Convert the binary number to its decimal (integer) equivalent:
//     0b100011101 = 0x11D = 285

class ReedSolomon
{
public:
    ReedSolomon();
    void c1Decode(std::vector<uint8_t> &inputData, std::vector<bool> &errorData,
        std::vector<bool> &paddedData);
    void c2Decode(std::vector<uint8_t> &inputData, std::vector<bool> &errorData,
        std::vector<bool> &paddedData);

    int32_t validC1s() const;
    int32_t fixedC1s() const;
    int32_t errorC1s() const;

    int32_t validC2s() const;
    int32_t fixedC2s() const;
    int32_t errorC2s() const;

private:
    int32_t m_validC1s;
    int32_t m_fixedC1s;
    int32_t m_errorC1s;

    int32_t m_validC2s;
    int32_t m_fixedC2s;
    int32_t m_errorC2s;
};

#endif // REEDSOLOMON_H