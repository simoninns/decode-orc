/*
 * File:        sector.cpp
 * Module:      EFM-library
 * Purpose:     EFM Section classes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "sector.h"
#include <logging.h>
#include <cstdlib>
#include <cstdio>
#include <iomanip>
#include <sstream>
#include <utility>

// Sector address class
// ---------------------------------------------------------------------------------------------------
SectorAddress::SectorAddress() : m_address(0)
{
    // There are 75 frames per second, 60 seconds per minute, and 60 minutes per hour
    // so the maximum number of frames is 75 * 60 * 60 = 270000
    if (m_address < 0 || m_address >= 270000) {
        LOG_ERROR("SectorAddress::SectionTime(): Invalid address value of {}", m_address);
        std::exit(1);
    }
}

SectorAddress::SectorAddress(int32_t address) : m_address(address)
{
    if (m_address < 0 || m_address >= 270000) {
        LOG_ERROR("SectorAddress::SectionTime(): Invalid address value of {}", m_address);
        std::exit(1);
    }
}

SectorAddress::SectorAddress(uint8_t minutes, uint8_t seconds, uint8_t frames)
{
    setTime(minutes, seconds, frames);
}

void SectorAddress::setAddress(int32_t address)
{
    if (address < 0 || address >= 270000) {
        LOG_ERROR("SectorAddress::setFrames(): Invalid address value of {}", address);
        std::exit(1);
    }

    m_address = address;
}

void SectorAddress::setTime(uint8_t minutes, uint8_t seconds, uint8_t frames)
{
    // Set the address in minutes, seconds, and frames

    // Ensure the time is sane
    if (minutes >= 60) {
        LOG_DEBUG("SectorAddress::setTime(): Invalid minutes value {}, setting to 59", minutes);
        minutes = 59;
    }
    if (seconds >= 60) {
        LOG_DEBUG("SectorAddress::setTime(): Invalid seconds value {}, setting to 59", seconds);
        seconds = 59;
    }
    if (frames >= 75) {
        LOG_DEBUG("SectorAddress::setTime(): Invalid frames value {}, setting to 74", frames);
        frames = 74;
    }

    m_address = (minutes * 60 + seconds) * 75 + frames;
}

std::string SectorAddress::toString() const
{
    // Return the time in the format MM:SS:FF
    std::ostringstream stream;
    stream << std::setfill('0')
           << std::setw(2) << static_cast<int>(m_address / (75 * 60)) << ":"
           << std::setw(2) << static_cast<int>((m_address / 75) % 60) << ":"
           << std::setw(2) << static_cast<int>(m_address % 75);
    return stream.str();
}

uint8_t SectorAddress::intToBcd(uint32_t value)
{
    if (value > 99) {
        LOG_ERROR("SectorAddress::intToBcd(): Value must be in the range 0 to 99.");
        std::exit(1);
    }

    uint16_t bcd = 0;
    uint16_t factor = 1;

    while (value > 0) {
        bcd += (value % 10) * factor;
        value /= 10;
        factor *= 16;
    }

    // Ensure the result is always 1 byte (00-99)
    return bcd & 0xFF;
}

// Raw sector class
// The raw sector is 2352 bytes (unscrambled) and contains user data and error correction data
RawSector::RawSector()
    : m_data(std::vector<uint8_t>(2352, 0)),
    m_errorData(std::vector<uint8_t>(2352, 0)),
    m_paddedData(std::vector<uint8_t>(2352, 0))
{}

void RawSector::pushData(const std::vector<uint8_t> &inData)
{
    m_data = inData;
}

void RawSector::pushData(std::vector<uint8_t> &&inData)
{
    m_data = std::move(inData);
}

void RawSector::pushErrorData(const std::vector<uint8_t> &inData)
{
    m_errorData = inData;
}

void RawSector::pushErrorData(std::vector<uint8_t> &&inData)
{
    m_errorData = std::move(inData);
}

void RawSector::pushPaddedData(const std::vector<uint8_t> &inData)
{
    m_paddedData = inData;
}

void RawSector::pushPaddedData(std::vector<uint8_t> &&inData)
{
    m_paddedData = std::move(inData);
}

std::vector<uint8_t> RawSector::data() const
{
    return m_data;
}

const std::vector<uint8_t> &RawSector::dataRef() const
{
    return m_data;
}

std::vector<uint8_t> RawSector::errorData() const
{
    return m_errorData;
}

const std::vector<uint8_t> &RawSector::errorDataRef() const
{
    return m_errorData;
}

std::vector<uint8_t> RawSector::paddedData() const
{
    return m_paddedData;
}

const std::vector<uint8_t> &RawSector::paddedDataRef() const
{
    return m_paddedData;
}

std::vector<uint8_t> &RawSector::mutableData()
{
    return m_data;
}

std::vector<uint8_t> &RawSector::mutableErrorData()
{
    return m_errorData;
}

std::vector<uint8_t> &RawSector::mutablePaddedData()
{
    return m_paddedData;
}

uint32_t RawSector::size() const
{
    return static_cast<uint32_t>(m_data.size());
}

void RawSector::showData()
{
    const int bytesPerLine = 48;
    bool hasError = false;

    // Extract the sector address data (note: this is not verified as correct)
    int32_t min = bcdToInt(m_data[12]);
    int32_t sec = bcdToInt(m_data[13]);
    int32_t frame = bcdToInt(m_data[14]);
    SectorAddress address(static_cast<uint8_t>(min), static_cast<uint8_t>(sec), static_cast<uint8_t>(frame));

    for (int offset = 0; offset < static_cast<int>(m_data.size()); offset += bytesPerLine) {
        // Print offset
        std::string line = "RawSector::showData() - [" + address.toString() + "] ";
        char offsetStr[16];
        snprintf(offsetStr, sizeof(offsetStr), "%06x: ", offset);
        line += offsetStr;
        
        // Print hex values
        for (int i = 0; i < bytesPerLine && (offset + i) < static_cast<int>(m_data.size()); ++i) {
            if (m_errorData[offset + i] == 0) {
                char hexStr[4];
                snprintf(hexStr, sizeof(hexStr), "%02x ", m_data[offset + i]);
                line += hexStr;
            } else {
                line += "XX ";
                hasError = true;
            }
        }

        LOG_TRACE("{}", line);
    }

    if (hasError) {
        LOG_TRACE("RawSector contains errors");
    }
}

uint8_t RawSector::bcdToInt(uint8_t bcd)
{
    return static_cast<uint8_t>((bcd >> 4) * 10 + (bcd & 0x0F));
}

// Sector class
// The sector is 2048 bytes and contains user data only (post error correction)
Sector::Sector()
    : m_data(std::vector<uint8_t>(2048, 0)),
      m_errorData(std::vector<uint8_t>(2048, 0))
{}

void Sector::pushData(const std::vector<uint8_t> &inData)
{
    m_data = inData;
}

void Sector::pushData(std::vector<uint8_t> &&inData)
{
    m_data = std::move(inData);
}

void Sector::pushErrorData(const std::vector<uint8_t> &inData)
{
    m_errorData = inData;
}

void Sector::pushErrorData(std::vector<uint8_t> &&inData)
{
    m_errorData = std::move(inData);
}

void Sector::pushPaddedData(const std::vector<uint8_t> &inData)
{
    m_paddedData = inData;
}

void Sector::pushPaddedData(std::vector<uint8_t> &&inData)
{
    m_paddedData = std::move(inData);
}

std::vector<uint8_t> Sector::data() const
{
    return m_data;
}

std::vector<uint8_t> Sector::errorData() const
{
    return m_errorData;
}

std::vector<uint8_t> Sector::paddedData() const
{
    return m_paddedData;
}

uint32_t Sector::size() const
{
    return static_cast<uint32_t>(m_data.size());
}

void Sector::showData()
{
    const int bytesPerLine = 2048/64;
    bool hasError = false;

    for (int offset = 0; offset < static_cast<int>(m_data.size()); offset += bytesPerLine) {
        // Print offset
        std::string line = "Sector::showData() - [" + m_address.toString() + "] ";
        char offsetStr[16];
        snprintf(offsetStr, sizeof(offsetStr), "%06x: ", offset);
        line += offsetStr;
        
        // Print hex values
        for (int i = 0; i < bytesPerLine && (offset + i) < static_cast<int>(m_data.size()); ++i) {
            if (m_errorData[offset + i] == 0) {
                char hexStr[4];
                snprintf(hexStr, sizeof(hexStr), "%02x ", m_data[offset + i]);
                line += hexStr;
            } else {
                line += "XX ";
                hasError = true;
            }
        }

        LOG_TRACE("{}", line);
    }

    if (hasError) {
        LOG_TRACE("Sector contains errors");
    }
}

void Sector::setAddress(SectorAddress address)
{
    m_address = address;
}

SectorAddress Sector::address() const
{
    return m_address;
}

void Sector::setMode(int32_t mode)
{
    // -1 is invalid/unknown
    // 0 is mode 0
    // 1 is mode 1
    // 2 is mode 2

    if (mode < -1 || mode > 2) {
        LOG_ERROR("Sector::setMode(): Invalid mode value of {}", mode);
        std::exit(1);
    }
    m_mode = mode;
}

int32_t Sector::mode() const
{
    return m_mode;
}
