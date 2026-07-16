/*
 * File:        rspc.cpp
 * Purpose:     EFM-library - Reed-Solomon Product-like Code (RSPC) functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "rspc.h"

#include <orc/stage/logging.h>

#include <cstdlib>

#include "ezpwd_compat.h"

// ECMA-130 RSPC configuration for Reed-Solomon forward error correction. The Q
// code is RS(45,43) and the P code is RS(26,24); both carry 2 parity symbols
// over GF(2^8) with POLY=0x11D, FCR=0, PRIM=1, so a single RS(255,253) codec
// (255 - 2 = 253 payload) decodes both as shortened code words. (P-12: the
// former byte-identical QRS/PRS pair collapsed to one type.)
template <size_t SYMBOLS, size_t PAYLOAD>
struct RspcRS;
template <size_t PAYLOAD>
struct RspcRS<255, PAYLOAD>
    : public __RS(RspcRS, uint8_t, 255, PAYLOAD, 0x11d, 0, 1, false);

// P-6/P-12: the RS corrector carries precomputed Galois-field tables. Building
// them per call was a measurable cost (P and Q run 86 + 52 times per corrupt
// sector, more with the iterative RSPC), so it is constructed once at file
// scope and reused. ezpwd's decode() is const (reads the tables, works in local
// scratch), so a single shared instance is safe for concurrent decode() calls.
RspcRS<255, 255 - 2> rspcRs;

Rspc::Rspc() {}

void Rspc::qParityEcc(std::vector<uint8_t>& inputData,
                      std::vector<uint8_t>& errorData) {
  // Keep track of the number of successful corrections
  int32_t successfulCorrections = 0;

  // RS code is Q(45,43)
  // There are 104 bytes of Q-Parity (52 code words)
  // Each Q field covers 12 to 2248 = 2236 bytes (2 * 1118)
  // 2236 / 43 = 52 Q-parity words (= 104 Q-parity bytes)
  //
  // Calculations are based on ECMA-130 Annex A

  uint8_t* uF1Data = reinterpret_cast<uint8_t*>(inputData.data());
  uint8_t* uF1Erasures = reinterpret_cast<uint8_t*>(errorData.data());

  // Ignore the 12 sync bytes
  uF1Data += 12;
  uF1Erasures += 12;

  // Store the data and erasures in the form expected by the ezpwd library
  std::vector<uint8_t> qField;
  std::vector<int> qFieldErasures;
  qField.resize(45);  // 43 + 2 parity bytes = 45

  // evenOdd = 0 = LSBs / evenOdd = 1 = MSBs
  for (int32_t evenOdd = 0; evenOdd < 2; evenOdd++) {
    for (int32_t Nq = 0; Nq < 26; Nq++) {
      qFieldErasures.clear();
      for (int32_t Mq = 0; Mq < 43; Mq++) {
        // Get 43 byte codeword location
        int32_t Vq = 2 * ((44 * Mq + 43 * Nq) % 1118) + evenOdd;
        qField[static_cast<size_t>(Mq)] = uF1Data[Vq];

        // Get codeword erasures if present
        if (uF1Erasures[Vq] == 1) qFieldErasures.push_back(Mq);
      }
      // Get 2 byte parity location
      int32_t qParityByte0 = 2 * ((43 * 26 + Nq) % 1118) + evenOdd;
      int32_t qParityByte1 = 2 * ((44 * 26 + Nq) % 1118) + evenOdd;

      // Note: Q-Parity data starts at 12 + 2236
      qField[43] = uF1Data[qParityByte0 + 2236];
      qField[44] = uF1Data[qParityByte1 + 2236];

      // E-4(d): a CIRC-flagged corrupt Q-parity byte must be registered as an
      // erasure too, otherwise it is trusted and wastes correction margin.
      if (uF1Erasures[qParityByte0 + 2236] == 1) qFieldErasures.push_back(43);
      if (uF1Erasures[qParityByte1 + 2236] == 1) qFieldErasures.push_back(44);

      // E-4(c): the Q code is RS(45,43) - distance 3, so it can correct at most
      // 2 erasures (or 1 error). With more than 2 known erasures a blind
      // error-only decode is highly miscorrection-prone, so skip the codeword
      // and leave its flags set for the P pass / next iteration.
      if (qFieldErasures.size() > 2) continue;

      std::vector<int> position;
      int fixed = rspcRs.decode(qField, qFieldErasures, &position);

      if (fixed >= 0) {
        // Correction succeeded (the sector EDC is the final arbiter, so a rare
        // miscorrection is caught downstream). Copy the corrected data AND
        // parity back and clear the corrected positions' erasure flags so the
        // P pass and subsequent iterations see them as reliable (E-4(a)).
        successfulCorrections++;
        // E-8(e): distinguish an already-valid codeword (fixed == 0) from one
        // that actually had symbols repaired.
        if (fixed == 0) {
          m_qCleanCodewords++;
        } else {
          m_qCorrectedCodewords++;
        }
        for (int32_t Mq = 0; Mq < 43; Mq++) {
          int32_t Vq = 2 * ((44 * Mq + 43 * Nq) % 1118) + evenOdd;
          uF1Data[Vq] = qField[static_cast<size_t>(Mq)];
          uF1Erasures[Vq] = 0;
        }
        uF1Data[qParityByte0 + 2236] = qField[43];
        uF1Data[qParityByte1 + 2236] = qField[44];
        uF1Erasures[qParityByte0 + 2236] = 0;
        uF1Erasures[qParityByte1 + 2236] = 0;
      } else {
        // E-4(b): a failed codeword's data symbols are unreliable - flag them
        // as erasures so the P pass treats them as such (product-code
        // iteration).
        for (int32_t Mq = 0; Mq < 43; Mq++) {
          int32_t Vq = 2 * ((44 * Mq + 43 * Nq) % 1118) + evenOdd;
          uF1Erasures[Vq] = 1;
        }
      }
    }
  }

  // Reset the pointers
  uF1Data -= 12;
  uF1Erasures -= 12;

  // Show Q-Parity correction result to debug
  if (successfulCorrections >= 52) {
    // Q-Parity correction successful
  } else {
    ORC_LOG_DEBUG(
        "Rspc::qParityEcc(): Q-Parity correction failed! Got {} correct out of "
        "52 possible codewords",
        successfulCorrections);
  }
}

void Rspc::pParityEcc(std::vector<uint8_t>& inputData,
                      std::vector<uint8_t>& errorData) {
  // Uses the shared file-scope `rspcRs` codec (see note at file scope).
  // Keep track of the number of successful corrections
  int32_t successfulCorrections = 0;

  uint8_t* uF1Data = reinterpret_cast<uint8_t*>(inputData.data());
  uint8_t* uF1Erasures = reinterpret_cast<uint8_t*>(errorData.data());

  // RS code is P(26,24)
  // There are 172 bytes of P-Parity (86 code words)
  // Each P field covers 12 to 2076 = 2064 bytes (2 * 1032)
  // 2064 / 24 = 86 P-parity words (= 172 P-parity bytes)
  //
  // Calculations are based on ECMA-130 Annex A

  // Ignore the 12 sync bytes
  uF1Data += 12;
  uF1Erasures += 12;

  // Store the data and erasures in the form expected by the ezpwd library
  std::vector<uint8_t> pField;
  std::vector<int> pFieldErasures;
  pField.resize(26);  // 24 + 2 parity bytes = 26

  // evenOdd = 0 = LSBs / evenOdd = 1 = MSBs
  for (int32_t evenOdd = 0; evenOdd < 2; evenOdd++) {
    for (int32_t Np = 0; Np < 43; Np++) {
      pFieldErasures.clear();
      for (int32_t Mp = 0; Mp < 26; Mp++) {
        // Get 24 byte codeword location + 2 P-parity bytes
        int32_t Vp = 2 * (43 * Mp + Np) + evenOdd;
        pField[static_cast<size_t>(Mp)] = uF1Data[Vp];

        // Get codeword erasures if present
        if (uF1Erasures[Vp] == 1) pFieldErasures.push_back(Mp);
      }

      // E-4(c): the P code is RS(26,24) - distance 3, at most 2 erasures. Skip
      // codewords with more than 2 known erasures rather than blindly decoding.
      if (pFieldErasures.size() > 2) continue;

      std::vector<int> position;
      int fixed = rspcRs.decode(pField, pFieldErasures, &position);

      if (fixed >= 0) {
        // Copy back the corrected data AND both P-parity bytes (Q protects the
        // P-parity, so discarding them would lose Q-recoverable corrections -
        // E-4(d)), and clear the corrected positions' erasure flags (E-4(a)).
        successfulCorrections++;
        // E-8(e): distinguish an already-valid codeword (fixed == 0) from one
        // that actually had symbols repaired.
        if (fixed == 0) {
          m_pCleanCodewords++;
        } else {
          m_pCorrectedCodewords++;
        }
        for (int32_t Mp = 0; Mp < 26; Mp++) {
          int32_t Vp = 2 * (43 * Mp + Np) + evenOdd;
          uF1Data[Vp] = pField[static_cast<size_t>(Mp)];
          uF1Erasures[Vp] = 0;
        }
      } else {
        // E-4(b): flag the failed codeword's data symbols for the next Q pass.
        for (int32_t Mp = 0; Mp < 24; Mp++) {
          int32_t Vp = 2 * (43 * Mp + Np) + evenOdd;
          uF1Erasures[Vp] = 1;
        }
      }
    }
  }

  // Reset the pointers
  uF1Data -= 12;
  uF1Erasures -= 12;

  // Show P-Parity correction result to debug
  if (successfulCorrections >= 86) {
    // P-Parity correction successful
  } else {
    ORC_LOG_DEBUG(
        "Rspc::pParityEcc(): P-Parity correction failed! Got {} correct out of "
        "86 possible codewords",
        successfulCorrections);
  }
}
