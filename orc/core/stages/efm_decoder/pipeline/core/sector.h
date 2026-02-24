/*
 * File:        sector.h
 * Module:      EFM-library
 * Purpose:     EFM Section classes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef SECTOR_H
#define SECTOR_H

#include <cstdint>
#include <vector>
#include <string>

// Sector Address class - stores ECMA-130 sector address in minutes, seconds, and frames
// (1/75th of a second)
class SectorAddress 
{
public:
SectorAddress();
    explicit SectorAddress(int32_t frames);
    SectorAddress(uint8_t minutes, uint8_t seconds, uint8_t frames);

    int32_t address() const { return m_address; }
    void setAddress(int32_t frames);
    void setTime(uint8_t minutes, uint8_t seconds, uint8_t frames);

    int32_t minutes() const { return m_address / (75 * 60); }
    int32_t seconds() const { return (m_address / 75) % 60; }
    int32_t frameNumber() const { return m_address % 75; }

    std::string toString() const;

    bool operator==(const SectorAddress &other) const { return m_address == other.m_address; }
    bool operator!=(const SectorAddress &other) const { return m_address != other.m_address; }
    bool operator<(const SectorAddress &other) const { return m_address < other.m_address; }
    bool operator>(const SectorAddress &other) const { return m_address > other.m_address; }
    bool operator<=(const SectorAddress &other) const { return !(*this > other); }
    bool operator>=(const SectorAddress &other) const { return !(*this < other); }
    SectorAddress operator+(const SectorAddress &other) const
    {
        return SectorAddress(m_address + other.m_address);
    }
    SectorAddress operator-(const SectorAddress &other) const
    {
        return SectorAddress(m_address - other.m_address);
    }
    SectorAddress &operator++()
    {
        ++m_address;
        return *this;
    }
    SectorAddress operator++(int)
    {
        SectorAddress tmp(*this);
        m_address++;
        return tmp;
    }
    SectorAddress &operator--()
    {
        --m_address;
        return *this;
    }
    SectorAddress operator--(int)
    {
        SectorAddress tmp(*this);
        m_address--;
        return tmp;
    }

    SectorAddress operator+(int frames) const
    {
        return SectorAddress(m_address + frames);
    }

    SectorAddress operator-(int frames) const
    {
        return SectorAddress(m_address - frames);
    }

private:
    int32_t m_address;
    static uint8_t intToBcd(uint32_t value);
};

class RawSector
{
public:
    RawSector();
    void pushData(const std::vector<uint8_t> &inData);
    void pushData(std::vector<uint8_t> &&inData);
    void pushErrorData(const std::vector<uint8_t> &inData);
    void pushErrorData(std::vector<uint8_t> &&inData);
    void pushPaddedData(const std::vector<uint8_t> &inData);
    void pushPaddedData(std::vector<uint8_t> &&inData);
    std::vector<uint8_t> data() const;
    const std::vector<uint8_t> &dataRef() const;
    std::vector<uint8_t> errorData() const;
    const std::vector<uint8_t> &errorDataRef() const;
    std::vector<uint8_t> paddedData() const;
    const std::vector<uint8_t> &paddedDataRef() const;
    std::vector<uint8_t> &mutableData();
    std::vector<uint8_t> &mutableErrorData();
    std::vector<uint8_t> &mutablePaddedData();
    uint32_t size() const;
    void showData();

private:
    std::vector<uint8_t> m_data;
    std::vector<uint8_t> m_errorData;
    std::vector<uint8_t> m_paddedData;

    uint8_t bcdToInt(uint8_t bcd);
};

class Sector
{
public:
    Sector();
    void pushData(const std::vector<uint8_t> &inData);
    void pushData(std::vector<uint8_t> &&inData);
    void pushErrorData(const std::vector<uint8_t> &inData);
    void pushErrorData(std::vector<uint8_t> &&inData);
    void pushPaddedData(const std::vector<uint8_t> &inData);
    void pushPaddedData(std::vector<uint8_t> &&inData);
    std::vector<uint8_t> data() const;
    std::vector<uint8_t> errorData() const;
    std::vector<uint8_t> paddedData() const;
    uint32_t size() const;
    void showData();

    void setAddress(SectorAddress address);
    SectorAddress address() const;
    void setMode(int32_t mode);
    int32_t mode() const;

    void dataValid(bool isValid) { m_validData = isValid; }
    bool isDataValid() const { return m_validData; }

private:
    std::vector<uint8_t> m_data;
    std::vector<uint8_t> m_errorData;
    std::vector<uint8_t> m_paddedData;

    SectorAddress m_address;
    int32_t m_mode;
    bool m_validData;
};

#endif // SECTOR_H
