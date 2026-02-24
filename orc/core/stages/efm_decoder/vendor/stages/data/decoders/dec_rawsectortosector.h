/*
 * File:        dec_rawsectortosector.h
 * Module:      efm-decoder-data
 * Purpose:     EFM Data24 to data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_RAWSECTORTOSECTOR_H
#define DEC_RAWSECTORTOSECTOR_H

#include <deque>
#include <vector>
#include <string>
#include "decoders.h"
#include "sector.h"
#include "rspc.h"

class RawSectorToSector : public Decoder
{
public:
    RawSectorToSector();
    void pushSector(const RawSector &rawSector);
    Sector popSector();
    bool isReady() const;

    void showStatistics() const;
    std::string statisticsText() const;

private:
    void processQueue();
    uint8_t bcdToInt(uint8_t bcd);
    uint32_t crc32(const std::vector<uint8_t> &src, int32_t size);

    std::deque<RawSector> m_inputBuffer;
    std::deque<Sector> m_outputBuffer;

    // Statistics
    uint32_t m_validSectors;
    uint32_t m_invalidSectors;
    uint32_t m_correctedSectors;

    uint32_t m_mode0Sectors;
    uint32_t m_mode1Sectors;
    uint32_t m_mode2Sectors;
    uint32_t m_invalidModeSectors;
};

#endif // DEC_RAWSECTORTOSECTOR_H