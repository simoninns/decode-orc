/*
 * File:        dec_rawsectortosector.cpp
 * Module:      efm-decoder-data
 * Purpose:     EFM Data24 to data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_rawsectortosector.h"
#include "core/logging.h"

// This table is the CRC32 look-up for the EDC data
static constexpr uint32_t crc32Lut[256] = {
    0x00000000, 0x90910101, 0x91210201, 0x01B00300, 0x92410401, 0x02D00500, 0x03600600, 0x93F10701,
    0x94810801, 0x04100900, 0x05A00A00, 0x95310B01, 0x06C00C00, 0x96510D01, 0x97E10E01, 0x07700F00,
    0x99011001, 0x09901100, 0x08201200, 0x98B11301, 0x0B401400, 0x9BD11501, 0x9A611601, 0x0AF01700,
    0x0D801800, 0x9D111901, 0x9CA11A01, 0x0C301B00, 0x9FC11C01, 0x0F501D00, 0x0EE01E00, 0x9E711F01,
    0x82012001, 0x12902100, 0x13202200, 0x83B12301, 0x10402400, 0x80D12501, 0x81612601, 0x11F02700,
    0x16802800, 0x86112901, 0x87A12A01, 0x17302B00, 0x84C12C01, 0x14502D00, 0x15E02E00, 0x85712F01,
    0x1B003000, 0x8B913101, 0x8A213201, 0x1AB03300, 0x89413401, 0x19D03500, 0x18603600, 0x88F13701,
    0x8F813801, 0x1F103900, 0x1EA03A00, 0x8E313B01, 0x1DC03C00, 0x8D513D01, 0x8CE13E01, 0x1C703F00,
    0xB4014001, 0x24904100, 0x25204200, 0xB5B14301, 0x26404400, 0xB6D14501, 0xB7614601, 0x27F04700,
    0x20804800, 0xB0114901, 0xB1A14A01, 0x21304B00, 0xB2C14C01, 0x22504D00, 0x23E04E00, 0xB3714F01,
    0x2D005000, 0xBD915101, 0xBC215201, 0x2CB05300, 0xBF415401, 0x2FD05500, 0x2E605600, 0xBEF15701,
    0xB9815801, 0x29105900, 0x28A05A00, 0xB8315B01, 0x2BC05C00, 0xBB515D01, 0xBAE15E01, 0x2A705F00,
    0x36006000, 0xA6916101, 0xA7216201, 0x37B06300, 0xA4416401, 0x34D06500, 0x35606600, 0xA5F16701,
    0xA2816801, 0x32106900, 0x33A06A00, 0xA3316B01, 0x30C06C00, 0xA0516D01, 0xA1E16E01, 0x31706F00,
    0xAF017001, 0x3F907100, 0x3E207200, 0xAEB17301, 0x3D407400, 0xADD17501, 0xAC617601, 0x3CF07700,
    0x3B807800, 0xAB117901, 0xAAA17A01, 0x3A307B00, 0xA9C17C01, 0x39507D00, 0x38E07E00, 0xA8717F01,
    0xD8018001, 0x48908100, 0x49208200, 0xD9B18301, 0x4A408400, 0xDAD18501, 0xDB618601, 0x4BF08700,
    0x4C808800, 0xDC118901, 0xDDA18A01, 0x4D308B00, 0xDEC18C01, 0x4E508D00, 0x4FE08E00, 0xDF718F01,
    0x41009000, 0xD1919101, 0xD0219201, 0x40B09300, 0xD3419401, 0x43D09500, 0x42609600, 0xD2F19701,
    0xD5819801, 0x45109900, 0x44A09A00, 0xD4319B01, 0x47C09C00, 0xD7519D01, 0xD6E19E01, 0x46709F00,
    0x5A00A000, 0xCA91A101, 0xCB21A201, 0x5BB0A300, 0xC841A401, 0x58D0A500, 0x5960A600, 0xC9F1A701,
    0xCE81A801, 0x5E10A900, 0x5FA0AA00, 0xCF31AB01, 0x5CC0AC00, 0xCC51AD01, 0xCDE1AE01, 0x5D70AF00,
    0xC301B001, 0x5390B100, 0x5220B200, 0xC2B1B301, 0x5140B400, 0xC1D1B501, 0xC061B601, 0x50F0B700,
    0x5780B800, 0xC711B901, 0xC6A1BA01, 0x5630BB00, 0xC5C1BC01, 0x5550BD00, 0x54E0BE00, 0xC471BF01,
    0x6C00C000, 0xFC91C101, 0xFD21C201, 0x6DB0C300, 0xFE41C401, 0x6ED0C500, 0x6F60C600, 0xFFF1C701,
    0xF881C801, 0x6810C900, 0x69A0CA00, 0xF931CB01, 0x6AC0CC00, 0xFA51CD01, 0xFBE1CE01, 0x6B70CF00,
    0xF501D001, 0x6590D100, 0x6420D200, 0xF4B1D301, 0x6740D400, 0xF7D1D501, 0xF661D601, 0x66F0D700,
    0x6180D800, 0xF111D901, 0xF0A1DA01, 0x6030DB00, 0xF3C1DC01, 0x6350DD00, 0x62E0DE00, 0xF271DF01,
    0xEE01E001, 0x7E90E100, 0x7F20E200, 0xEFB1E301, 0x7C40E400, 0xECD1E501, 0xED61E601, 0x7DF0E700,
    0x7A80E800, 0xEA11E901, 0xEBA1EA01, 0x7B30EB00, 0xE8C1EC01, 0x7850ED00, 0x79E0EE00, 0xE971EF01,
    0x7700F000, 0xE791F101, 0xE621F201, 0x76B0F300, 0xE541F401, 0x75D0F500, 0x7460F600, 0xE4F1F701,
    0xE381F801, 0x7310F900, 0x72A0FA00, 0xE231FB01, 0x71C0FC00, 0xE151FD01, 0xE0E1FE01, 0x7070FF00
};

RawSectorToSector::RawSectorToSector()
    : m_validSectors(0),
    m_invalidSectors(0),
    m_correctedSectors(0),
    m_mode0Sectors(0),
    m_mode1Sectors(0),
    m_mode2Sectors(0),
    m_invalidModeSectors(0)
{}

void RawSectorToSector::pushSector(const RawSector &rawSector)
{
    // Add the data to the input buffer
    m_inputBuffer.push_back(rawSector);

    // Process the queue
    processQueue();
}

Sector RawSectorToSector::popSector()
{
    // Return the first item in the output buffer
    Sector sector = m_outputBuffer.front();
    m_outputBuffer.pop_front();
    return sector;
}

bool RawSectorToSector::isReady() const
{
    // Return true if the output buffer is not empty
    return !m_outputBuffer.empty();
}

// Note: Mode 0 and Mode 2 support missing
// Note: Does not fill missing sectors
void RawSectorToSector::processQueue()
{
    while (!m_inputBuffer.empty()) {
        // Get the first item in the input buffer (move to avoid copy)
        RawSector rawSector = std::move(m_inputBuffer.front());
        m_inputBuffer.pop_front();
        bool rawSectorValid = false;

        // Verify the data sizes (sanity check) - use dataRef() to avoid copy
        if (rawSector.dataRef().size() != 2352) {
            LOG_DEBUG("RawSectorToSector::processQueue(): Sector data size is incorrect. Expected 2352 bytes, got {} bytes", rawSector.dataRef().size());
            LOG_CRITICAL("RawSectorToSector::processQueue(): Sector data size is incorrect");
            std::exit(1);
        }

        if (rawSector.errorDataRef().size() != 2352) {
            LOG_DEBUG("RawSectorToSector::processQueue(): Sector error data size is incorrect. Expected 2352 bytes, got {} bytes", rawSector.errorDataRef().size());
            LOG_CRITICAL("RawSectorToSector::processQueue(): Sector error data size is incorrect");
            std::exit(1);
        }

        if (rawSector.paddedDataRef().size() != 2352) {
            LOG_DEBUG("RawSectorToSector::processQueue(): Sector padded data size is incorrect. Expected 2352 bytes, got {} bytes", rawSector.paddedDataRef().size());
            LOG_CRITICAL("RawSectorToSector::processQueue(): Sector padded data size is incorrect");
            std::exit(1);
        }

        // Determine the sector mode (for modes 0 and 2 there is no correction available)
        int32_t mode = 0;

        // Is the mode byte valid (not error or padding)? Use dataRef() to avoid copy
        const std::vector<uint8_t> &rawData = rawSector.dataRef();
        const std::vector<uint8_t> &rawErrorData = rawSector.errorDataRef();
        
        if (static_cast<uint8_t>(rawErrorData[15]) != 0) {
            // Mode byte is invalid
            LOG_DEBUG("RawSectorToSector::processQueue(): Sector mode byte is invalid. Assuming it's mode 1");
            mode = -1;
        } else {
            // Extract the sector mode data
            if (static_cast<uint8_t>(rawData[15]) == 0) mode = 0;
            else if (static_cast<uint8_t>(rawData[15]) == 1) mode = 1;
            else if (static_cast<uint8_t>(rawData[15]) == 2) mode = 2;
            else mode = -1;

            if (mode != 1) {
                LOG_DEBUG("RawSectorToSector::processQueue(): Sector mode byte is valid, but mode isn't? Mode reported as {}", static_cast<uint8_t>(rawData[15]));
            }
        }

        // If the mode is invalid, we try to treat the sector as mode 1 to see if the error correction
        // makes the mode metadata valid.  If it doesn't we discard the sector as error
        if (mode == 1 || mode == -1) {
            // Compute the CRC32 of the sector data based on the EDC word
            uint32_t originalEdcWord =
                ((static_cast<uint32_t>(static_cast<uint8_t>(rawData[2064]))) <<  0) |
                ((static_cast<uint32_t>(static_cast<uint8_t>(rawData[2065]))) <<  8) |
                ((static_cast<uint32_t>(static_cast<uint8_t>(rawData[2066]))) << 16) |
                ((static_cast<uint32_t>(static_cast<uint8_t>(rawData[2067]))) << 24);

            uint32_t edcWord = crc32(rawData, 2064);

            // If the CRC32 of the sector data is incorrect, attempt to correct it using Q and P parity
            if (originalEdcWord != edcWord) {
                LOG_DEBUG("RawSectorToSector::processQueue(): CRC32 error - sector data is corrupt. EDC: {} Calculated: {} attempting to correct",
                             originalEdcWord, edcWord);

                // Attempt Q and P parity error correction on the sector data
                Rspc rspc;

                // Modify sector data in-place to avoid copies
                std::vector<uint8_t> &correctedData = rawSector.mutableData();
                std::vector<uint8_t> &correctedErrorData = rawSector.mutableErrorData();

                rspc.qParityEcc(correctedData, correctedErrorData);
                rspc.pParityEcc(correctedData, correctedErrorData);

                // Data modified in-place, no need to push back

                // Computer CRC32 again for the corrected data
                uint32_t correctedEdcWord =
                    ((static_cast<uint32_t>(static_cast<uint8_t>(correctedData[2064]))) <<  0) |
                    ((static_cast<uint32_t>(static_cast<uint8_t>(correctedData[2065]))) <<  8) |
                    ((static_cast<uint32_t>(static_cast<uint8_t>(correctedData[2066]))) << 16) |
                    ((static_cast<uint32_t>(static_cast<uint8_t>(correctedData[2067]))) << 24);

                edcWord = crc32(correctedData, 2064);

                // Is the CRC now correct?
                if (correctedEdcWord != edcWord) {
                    // Error correction failed - sector is invalid and there's nothing more we can do

                    if (mode == 1) {
                        LOG_DEBUG("RawSectorToSector::processQueue(): CRC32 error - sector data cannot be recovered. EDC: {} Calculated: {} post correction",
                                                  correctedEdcWord, edcWord);
                        m_mode1Sectors++;
                        rawSectorValid = false;
                    } else {
                        // Mode was invalid as the sector is completely invalid.  This is probably padding of some sort
                        LOG_DEBUG("RawSectorToSector::processQueue(): Sector mode was invalid and the sector doesn't appear to be mode 1");
                        m_invalidModeSectors++;
                        rawSectorValid = false;
                    }
                } else {
                    // Sector was invalid, but now corrected
                    LOG_DEBUG("RawSectorToSector::processQueue(): Sector data corrected. EDC: {} Calculated: {}", correctedEdcWord, edcWord);
                    m_correctedSectors++;
                    rawSectorValid = true;
                    mode = 1; // If error correction worked... this a mode 1 sector
                }
            } else {
                // Original sector data is valid
                m_validSectors++;
                rawSectorValid = true;

                // It's possible that the original mode byte was marked as error, but the RSCP error correction
                // was able to correct the data.  In this case, we need to update the mode byte
                if (static_cast<uint8_t>(rawData[15]) == 0) mode = 0;
                else if (static_cast<uint8_t>(rawData[15]) == 1) mode = 1;
                else if (static_cast<uint8_t>(rawData[15]) == 2) mode = 2;
                else mode = -1;

                if (mode == 0) m_mode0Sectors++;
                else if (mode == 1) m_mode1Sectors++;
                else if (mode == 2) m_mode2Sectors++;
                else {
                    LOG_DEBUG("RawSectorToSector::processQueue(): EDC: {} Calculated: {} Mode byte: {}",
                                 originalEdcWord, edcWord, static_cast<uint8_t>(rawData[15]));
                    LOG_CRITICAL("RawSectorToSector::processQueue(): Invalid sector mode of {} - even though sector data was valid - bug?", mode);
                    std::exit(1);
                }
            }
        } else {
            // Mode 0 and Mode 2 sectors are not corrected
            if (mode == 0) m_mode0Sectors++;
            else if (mode == 2) m_mode2Sectors++;
            rawSectorValid = true;

            LOG_WARN("RawSectorToSector::processQueue(): Mode 0 and Mode 2 sectors are probably not handled correctly - consider submitting this as test data");
        }

        // Determine the sector's metadata
        SectorAddress sectorAddress(0, 0, 0);

        // If the raw sector data is valid, form a sector from it
        if (rawSectorValid) {
            // Re-fetch references in case data was modified during correction
            const std::vector<uint8_t> &finalRawData = rawSector.dataRef();
            const std::vector<uint8_t> &finalRawErrorData = rawSector.errorDataRef();
            
            // Extract the sector address data
            int32_t min = bcdToInt(finalRawData[12]);
            int32_t sec = bcdToInt(finalRawData[13]);
            int32_t frame = bcdToInt(finalRawData[14]);
            sectorAddress = SectorAddress(min, sec, frame);

            // Extract the sector mode data
            if (static_cast<uint8_t>(finalRawData[15]) == 0) mode = 0;
            else if (static_cast<uint8_t>(finalRawData[15]) == 1) mode = 1;
            else if (static_cast<uint8_t>(finalRawData[15]) == 2) mode = 2;
            else mode = -1;
        
            // Create an output sector
            Sector sector;
            sector.dataValid(rawSectorValid);
            sector.setAddress(sectorAddress);
            sector.setMode(mode);
            
            // Push only the user data to the output sector (bytes 16 to 2063 = 2KBytes data)
            // Use iterators directly to avoid temporary vectors
            std::vector<uint8_t> userData;
            std::vector<uint8_t> userErrorData;
            userData.reserve(2048);
            userErrorData.reserve(2048);
            userData.assign(finalRawData.begin() + 16, finalRawData.begin() + 16 + 2048);
            userErrorData.assign(finalRawErrorData.begin() + 16, finalRawErrorData.begin() + 16 + 2048);
            
            sector.pushData(std::move(userData));
            sector.pushErrorData(std::move(userErrorData));

            // Add the sector to the output buffer
            m_outputBuffer.push_back(sector);
        } else {
            // Sector is invalid - discard it
            m_invalidSectors++;
        }
    }
}

// Convert 1 byte BCD to integer
uint8_t RawSectorToSector::bcdToInt(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

// CRC code adapted and used under GPLv3 from:
// https://github.com/claunia/edccchk/blob/master/edccchk.c
uint32_t RawSectorToSector::crc32(const std::vector<uint8_t> &src, int32_t size)
{
    uint32_t crc = 0;
    const uint8_t *data = src.data();

    while(size--) {
        crc = (crc >> 8) ^ crc32Lut[(crc ^ (*data++)) & 0xFF];
    }

    return crc;
}

void RawSectorToSector::showStatistics() const
{
    LOG_INFO("Raw Sector to Sector (RSPC error-correction):");
    LOG_INFO("  Valid sectors: {} (corrected: {})", m_validSectors + m_correctedSectors, m_correctedSectors);
    LOG_INFO("  Invalid sectors: {}", m_invalidSectors);

    LOG_INFO("  Sector metadata:");
    LOG_INFO("    Mode 0 sectors: {}", m_mode0Sectors);
    LOG_INFO("    Mode 1 sectors: {}", m_mode1Sectors);
    LOG_INFO("    Mode 2 sectors: {}", m_mode2Sectors);
    LOG_INFO("    Invalid mode sectors: {}", m_invalidModeSectors);
}
