/*
 * File:        rspc.h
 * Purpose:     EFM-library - Reed-Solomon Product-like Code (RSPC) functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef RSPC_H
#define RSPC_H

#include <cstdint>
#include <vector>

class Rspc {
 public:
  Rspc();
  void qParityEcc(std::vector<uint8_t>& inputData,
                  std::vector<uint8_t>& errorData);
  void pParityEcc(std::vector<uint8_t>& inputData,
                  std::vector<uint8_t>& errorData);

  // E-8(e): codeword-level RSPC statistics accumulated over this instance's
  // passes. A "clean" codeword decoded with zero symbol corrections (it was
  // already valid); a "corrected" one had one or more symbols repaired.
  // Separating them lets callers report real correction activity instead of
  // conflating already-valid with repaired.
  int32_t qCleanCodewords() const { return m_qCleanCodewords; }
  int32_t qCorrectedCodewords() const { return m_qCorrectedCodewords; }
  int32_t pCleanCodewords() const { return m_pCleanCodewords; }
  int32_t pCorrectedCodewords() const { return m_pCorrectedCodewords; }

 private:
  int32_t m_qCleanCodewords = 0;
  int32_t m_qCorrectedCodewords = 0;
  int32_t m_pCleanCodewords = 0;
  int32_t m_pCorrectedCodewords = 0;
};

#endif  // RSPC_H