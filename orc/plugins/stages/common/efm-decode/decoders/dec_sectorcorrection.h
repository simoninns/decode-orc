/*
 * File:        dec_sectorcorrection.h
 * Purpose:     efm-decoder-data - EFM Data24 to data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_SECTORCORRECTION_H
#define DEC_SECTORCORRECTION_H

#include <deque>

#include "decoders.h"
#include "sector.h"

class SectorCorrection : public Decoder {
 public:
  SectorCorrection();
  void pushSector(const Sector& sector);
  Sector popSector();
  bool isReady() const;

  void showStatistics() const;

  // Accessors for the curated decode report (sector-gap correction).
  uint32_t goodSectors() const { return m_goodSectors; }
  uint32_t missingSectors() const { return m_missingSectors; }
  uint32_t missingLeadingSectors() const { return m_missingLeadingSectors; }

 private:
  void processQueue();

  std::deque<Sector> m_inputBuffer;
  std::deque<Sector> m_outputBuffer;

  bool m_haveLastSectorInfo;
  SectorAddress m_lastSectorAddress;
  int32_t m_lastSectorMode;

  // Statistics
  uint32_t m_goodSectors;
  uint32_t m_missingLeadingSectors;
  uint32_t m_missingSectors;
};

#endif  // DEC_SECTORCORRECTION_H