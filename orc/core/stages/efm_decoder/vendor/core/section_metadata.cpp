/*
 * File:        section_metadata.cpp
 * Module:      EFM-library
 * Purpose:     Frame metadata classes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "section_metadata.h"
#include "hex_utils.h"
#include "core/logging.h"
#include <cstdlib>
#include <iomanip>
#include <sstream>

// SectionType class
// ---------------------------------------------------------------------------------------------------
std::string SectionType::toString() const
{
    switch (m_type) {
    case LeadIn:
        return "LEAD_IN";
    case LeadOut:
        return "LEAD_OUT";
    case UserData:
        return "USER_DATA";
    default:
        return "UNKNOWN";
    }
}

// Stream operators for SectionType
// NOTE: QDataStream operators disabled for C++17 migration
/*
QDataStream &operator>>(QDataStream &in, SectionType &type)
{
    int32_t rawType;
    in >> rawType;
    type.setType(static_cast<SectionType::Type>(rawType));
    return in;
}

QDataStream &operator<<(QDataStream &out, const SectionType &type)
{
    out << static_cast<int32_t>(type.type());
    return out;
}
*/

// Section time class
// ---------------------------------------------------------------------------------------------------
SectionTime::SectionTime() : m_frames(0)
{
    // There are 75 frames per second, 60 seconds per minute, and 60 minutes per hour
    // so the maximum number of frames is 75 * 60 * 60 = 270000
    if (m_frames < 0 || m_frames >= 270000) {
        LOG_ERROR("SectionTime::SectionTime(): Invalid frame value of {}", m_frames);
        std::exit(1);
    }
}

SectionTime::SectionTime(int32_t frames) : m_frames(frames)
{
    if (m_frames < 0 || m_frames >= 270000) {
        LOG_ERROR("SectionTime::SectionTime(): Invalid frame value of {}", m_frames);
        std::exit(1);
    }
}

SectionTime::SectionTime(uint8_t minutes, uint8_t seconds, uint8_t frames)
{
    setTime(minutes, seconds, frames);
}

void SectionTime::setFrames(int32_t frames)
{
    if (frames < 0 || frames >= 270000) {
        LOG_ERROR("SectionTime::setFrames(): Invalid frame value of {}", frames);
        std::exit(1);
    }

    m_frames = frames;
}

void SectionTime::setTime(uint8_t minutes, uint8_t seconds, uint8_t frames)
{
    // Set the time in minutes, seconds, and frames

    // Ensure the time is sane
    if (minutes >= 60) {
        LOG_DEBUG("SectionTime::setTime(): Invalid minutes value {}, setting to 59", minutes);
        minutes = 59;
    }
    if (seconds >= 60) {
        LOG_DEBUG("SectionTime::setTime(): Invalid seconds value {}, setting to 59", seconds);
        seconds = 59;
    }
    if (frames >= 75) {
        LOG_DEBUG("SectionTime::setTime(): Invalid frames value {}, setting to 74", frames);
        frames = 74;
    }

    m_frames = (minutes * 60 + seconds) * 75 + frames;
}

std::string SectionTime::toString() const
{
    // Return the time in the format MM:SS:FF
    return HexUtils::formatTime(m_frames / (75 * 60), (m_frames / 75) % 60, m_frames % 75);
}

std::vector<uint8_t> SectionTime::toBcd() const
{
    // Return 3 bytes of BCD data representing the time as MM:SS:FF
    std::vector<uint8_t> bcd;

    uint32_t mins = m_frames / (75 * 60);
    uint32_t secs = (m_frames / 75) % 60;
    uint32_t frms = m_frames % 75;

    bcd.push_back(intToBcd(mins));
    bcd.push_back(intToBcd(secs));
    bcd.push_back(intToBcd(frms));

    return bcd;
}

