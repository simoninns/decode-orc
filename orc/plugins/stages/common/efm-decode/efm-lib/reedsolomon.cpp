/*
 * File:        reedsolomon.cpp
 * Purpose:     EFM-library - Reed-Solomon CIRC functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "reedsolomon.h"

#include <orc/stage/logging.h>

#include <algorithm>
#include <cstdlib>

#include "efm_exception.h"
#include "ezpwd_compat.h"

// ezpwd ECMA-130 CIRC configuration. Both the C1 (32,28) and C2 (28,24)
// shortened codes carry 4 parity symbols over GF(2^8) with POLY=0x11D, FCR=0,
// PRIM=1, so a single RS(255,251) codec (255 - 4 = 251 payload) decodes both -
// ezpwd treats the shorter received words as shortened code words. (P-12: the
// former byte-identical C1RS/C2RS pair collapsed to one type.)
template <size_t SYMBOLS, size_t PAYLOAD>
struct CircRS;
template <size_t PAYLOAD>
struct CircRS<255, PAYLOAD>
    : public __RS(CircRS, uint8_t, 255, PAYLOAD, 0x11D, 0, 1, false);

// P-12: the RS codec carries precomputed Galois-field tables. ezpwd's decode()
// is a const method - it only reads those tables and works in local scratch -
// so a single shared instance is safe for concurrent decode() calls from
// multiple pipelines. A file-scope instance (constructed once) is used rather
// than thread_local because this CIRC path calls decode() ~26M times per stereo
// disc and is sensitive to per-access thread-local storage overhead.
CircRS<255, 255 - 4> circRs;

ReedSolomon::ReedSolomon() {
  // Initialise statistics
  m_validC1s = 0;
  m_fixedC1s = 0;
  m_errorC1s = 0;

  m_validC2s = 0;
  m_fixedC2s = 0;
  m_errorC2s = 0;
}

// Perform a C1 Reed-Solomon decoding operation on the input data
// This is a (32,28) Reed-Solomon encode - 32 bytes in, 28 bytes out
void ReedSolomon::c1Decode(std::vector<uint8_t>& inputData,
                           std::vector<uint8_t>& errorData,
                           std::vector<uint8_t>& paddedData) {
  // Ensure input data is 32 bytes long
  if (inputData.size() != 32) {
    ORC_LOG_ERROR("ReedSolomon::c1Decode - Input data must be 32 bytes long");
    throw efm::EfmDecodeError(__func__);
  }

  // Trim the parity bytes from the padded data (32 → 28)
  paddedData.resize(paddedData.size() - 4);

  // Copy input into reusable scratch for the ezpwd decoder (which modifies in
  // place). assign() retains the scratch buffer's capacity (P-6).
  m_scratchData.assign(inputData.begin(), inputData.end());
  m_erasures.clear();
  m_position.clear();

  // Convert the errorData into a list of erasure positions
  for (int index = 0; index < static_cast<int>(errorData.size()); ++index) {
    if (errorData[index]) m_erasures.push_back(index);
  }

  // Snapshot the supplied erasures before decode prunes them (see c2Decode).
  const std::vector<int> suppliedErasures = m_erasures;

  // E-2: the (32,28) C1 code has minimum distance 5, so it can attempt an
  // in-capacity erasure decode for up to 4 supplied erasures. C1 erasure flags
  // come from the EFM demodulator (invalid 14-bit symbols - reliable erasures),
  // so passing 3-4 of them straight through as uncorrectable without attempting
  // the decode discarded correctable words. More than 4 exceeds capacity.
  if (suppliedErasures.size() > 4) {
    inputData.resize(inputData.size() - 4);  // keep received bytes 0..27
    errorData.assign(inputData.size(), 1);
    ++m_errorC1s;
    return;
  }

  // Decode the data
  int result = circRs.decode(m_scratchData, m_erasures, &m_position);

  // Accept combinations satisfying 2e + s <= 4 (see c2Decode for the
  // rationale).
  bool accept = false;
  if (result >= 0) {
    int locatedErrors = 0;
    for (int p : m_position) {
      if (std::find(suppliedErasures.begin(), suppliedErasures.end(), p) ==
          suppliedErasures.end()) {
        ++locatedErrors;
      }
    }
    if (2 * locatedErrors + static_cast<int>(suppliedErasures.size()) <= 4) {
      accept = true;
    }
  }

  if (accept) {
    // Strip the parity bytes (32 → 28) from the corrected data
    inputData.assign(m_scratchData.begin(), m_scratchData.end() - 4);
    errorData.assign(inputData.size(), 0);

    if (result == 0) {
      ++m_validC1s;
    } else {
      ++m_fixedC1s;
    }
    return;
  }

  // Rejected: propagate the RECEIVED bytes (first 28) and flag all as corrupt.
  inputData.resize(inputData.size() - 4);
  errorData.assign(inputData.size(), 1);
  ++m_errorC1s;
  return;
}

// Perform a C2 Reed-Solomon decoding operation on the input data
// This is a (28,24) Reed-Solomon encode - 28 bytes in, 24 bytes out
void ReedSolomon::c2Decode(std::vector<uint8_t>& inputData,
                           std::vector<uint8_t>& errorData,
                           std::vector<uint8_t>& paddedData) {
  // Ensure input data is 28 bytes long
  if (inputData.size() != 28) {
    ORC_LOG_ERROR("ReedSolomon::c2Decode - Input data must be 28 bytes long");
    throw efm::EfmDecodeError(__func__);
  }

  if (errorData.size() != 28) {
    ORC_LOG_ERROR("ReedSolomon::c2Decode - Error data must be 28 bytes long");
    throw efm::EfmDecodeError(__func__);
  }

  // Remove parity positions 12-15 from paddedData (28 → 24)
  paddedData.erase(paddedData.begin() + 12, paddedData.begin() + 16);

  // Copy input into reusable scratch for the ezpwd decoder (which modifies in
  // place). assign() retains the scratch buffer's capacity (P-6).
  m_scratchData.assign(inputData.begin(), inputData.end());
  m_erasures.clear();
  m_position.clear();

  // Convert the errorData into a list of erasure positions
  for (int index = 0; index < static_cast<int>(errorData.size()); ++index) {
    if (errorData[index] != 0) m_erasures.push_back(index);
  }

  // Snapshot the supplied erasures: ezpwd's decode() prunes erasures it finds
  // were actually correct, so we need the original list to classify the
  // corrections it reports in 'position'.
  const std::vector<int> suppliedErasures = m_erasures;

  // The (28,24) C2 code has minimum distance 5, so more than 4 supplied
  // erasures already exceeds capacity and cannot be corrected.
  if (suppliedErasures.size() > 4) {
    // Remove parity byte positions 12-15 and assign result
    inputData.erase(inputData.begin() + 12, inputData.begin() + 16);
    errorData.assign(inputData.size(), 1);
    ++m_errorC2s;
    return;
  }

  // Decode the data
  int result = circRs.decode(m_scratchData, m_erasures, &m_position);

  // E-1: accept erasure-dominated corrections up to the code's full capacity.
  // IEC 60908 §16.3 / ECMA-130 Annex C: the (28,24) C2 code corrects any
  // combination of e located errors and s supplied erasures with 2e + s <= 4.
  // After a C1 burst failure all 28 inputs are flagged and the cross-interleave
  // spreads them so 3-4 erasures per C2 word is the *normal* burst case; the
  // previous "reject any decode that changed > 2 symbols" clamp discarded those
  // guaranteed-valid corrections, roughly halving CIRC's designed burst
  // tolerance. 'position' lists the positions ezpwd actually changed; any that
  // was not a supplied erasure is a located error.
  bool accept = false;
  if (result >= 0) {
    int locatedErrors = 0;
    for (int p : m_position) {
      if (std::find(suppliedErasures.begin(), suppliedErasures.end(), p) ==
          suppliedErasures.end()) {
        ++locatedErrors;
      }
    }
    if (2 * locatedErrors + static_cast<int>(suppliedErasures.size()) <= 4) {
      accept = true;
    }
  }

  if (accept) {
    // Keep the 24 payload bytes of the decoded (corrected) data, dropping
    // parity byte positions 12-15. Written straight into inputData (reusing its
    // capacity) so the scratch buffer is left intact for the next call.
    inputData.resize(24);
    std::copy(m_scratchData.begin(), m_scratchData.begin() + 12,
              inputData.begin());
    std::copy(m_scratchData.begin() + 16, m_scratchData.begin() + 28,
              inputData.begin() + 12);
    errorData.assign(inputData.size(), 0);

    if (result == 0) {
      ++m_validC2s;
    } else {
      ++m_fixedC2s;
    }
    return;
  }

  // Rejected: propagate the RECEIVED bytes (not the decoder-modified tmpData,
  // which may hold a miscorrection) and flag all outputs as corrupt.
  inputData.erase(inputData.begin() + 12, inputData.begin() + 16);
  errorData.assign(inputData.size(), 1);
  ++m_errorC2s;
  return;
}

// Getter functions for the statistics
int32_t ReedSolomon::validC1s() const { return m_validC1s; }

int32_t ReedSolomon::fixedC1s() const { return m_fixedC1s; }

int32_t ReedSolomon::errorC1s() const { return m_errorC1s; }

int32_t ReedSolomon::validC2s() const { return m_validC2s; }

int32_t ReedSolomon::fixedC2s() const { return m_fixedC2s; }

int32_t ReedSolomon::errorC2s() const { return m_errorC2s; }
