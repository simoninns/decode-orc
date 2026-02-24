/*
 * File:        subcode.h
 * Module:      EFM-library
 * Purpose:     Subcode channel functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef SUBCODE_H
#define SUBCODE_H

#include <cstdint>
#include <vector>
#include <string>

#include "section_metadata.h"

class Subcode
{
public:
    Subcode() = default;

    SectionMetadata fromData(const std::vector<uint8_t> &data);
    std::vector<uint8_t> toData(const SectionMetadata &sectionMetadata);

private:
    void setBit(std::vector<uint8_t> &data, uint8_t bitPosition, bool value);
    bool getBit(const std::vector<uint8_t> &data, uint8_t bitPosition);
    bool isCrcValid(std::vector<uint8_t> qChannelData);
    uint16_t getQChannelCrc(std::vector<uint8_t> qChannelData);
    void setQChannelCrc(std::vector<uint8_t> &qChannelData);
    uint16_t calculateQChannelCrc16(const std::vector<uint8_t> &data);
    bool repairData(std::vector<uint8_t> &qChannelData);

    uint8_t countBits(uint8_t byteValue);
    uint8_t intToBcd2(uint8_t value);
    uint8_t bcd2ToInt(uint8_t bcd);
    int32_t validateAndClampTimeValue(int32_t value, int32_t maxValue, const std::string &valueName,
                                     SectionMetadata &sectionMetadata);
};

#endif // SUBCODE_H