uint8_t SectionTime::intToBcd(uint32_t value)
{
    if (value > 99) {
        LOG_ERROR("SectionTime::intToBcd(): Value must be in the range 0 to 99.");
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

// Stream operators for SectionTime
// NOTE: QDataStream operators disabled for C++17 migration
/*
QDataStream &operator>>(QDataStream &in, SectionTime &time)
{
    int32_t frames;
    in >> frames;
    time.setFrames(frames);
    return in;
}

QDataStream &operator<<(QDataStream &out, const SectionTime &time)
{
    out << time.frames();
    return out;
}
*/

// Section metadata class
// -----------------------------------------------------------------------------------------------
void SectionMetadata::setSectionType(const SectionType &sectionType, uint8_t trackNumber)
{
    m_trackNumber = trackNumber;
    m_sectionType = sectionType;

    // Ensure track number is sane
    if (m_sectionType.type() == SectionType::LeadIn) {
        if (m_trackNumber != 0) {
            LOG_DEBUG("SectionMetadata::setSectionType(): Setting track number to 0 for LeadIn section (was {})", m_trackNumber);
            m_trackNumber = 0;
        }
    }
    if (m_sectionType.type() == SectionType::LeadOut) {
        if (m_trackNumber != 0) {
            LOG_DEBUG("SectionMetadata::setSectionType(): Setting track number to 0 for LeadOut section (was {})", m_trackNumber);
            m_trackNumber = 0;
        }
    }
    if ((m_sectionType.type() == SectionType::UserData) && (m_trackNumber < 1 || m_trackNumber > 98)) {
        LOG_DEBUG("SectionMetadata::setSectionType(): Setting track number to 1 for UserData section (was {})", m_trackNumber);
        m_trackNumber = 1;
    }
}

void SectionMetadata::setTrackNumber(uint8_t trackNumber)
{
    m_trackNumber = trackNumber;

    // Ensure track number is sane
    if (m_sectionType.type() == SectionType::LeadIn) {
        if (m_trackNumber != 0) {
            LOG_DEBUG("SectionMetadata::setSectionType(): Setting track number to 0 for LeadIn section (was {})", m_trackNumber);
            m_trackNumber = 0;
        }
    }
    if (m_sectionType.type() == SectionType::LeadOut) {
        if (m_trackNumber != 0) {
            LOG_DEBUG("SectionMetadata::setSectionType(): Setting track number to 0 for LeadOut section (was {})", m_trackNumber);
            m_trackNumber = 0;
        }
    }
    if ((m_sectionType.type() == SectionType::UserData) && (m_trackNumber < 1 || m_trackNumber > 98)) {
        LOG_DEBUG("SectionMetadata::setSectionType(): Setting track number to 1 for UserData section (was {})", m_trackNumber);
        m_trackNumber = 1;
    }
}

// Stream operators for SectionMetadata
// NOTE: QDataStream operators disabled for C++17 migration
/*
QDataStream &operator>>(QDataStream &in, SectionMetadata &metadata)
{
    // Read section type and times
    in >> metadata.m_sectionType;
    in >> metadata.m_sectionTime;
    in >> metadata.m_absoluteSectionTime;
    
    // Read track number
    in >> metadata.m_trackNumber;
    
    // Read boolean flags
    in >> metadata.m_isValid;
    in >> metadata.m_isAudio;
    in >> metadata.m_isCopyProhibited;
    in >> metadata.m_hasPreemphasis;
    in >> metadata.m_is2Channel;
    in >> metadata.m_pFlag;

    // Read qmode 1 and 2 parameters
    in >> metadata.m_upcEanCode;
    in >> metadata.m_isrcCode;
    
    // Read Q mode
    int32_t qMode;
    in >> qMode;
    metadata.m_qMode = static_cast<SectionMetadata::QMode>(qMode);
    
    return in;
}

QDataStream &operator<<(QDataStream &out, const SectionMetadata &metadata)
{
    // Write section type and times
    out << metadata.m_sectionType;
    out << metadata.m_sectionTime;
    out << metadata.m_absoluteSectionTime;
    
    // Write track number
    out << metadata.m_trackNumber;
    
    // Write boolean flags
    out << metadata.m_isValid;
    out << metadata.m_isAudio;
    out << metadata.m_isCopyProhibited;
    out << metadata.m_hasPreemphasis;
    out << metadata.m_is2Channel;
    out << metadata.m_pFlag;

    // Write qmode 1 and 2 parameters
    out << metadata.m_upcEanCode;
    out << metadata.m_isrcCode;
    
    // Write Q mode
    out << static_cast<int32_t>(metadata.m_qMode);
    
    return out;
}
*/
