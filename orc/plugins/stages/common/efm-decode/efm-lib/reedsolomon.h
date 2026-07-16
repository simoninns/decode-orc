/*
 * File:        reedsolomon.h
 * Purpose:     EFM-library - Reed-Solomon CIRC functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef REEDSOLOMON_H
#define REEDSOLOMON_H

#include <cstdint>
#include <vector>

// ezpwd __RS() macro parameters are:
// NAME, TYPE, SYMBOLS, PAYLOAD, POLY, FCR, PRIM, DUAL
// (the ECMA-130 values below are POLY=0x11D, FCR=0, PRIM=1, DUAL=false -
// mislabelling FCR/PRIM as INIT/AGR would invite a future mis-edit of the
// safety-critical first-consecutive-root value.)

// To find the integer representation of the polynomial P(x)=x^8+x^4+x^3+x^2+1
// treat the coefficients as binary digits, where each coefficient corresponds
// to a power of x, starting from x^0 on the rightmost side. If there is no term
// for a specific power of x, its coefficient is 0.
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

class ReedSolomon {
 public:
  ReedSolomon();
  void c1Decode(std::vector<uint8_t>& inputData,
                std::vector<uint8_t>& errorData,
                std::vector<uint8_t>& paddedData);
  void c2Decode(std::vector<uint8_t>& inputData,
                std::vector<uint8_t>& errorData,
                std::vector<uint8_t>& paddedData);

  int32_t validC1s() const;
  int32_t fixedC1s() const;
  int32_t errorC1s() const;

  int32_t validC2s() const;
  int32_t fixedC2s() const;
  int32_t errorC2s() const;

 private:
  // P-6: reusable scratch buffers so the CIRC hot path (decode() is called
  // ~26M times per stereo disc) does not allocate per call. Each pipeline owns
  // its own ReedSolomon and calls c1/c2Decode sequentially, so plain members
  // are safe (the shared ezpwd codecs are const; this object is not shared).
  std::vector<uint8_t> m_scratchData;
  std::vector<int> m_erasures;
  std::vector<int> m_position;

  int32_t m_validC1s;
  int32_t m_fixedC1s;
  int32_t m_errorC1s;

  int32_t m_validC2s;
  int32_t m_fixedC2s;
  int32_t m_errorC2s;
};

#endif  // REEDSOLOMON_H