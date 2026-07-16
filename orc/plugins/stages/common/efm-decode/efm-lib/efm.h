/*
 * File:        efm.h
 * Purpose:     EFM-library - EFM conversion functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef EFM_H
#define EFM_H

#include <array>
#include <cstdint>
#include <string>

class Efm {
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
  static constexpr size_t EFM_LUT_SIZE = 258;  // 256 + 2 sync symbols
  static constexpr uint16_t INVALID_EFM = 300;
  // P-8: EFM codes are 14-bit, so a flat 16 KiB reverse table indexed directly
  // by the code beats a hashed lookup (no hash/bucket-chase ~240K times/s of
  // audio) and is built once per instance.
  static constexpr size_t EFM_CODE_SPACE = 1u << 14;  // 14-bit code space
  std::array<uint16_t, EFM_CODE_SPACE> m_efmReverse;
};

#endif  // EFM_H