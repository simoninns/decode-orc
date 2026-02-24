/*
 * File:        dec_sectorcorrection.cpp
 * Module:      efm-decoder-data
 * Purpose:     EFM Data24 to data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_sectorcorrection.h"
#include "logging.h"

SectorCorrection::SectorCorrection()
    : m_haveLastSectorInfo(false),
      m_lastSectorAddress(0),
      m_lastSectorMode(0),
      m_goodSectors(0),
      m_missingLeadingSectors(0),
      m_missingSectors(0)
{}

void SectorCorrection::pushSector(const Sector &sector)
{
    // Add the data to the input buffer
    m_inputBuffer.push_back(sector);

    // Process the queue
    processQueue();
}

Sector SectorCorrection::popSector()
{
    // Return the first item in the output buffer
    Sector sector = m_outputBuffer.front();
    m_outputBuffer.pop_front();
    return sector;
}

void SectorCorrection::processQueue()
{
    while (!m_inputBuffer.empty()) {
        // Get the first item in the input buffer
        Sector sector = m_inputBuffer.front();
        m_inputBuffer.pop_front();

        if (!m_haveLastSectorInfo) {
            // This is the first sector - we have to fill the missing leading sectors
            // if the address isn't 0

            if (sector.address().frameNumber() != 0) {
                // Fill the missing leading sectors from address 0 to the first decoded sector address
                LOG_DEBUG("SectorCorrection::processQueue(): First received frame address is {} ({})",
                             sector.address().address(), sector.address().toString());
                LOG_DEBUG("SectorCorrection::processQueue(): Filling missing leading sectors with {} sectors",
                             sector.address().address());
                for (int i = 0; i < sector.address().address(); i++) {
                    Sector missingSector;
                    missingSector.dataValid(false);
                    missingSector.setAddress(SectorAddress(i));
                    missingSector.setMode(1);
                    missingSector.pushData(std::vector<uint8_t>(2048, 0));
                    missingSector.pushErrorData(std::vector<uint8_t>(2048, 1));
                    m_outputBuffer.push_back(missingSector);
                    m_missingLeadingSectors++;
                }
            }

            m_haveLastSectorInfo = true;
            m_lastSectorAddress = sector.address();
            m_lastSectorMode = sector.mode();
        } else {
            // Check if there is a gap between this sector and the last
            if (sector.address() != m_lastSectorAddress + 1) {
                // Calculate the number of missing sectors
                int32_t gap = sector.address().address() - m_lastSectorAddress.address() - 1;

                LOG_DEBUG("SectorCorrection::processQueue(): Sector is not in the correct position. Last good sector address: {} {} Current sector address: {} {} Gap: {}",
                             m_lastSectorAddress.address(), m_lastSectorAddress.toString(),
                             sector.address().address(), sector.address().toString(), gap);
                
                 // Add missing sectors
                for (int32_t i = 0; i < gap; ++i) {
                    Sector missingSector;
                    missingSector.dataValid(false);
                    missingSector.setAddress(m_lastSectorAddress + 1 + i);
                    missingSector.setMode(1);
                    missingSector.pushData(std::vector<uint8_t>(2048, 0));
                    missingSector.pushErrorData(std::vector<uint8_t>(2048, 1));
                    m_outputBuffer.push_back(missingSector);
                    m_missingSectors++;
                }
            }
        }

        // Add the sector to the output buffer
        m_outputBuffer.push_back(sector);
        m_goodSectors++;

        // Update the last-good sector information
        m_lastSectorAddress = sector.address();
        m_lastSectorMode = sector.mode();
    }
}

bool SectorCorrection::isReady() const
{
    // Return true if the output buffer is not empty
    return !m_outputBuffer.empty();
}

void SectorCorrection::showStatistics() const
{
    LOG_INFO("Sector gap correction:");
    LOG_INFO("  Good sectors: {}", m_goodSectors);
    LOG_INFO("  Missing leading sectors: {}", m_missingLeadingSectors);
    LOG_INFO("  Missing/Gap sectors: {}", m_missingSectors);
    LOG_INFO("  Total sectors: {}", m_goodSectors + m_missingLeadingSectors + m_missingSectors);
}
