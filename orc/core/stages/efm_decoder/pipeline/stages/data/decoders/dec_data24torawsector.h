/*
 * File:        dec_data24torawsector.h
 * Module:      efm-decoder-data
 * Purpose:     EFM Data24 to data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_DATA24TORAWSECTOR_H
#define DEC_DATA24TORAWSECTOR_H

#include <deque>
#include <vector>
#include <string>
#include "decoders.h"
#include "sector.h"

class Data24ToRawSector : public Decoder
{
public:
    Data24ToRawSector();
    void pushSection(const Data24Section &data24Section);
    RawSector popSector();
    bool isReady() const;

    void showStatistics() const;
    std::string statisticsText() const;

private:
    void processStateMachine();
    size_t bufferedBytes() const;
    void consumeBufferedBytes(size_t count, bool consumePadded = true);
    void compactBuffers();

    std::deque<Data24Section> m_inputBuffer;
    std::deque<RawSector> m_outputBuffer;

    // State machine states
    enum State { WaitingForSync, InSync, LostSync };

    State m_currentState;

    // 12 byte sync pattern
    const std::vector<uint8_t> m_syncPattern{0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

    // Sector data buffer
    std::vector<uint8_t> m_sectorData;
    std::vector<uint8_t> m_sectorErrorData;
    std::vector<uint8_t> m_sectorPaddedData;
    size_t m_sectorDataStart;
    size_t m_sectorErrorStart;
    size_t m_sectorPaddedStart;

    // State machine state processing functions
    State waitingForSync();
    State inSync();
    State lostSync();

    uint32_t m_missedSyncPatternCount;
    uint32_t m_goodSyncPatternCount;
    uint32_t m_badSyncPatternCount;

    // Statistics
    uint32_t m_validSectorCount;
    uint32_t m_discardedBytes;
    uint32_t m_discardedPaddingBytes;
    uint32_t m_syncLostCount;
};

#endif // DEC_DATA24TORAWSECTOR_H