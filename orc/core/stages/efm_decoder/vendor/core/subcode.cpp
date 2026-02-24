/*
 * File:        subcode.cpp
 * Module:      EFM-library
 * Purpose:     Convert subcode data to FrameMetadata and back
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "subcode.h"
#include "hex_utils.h"
#include "logging.h"
#include <cstdlib>
#include <cstdio>

// Takes 98 bytes of subcode data and returns a FrameMetadata object
SectionMetadata Subcode::fromData(const std::vector<uint8_t> &data)
{
    // Ensure the data is 98 bytes long
    if (data.size() != 98) {
        LOG_ERROR("Subcode::fromData(): Data size of {} does not match 98 bytes", static_cast<int>(data.size()));
        std::exit(1);
    }

    // Extract the p-channel data and q-channel data
    std::vector<uint8_t> pChannelData;
    std::vector<uint8_t> qChannelData;
    pChannelData.resize(12);
    qChannelData.resize(12);

    // Note: index 0 and 1 are sync0 and sync1 bytes
    // so we get 96 bits of data per channel from 98 bytes input
    for (int index = 2; index < data.size(); ++index) {
        setBit(pChannelData, index - 2, data[index] & 0x80);
        setBit(qChannelData, index - 2, data[index] & 0x40);
    }

    // Create the SectionMetadata object
    SectionMetadata sectionMetadata;

    // Set the p-channel (p-channel is just repeating flag)
    // For correction purposes we will count the number of 0s and 1s
    // and set the flag to the majority value
    int oneCount = 0;
    for (int index = 2; index < pChannelData.size(); ++index) {
        // Count the number of bits set to 1 in pChannelData[index]
        oneCount += countBits(static_cast<uint8_t>(pChannelData[index]));
    }

    // if (oneCount != 96 && oneCount != 0) {
    //     if (m_showDebug) {
    //         tbcDebugStream() << "Subcode::fromData(): P channel data contains" << 96-oneCount << "zeros and"
    //                  << oneCount << "ones - indicating some p-channel corruption";
    //     }
    // }

    if (oneCount > (96/2)) {
        sectionMetadata.setPFlag(true);
    } else {
        sectionMetadata.setPFlag(false);
    }

    // Set the q-channel
    // If the q-channel CRC is not valid, attempt to repair the data
    if (!isCrcValid(qChannelData))
        sectionMetadata.setRepaired(repairData(qChannelData));

    if (isCrcValid(qChannelData)) {
        // Set the q-channel data from the subcode data

        // Get the address and control nybbles
        uint8_t controlNybble = qChannelData[0] >> 4;
        uint8_t modeNybble = qChannelData[0] & 0x0F;

        // Validate the mode nybble before processing
        bool validMode = (modeNybble >= 0x0 && modeNybble <= 0x4);
        
        if (!validMode) {
            // Invalid mode nybble - treat as corrupted data even though CRC passed
            LOG_DEBUG("Subcode::fromData(): Invalid Q-mode nybble! Must be 0-4, got {} - Q channel data is: {}",
                         modeNybble, HexUtils::vectorToHex(qChannelData));
            
            // Set the q-channel data to invalid and use default values
            // Extract absolute time for diagnostic purposes
            int32_t minutes = bcd2ToInt(qChannelData[7]);
            int32_t seconds = bcd2ToInt(qChannelData[8]);
            int32_t frames = bcd2ToInt(qChannelData[9]);

            if (minutes < 0) minutes = 0;
            if (minutes > 59) minutes = 59;
            if (seconds < 0) seconds = 0;
            if (seconds > 59) seconds = 59;
            if (frames < 0) frames = 0;
            if (frames > 74) frames = 74;

            SectionTime badAbsTime = SectionTime(minutes, seconds, frames);
            LOG_DEBUG("Subcode::fromData(): Potentially corrupt absolute time is: {}",
                         badAbsTime.toString());
            sectionMetadata.setValid(false);
            return sectionMetadata;
        }

        // Set the q-channel mode
        switch (modeNybble) {
        case 0x0:
            // IEC 60908 17.5.4 says to treat this as Q-mode 1
            sectionMetadata.setQMode(SectionMetadata::QMode1);
            break;
        case 0x1:
            sectionMetadata.setQMode(SectionMetadata::QMode1);
            break;
        case 0x2:
            sectionMetadata.setQMode(SectionMetadata::QMode2);
            break;
        case 0x3:
            sectionMetadata.setQMode(SectionMetadata::QMode3);
            break;
        case 0x4:
            sectionMetadata.setQMode(SectionMetadata::QMode4);
            break;
        }

        // Validate the control nybble before processing
        bool validControl = (controlNybble <= 0x4) || (controlNybble == 0x6) || 
                            (controlNybble >= 0x8 && controlNybble <= 0xB);
        
        if (!validControl) {
            // Invalid control nybble - treat as corrupted data even though CRC passed
            LOG_DEBUG("Subcode::fromData(): Invalid control nybble! Must be 0-4, 6, or 8-11, got {} - Q channel data is: {}",
                         controlNybble, HexUtils::vectorToHex(qChannelData));
            
            // Set the q-channel data to invalid
            int32_t minutes = bcd2ToInt(qChannelData[7]);
            int32_t seconds = bcd2ToInt(qChannelData[8]);
            int32_t frames = bcd2ToInt(qChannelData[9]);

            if (minutes < 0) minutes = 0;
            if (minutes > 59) minutes = 59;
            if (seconds < 0) seconds = 0;
            if (seconds > 59) seconds = 59;
            if (frames < 0) frames = 0;
            if (frames > 74) frames = 74;

            SectionTime badAbsTime = SectionTime(minutes, seconds, frames);
            LOG_DEBUG("Subcode::fromData(): Potentially corrupt absolute time is: {}",
                         badAbsTime.toString());
            sectionMetadata.setValid(false);
            return sectionMetadata;
        }

        // Set the q-channel control settings
        switch (controlNybble) {
        case 0x0:
            // AUDIO_2CH_NO_PREEMPHASIS_COPY_PROHIBITED
            sectionMetadata.setAudio(true);
            sectionMetadata.setCopyProhibited(true);
            sectionMetadata.setPreemphasis(false);
            sectionMetadata.set2Channel(true);
            break;
        case 0x1:
            // AUDIO_2CH_PREEMPHASIS_COPY_PROHIBITED
            sectionMetadata.setAudio(true);
            sectionMetadata.setCopyProhibited(true);
            sectionMetadata.setPreemphasis(true);
            sectionMetadata.set2Channel(true);
            break;
        case 0x2:
            // AUDIO_2CH_NO_PREEMPHASIS_COPY_PERMITTED
            sectionMetadata.setAudio(true);
            sectionMetadata.setCopyProhibited(false);
            sectionMetadata.setPreemphasis(false);
            sectionMetadata.set2Channel(true);
            break;
        case 0x3:
            // AUDIO_2CH_PREEMPHASIS_COPY_PERMITTED
            sectionMetadata.setAudio(true);
            sectionMetadata.setCopyProhibited(false);
            sectionMetadata.setPreemphasis(true);
            sectionMetadata.set2Channel(true);
            break;
        case 0x4:
            // DIGITAL_COPY_PROHIBITED
            sectionMetadata.setAudio(false);
            sectionMetadata.setCopyProhibited(true);
            sectionMetadata.setPreemphasis(false);
            sectionMetadata.set2Channel(true);
            break;
        case 0x6:
            // DIGITAL_COPY_PERMITTED
            sectionMetadata.setAudio(false);
            sectionMetadata.setCopyProhibited(false);
            sectionMetadata.setPreemphasis(false);
            sectionMetadata.set2Channel(true);
            break;
        case 0x8:
            // AUDIO_4CH_NO_PREEMPHASIS_COPY_PROHIBITED
            sectionMetadata.setAudio(true);
            sectionMetadata.setCopyProhibited(true);
            sectionMetadata.setPreemphasis(false);
            sectionMetadata.set2Channel(false);
            break;
        case 0x9:
            // AUDIO_4CH_PREEMPHASIS_COPY_PROHIBITED
            sectionMetadata.setAudio(true);
            sectionMetadata.setCopyProhibited(true);
            sectionMetadata.setPreemphasis(true);
            sectionMetadata.set2Channel(false);
            break;
        case 0xA:
            // AUDIO_4CH_NO_PREEMPHASIS_COPY_PERMITTED
            sectionMetadata.setAudio(true);
            sectionMetadata.setCopyProhibited(false);
            sectionMetadata.setPreemphasis(false);
            sectionMetadata.set2Channel(false);
            break;
        case 0xB:
            // AUDIO_4CH_PREEMPHASIS_COPY_PERMITTED
            sectionMetadata.setAudio(true);
            sectionMetadata.setCopyProhibited(false);
            sectionMetadata.setPreemphasis(true);
            sectionMetadata.set2Channel(false);
            break;
        }

        if (sectionMetadata.qMode() == SectionMetadata::QMode1
            || sectionMetadata.qMode() == SectionMetadata::QMode4) {
            // Get the track number
            uint8_t trackNumber = bcd2ToInt(qChannelData[1]);

            // If the track number is 0, then this is a lead-in frame
            // If the track number is 0xAA, then this is a lead-out frame
            // If the track number is 1-99, then this is a user data frame
            if (trackNumber == 0) {
                sectionMetadata.setSectionType(SectionType(SectionType::LeadIn), 0);
                LOG_DEBUG("Subcode::fromData(): Q-Mode 1/4 has track number 0 - this is a lead-in frame");
            } else if (trackNumber == 0xAA) {
                sectionMetadata.setSectionType(SectionType(SectionType::LeadOut), 0);
                LOG_DEBUG("Subcode::fromData(): Q-Mode 1/4 has track number 0xAA - this is a lead-out frame");
            } else {
                sectionMetadata.setSectionType(SectionType(SectionType::UserData), trackNumber);
            }

            // Set the frame time q_data_channel[3-5]
            // Validate BCD values to handle edge case where CRC passes but data is corrupt
            int32_t sectionMinutes = validateAndClampTimeValue(bcd2ToInt(qChannelData[3]), 59, "section minutes", sectionMetadata);
            int32_t sectionSeconds = validateAndClampTimeValue(bcd2ToInt(qChannelData[4]), 59, "section seconds", sectionMetadata);
            int32_t sectionFrames = validateAndClampTimeValue(bcd2ToInt(qChannelData[5]), 74, "section frames", sectionMetadata);
            sectionMetadata.setSectionTime(SectionTime(sectionMinutes, sectionSeconds, sectionFrames));

            // Set the zero byte q_data_channel[6] - Not used at the moment

            // Set the ap time q_data_channel[7-9]
            // Validate BCD values to handle edge case where CRC passes but data is corrupt
            int32_t absMinutes = validateAndClampTimeValue(bcd2ToInt(qChannelData[7]), 59, "absolute minutes", sectionMetadata);
            int32_t absSeconds = validateAndClampTimeValue(bcd2ToInt(qChannelData[8]), 59, "absolute seconds", sectionMetadata);
            int32_t absFrames = validateAndClampTimeValue(bcd2ToInt(qChannelData[9]), 74, "absolute frames", sectionMetadata);
            sectionMetadata.setAbsoluteSectionTime(SectionTime(absMinutes, absSeconds, absFrames));
        } else if (sectionMetadata.qMode() == SectionMetadata::QMode2) {
            // Extract the 52 bit UPC/EAN code
            // This is a 13 digit BCD code, so we need to convert it to an integer
            uint64_t upc = 0;
            for (int i = 0; i < 13; ++i) {
                upc *= 10;
                upc += bcd2ToInt(qChannelData[i + 1]);
            }
            sectionMetadata.setUpcEanCode(upc);
            // Show the UPC/EAN code as 13 digits padded with leading zeros
            std::string upcString = std::to_string(upc);
            while (upcString.size() < 13) {
                upcString = "0" + upcString;
            }

            LOG_DEBUG("Subcode::fromData(): Q-Mode 2 has UPC/EAN code of: {}", upcString);

            // Only the absolute frame number is included for Q mode 2
            sectionMetadata.setSectionType(SectionType(SectionType::UserData), 1);
            sectionMetadata.setSectionTime(SectionTime(0, 0, 0));
            
            // Validate BCD value to handle edge case where CRC passes but data is corrupt
            int32_t absFrames = validateAndClampTimeValue(bcd2ToInt(qChannelData[9]), 74, "absolute frames (QMode2)", sectionMetadata);
            sectionMetadata.setAbsoluteSectionTime(SectionTime(0, 0, absFrames));
        } else if (sectionMetadata.qMode() == SectionMetadata::QMode3) {
            // There is no test data for this qmode, so this is untested
            LOG_WARN("Subcode::fromData(): Q-Mode 3 metadata is present on this disc.  This is untested.");
            LOG_ERROR("Subcode::fromData(): Please submit this data for testing - ask in Discord/IRC");
            std::exit(1);

            // Only the absolute frame number is included for Q mode 3
            sectionMetadata.setSectionType(SectionType(SectionType::UserData), 1);
            sectionMetadata.setSectionTime(SectionTime(0, 0, 0));
            
            // Validate BCD value to handle edge case where CRC passes but data is corrupt
            int32_t absFrames = validateAndClampTimeValue(bcd2ToInt(qChannelData[9]), 74, "absolute frames (QMode3)", sectionMetadata);
            sectionMetadata.setAbsoluteSectionTime(SectionTime(0, 0, absFrames));
        } else {
            LOG_ERROR("Subcode::fromData(): Invalid Q-mode {}", static_cast<int>(sectionMetadata.qMode()));
            std::exit(1);
        }

        sectionMetadata.setValid(true);
    } else {
        // Set the q-channel data to invalid leaving the rest of
        // the metadata as default values
        char crcBuf1[32], crcBuf2[32];
        snprintf(crcBuf1, sizeof(crcBuf1), "%x", static_cast<int>(getQChannelCrc(qChannelData)));
        snprintf(crcBuf2, sizeof(crcBuf2), "%x", static_cast<int>(calculateQChannelCrc16(qChannelData)));
        LOG_DEBUG("Subcode::fromData(): Invalid CRC in Q-channel data - expected: {} calculated: {}",
                     crcBuf1, crcBuf2);

        // Range check the absolute time - as it is potentially corrupt and could be out of range
        int32_t minutes = bcd2ToInt(qChannelData[7]);
        int32_t seconds = bcd2ToInt(qChannelData[8]);
        int32_t frames = bcd2ToInt(qChannelData[9]);

        if (minutes < 0) minutes = 0;
        if (minutes > 59) minutes = 59;
        if (seconds < 0) seconds = 0;
        if (seconds > 59) seconds = 59;
        if (frames < 0) frames = 0;
        if (frames > 74) frames = 74;

        SectionTime badAbsTime = SectionTime(minutes, seconds, frames);
        LOG_DEBUG("Subcode::fromData(): Q channel data is: {} potentially corrupt absolute time is: {}",
                     HexUtils::vectorToHex(qChannelData),
                     badAbsTime.toString());
        sectionMetadata.setValid(false);
    }

    // Sanity check the track number and frame type
    //
    // If the track number is 0, then this is a lead-in frame
    // If the track number is 0xAA, then this is a lead-out frame
    // If the track number is 1-99, then this is a user data frame
    if (sectionMetadata.trackNumber() == 0
        && sectionMetadata.sectionType().type() != SectionType::LeadIn) {
        LOG_DEBUG("Subcode::fromData(): Track number 0 is only valid for lead-in frames");
    } else if (sectionMetadata.trackNumber() == 0xAA
               && sectionMetadata.sectionType().type() != SectionType::LeadOut) {
        LOG_DEBUG("Subcode::fromData(): Track number 0xAA is only valid for lead-out frames");
    } else if (sectionMetadata.trackNumber() > 99) {
        LOG_DEBUG("Subcode::fromData(): Track number {} is out of range", sectionMetadata.trackNumber());
    }

    if (sectionMetadata.isRepaired()) {
        LOG_DEBUG("Subcode::fromData(): Q-channel repaired for section with absolute time: {} track number: {} and section time: {}",
                     sectionMetadata.absoluteSectionTime().toString(),
                     sectionMetadata.trackNumber(),
                     sectionMetadata.sectionTime().toString());
    }

    // All done!
    return sectionMetadata;
}

uint8_t Subcode::countBits(uint8_t byteValue)
{
    uint8_t count = 0;
    for (int i = 0; i < 8; ++i) {
        if (byteValue & (1 << i))
            count++;
    }
    return count;
}

// Takes a FrameMetadata object and returns 98 bytes of subcode data
std::vector<uint8_t> Subcode::toData(const SectionMetadata &sectionMetadata)
{
    std::vector<uint8_t> pChannelData(12, 0);
    std::vector<uint8_t> qChannelData(12, 0);

    // Set the p-channel data
    for (int i = 0; i < 12; ++i) {
        if (sectionMetadata.pFlag())
            pChannelData[i] = 0xFF;
        else
            pChannelData[i] = 0x00;
    }

    // Create the control and address nybbles
    uint8_t controlNybble = 0;
    uint8_t modeNybble = 0;

    switch (sectionMetadata.qMode()) {
    case SectionMetadata::QMode1:
        modeNybble = 0x1; // 0b0001
        break;
    case SectionMetadata::QMode2:
        modeNybble = 0x2; // 0b0010
        break;
    case SectionMetadata::QMode3:
        modeNybble = 0x3; // 0b0011
        break;
    case SectionMetadata::QMode4:
        modeNybble = 0x4; // 0b0100
        break;
    default:
        LOG_ERROR("Subcode::toData(): Invalid Q-mode {}", static_cast<int>(sectionMetadata.qMode()));
        std::exit(1);
    }

    bool audio = sectionMetadata.isAudio();
    bool copyProhibited = sectionMetadata.isCopyProhibited();
    bool preemphasis = sectionMetadata.hasPreemphasis();
    bool channels2 = sectionMetadata.is2Channel();

    // These are the valid combinations of control nybble flags
    if (audio && channels2 && !preemphasis && copyProhibited)
        controlNybble = 0x0; // 0b0000 = AUDIO_2CH_NO_PREEMPHASIS_COPY_PROHIBITED
    else if (audio && channels2 && preemphasis && copyProhibited)
        controlNybble = 0x1; // 0b0001 = AUDIO_2CH_PREEMPHASIS_COPY_PROHIBITED
    else if (audio && channels2 && !preemphasis && !copyProhibited)
        controlNybble = 0x2; // 0b0010 = AUDIO_2CH_NO_PREEMPHASIS_COPY_PERMITTED
    else if (audio && channels2 && preemphasis && !copyProhibited)
        controlNybble = 0x3; // 0b0011 = AUDIO_2CH_PREEMPHASIS_COPY_PERMITTED
    else if (!audio && copyProhibited)
        controlNybble = 0x4; // 0b0100 = DIGITAL_COPY_PROHIBITED
    else if (!audio && !copyProhibited)
        controlNybble = 0x6; // 0b0110 = DIGITAL_COPY_PERMITTED
    else if (audio && !channels2 && !preemphasis && copyProhibited)
        controlNybble = 0x8; // 0b1000 = AUDIO_4CH_NO_PREEMPHASIS_COPY_PROHIBITED
    else if (audio && !channels2 && preemphasis && copyProhibited)
        controlNybble = 0x9; // 0b1001 = AUDIO_4CH_PREEMPHASIS_COPY_PROHIBITED
    else if (audio && !channels2 && !preemphasis && !copyProhibited)
        controlNybble = 0xA; // 0b1010 = AUDIO_4CH_NO_PREEMPHASIS_COPY_PERMITTED
    else if (audio && !channels2 && preemphasis && !copyProhibited)
        controlNybble = 0xB; // 0b1011 = AUDIO_4CH_PREEMPHASIS_COPY_PERMITTED
    else {
        LOG_ERROR("Subcode::toData(): Invalid control nybble! Must be 0-3, 4-7 or 8-11");
        std::exit(1);
    }

    // The Q-channel data is constructed from the Q-mode (4 bits) and control bits (4 bits)
    // Q-mode is 0-3 and control is 4-7
    qChannelData[0] = controlNybble << 4 | modeNybble;

    // Get the frame metadata
    SectionType frameType = sectionMetadata.sectionType();
    SectionTime fTime = sectionMetadata.sectionTime();
    SectionTime apTime = sectionMetadata.absoluteSectionTime();
    uint8_t trackNumber = sectionMetadata.trackNumber();

    // Sanity check the track number and frame type
    //
    // If the track number is 0, then this is a lead-in frame
    // If the track number is 0xAA, then this is a lead-out frame
    // If the track number is 1-99, then this is a user data frame
    if (trackNumber == 0 && frameType.type() != SectionType::LeadIn) {
        LOG_ERROR("Subcode::toData(): Track number 0 is only valid for lead-in frames");
        std::exit(1);
    } else if (trackNumber == 0xAA && frameType.type() != SectionType::LeadOut) {
        LOG_ERROR("Subcode::toData(): Track number 0xAA is only valid for lead-out frames");
        std::exit(1);
    } else if (trackNumber > 99) {
        LOG_ERROR("Subcode::toData(): Track number {} is out of range", trackNumber);
        std::exit(1);
    }

    // Set the Q-channel data
    if (frameType.type() == SectionType::LeadIn) {
        uint16_t tno = 0x00;
        uint16_t pointer = 0x00;
        uint8_t zero = 0;

        qChannelData[1] = tno;
        qChannelData[2] = pointer;
        qChannelData[3] = fTime.toBcd()[0];
        qChannelData[4] = fTime.toBcd()[1];
        qChannelData[5] = fTime.toBcd()[2];
        qChannelData[6] = zero;
        qChannelData[7] = apTime.toBcd()[0];
        qChannelData[8] = apTime.toBcd()[1];
        qChannelData[9] = apTime.toBcd()[2];
        }

        if (frameType.type() == SectionType::UserData) {
        uint8_t tno = intToBcd2(trackNumber);
        uint8_t index = 01; // Not correct?
        uint8_t zero = 0;

        qChannelData[1] = tno;
        qChannelData[2] = index;
        qChannelData[3] = fTime.toBcd()[0];
        qChannelData[4] = fTime.toBcd()[1];
        qChannelData[5] = fTime.toBcd()[2];
        qChannelData[6] = zero;
        qChannelData[7] = apTime.toBcd()[0];
        qChannelData[8] = apTime.toBcd()[1];
        qChannelData[9] = apTime.toBcd()[2];
        }

        if (frameType.type() == SectionType::LeadOut) {
        uint16_t tno = 0xAA; // Hexadecimal AA for lead-out
        uint16_t index = 01; // Must be 01 for lead-out
        uint8_t zero = 0;

        qChannelData[1] = tno;
        qChannelData[2] = index;
        qChannelData[3] = fTime.toBcd()[0];
        qChannelData[4] = fTime.toBcd()[1];
        qChannelData[5] = fTime.toBcd()[2];
        qChannelData[6] = zero;
        qChannelData[7] = apTime.toBcd()[0];
        qChannelData[8] = apTime.toBcd()[1];
        qChannelData[9] = apTime.toBcd()[2];
    }

    // Set the CRC
    setQChannelCrc(qChannelData); // Sets data[10] and data[11]

    // Now we need to convert the p-channel and q-channel data into a 98 byte array
    std::vector<uint8_t> data;
    data.resize(98);
    data[0] = 0x00; // Sync0
    data[1] = 0x00; // Sync1

    for (int index = 2; index < 98; ++index) {
        uint8_t m_subcodeByte = 0x00;
        if (getBit(pChannelData, index - 2))
            m_subcodeByte |= 0x80;
        if (getBit(qChannelData, index - 2))
            m_subcodeByte |= 0x40;
        data[index] = m_subcodeByte;
    }

    return data;
}

// Set a bit in a byte array
void Subcode::setBit(std::vector<uint8_t> &data, uint8_t bitPosition, bool value)
{
    // Check to ensure the bit position is valid
    if (bitPosition >= data.size() * 8) {
        LOG_ERROR("Subcode::setBit(): Bit position {} is out of range for data size {}",
                     bitPosition, static_cast<int>(data.size()));
        std::exit(1);
    }

    // We need to convert this to a byte number and bit number within that byte
    uint8_t byteNumber = bitPosition / 8;
    uint8_t bitNumber = 7 - (bitPosition % 8);

    // Set the bit
    if (value) {
        data[byteNumber] = static_cast<uint8_t>(data[byteNumber] | (1 << bitNumber)); // Set bit
    } else {
        data[byteNumber] = static_cast<uint8_t>(data[byteNumber] & ~(1 << bitNumber)); // Clear bit
    }
}

// Get a bit from a byte array
bool Subcode::getBit(const std::vector<uint8_t> &data, uint8_t bitPosition)
{
    // Check to ensure we don't overflow the data array
    if (bitPosition >= data.size() * 8) {
        LOG_ERROR("Subcode::getBit(): Bit position {} is out of range for data size {}",
                     bitPosition, static_cast<int>(data.size()));
        std::exit(1);
    }

    // We need to convert this to a byte number and bit number within that byte
    uint8_t byteNumber = bitPosition / 8;
    uint8_t bitNumber = 7 - (bitPosition % 8);

    // Get the bit
    return (data[byteNumber] & (1 << bitNumber)) != 0;
}

bool Subcode::isCrcValid(std::vector<uint8_t> qChannelData)
{
    // Get the CRC from the data
    uint16_t dataCrc = getQChannelCrc(qChannelData);

    // Calculate the CRC
    uint16_t calculatedCrc = calculateQChannelCrc16(qChannelData);

    // Check if the CRC is valid
    return dataCrc == calculatedCrc;
}

uint16_t Subcode::getQChannelCrc(std::vector<uint8_t> qChannelData)
{
    // Get the CRC from the data
    return static_cast<uint16_t>(static_cast<uint8_t>(qChannelData[10]) << 8
                                 | static_cast<uint8_t>(qChannelData[11]));
}

void Subcode::setQChannelCrc(std::vector<uint8_t> &qChannelData)
{
    // Calculate the CRC
    uint16_t calculatedCrc = calculateQChannelCrc16(qChannelData);

    // Set the CRC in the data
    qChannelData[10] = static_cast<uint8_t>(calculatedCrc >> 8);
    qChannelData[11] = static_cast<uint8_t>(calculatedCrc & 0xFF);
}

// Generate a 16-bit CRC for the subcode data
// Adapted from http://mdfs.net/Info/Comp/Comms/CRC16.htm
uint16_t Subcode::calculateQChannelCrc16(const std::vector<uint8_t> &qChannelData)
{
    std::size_t i;
    uint32_t crc = 0;

    // Create a copy excluding the last 2 bytes
    std::vector<uint8_t> data(qChannelData.begin(), qChannelData.end() - 2);

    for (int pos = 0; pos < data.size(); ++pos) {
        crc = crc ^ static_cast<uint32_t>(static_cast<uint8_t>(data[pos]) << 8);
        for (i = 0; i < 8; ++i) {
            crc = crc << 1;
            if (crc & 0x10000)
                crc = (crc ^ 0x1021) & 0xFFFF;
        }
    }

    // Invert the CRC
    crc = ~crc & 0xFFFF;

    return static_cast<uint16_t>(crc);
}

// Because of the way Q-channel data is spread over many frames, the most
// likely cause of a CRC error is a single bit error in the data. We can
// attempt to repair the data by flipping each bit in turn and checking
// the CRC.
//
// Perhaps there is some more effective way to repair the data, but this
// will do for now.
bool Subcode::repairData(std::vector<uint8_t> &qChannelData)
{
    std::vector<uint8_t> dataCopy = qChannelData;

    // 96-16 = Don't repair CRC bits
    for (int i = 0; i < 96 - 16; ++i) {
        dataCopy = qChannelData;
        dataCopy[i / 8] = static_cast<uint8_t>(dataCopy[i / 8] ^ (1 << (7 - (i % 8))));

        if (isCrcValid(dataCopy)) {
            qChannelData = dataCopy;
            return true;
        }
    }

    return false;
}

// Convert integer to BCD (Binary Coded Decimal)
// Output is always 2 nybbles (00-99)
uint8_t Subcode::intToBcd2(uint8_t value)
{
    if (value > 99) {
        LOG_ERROR("Subcode::intToBcd2(): Value must be in the range 0 to 99. Got {}", value);
        std::exit(1);
    }

    uint16_t bcd = 0;
    uint16_t factor = 1;

    while (value > 0) {
        bcd += (value % 10) * factor;
        value /= 10;
        factor *= 16;
    }

    // Ensure the result is always 2 bytes (00-99)
    return bcd & 0xFF;
}

// Validate and clamp time component values, marking section as repaired if needed
// Returns the clamped value
int32_t Subcode::validateAndClampTimeValue(int32_t value, int32_t maxValue, const std::string &valueName, 
                                          SectionMetadata &sectionMetadata)
{
    if (value > maxValue) {
        LOG_DEBUG("Subcode::validateAndClampTimeValue(): Invalid {} value {} - marking section as repaired",
                     valueName, value);
        sectionMetadata.setRepaired(true);
        return maxValue;
    }
    return value;
}

// Convert BCD (Binary Coded Decimal) to integer
uint8_t Subcode::bcd2ToInt(uint8_t bcd)
{
    uint16_t value = 0;
    uint16_t factor = 1;

    // Check for the lead out track exception of 0xAA
    // (See ECMA-130 22.3.3.1)
    if (bcd == 0xAA) {
        return 0xAA;
    }

    while (bcd > 0) {
        value += (bcd & 0x0F) * factor;
        bcd >>= 4;
        factor *= 10;
    }

    return value;
}
