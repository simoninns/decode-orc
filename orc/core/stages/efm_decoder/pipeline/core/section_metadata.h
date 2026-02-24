/*
 * File:        section_metadata.h
 * Module:      EFM-library
 * Purpose:     Section metadata classes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef SECTION_METADATA_H
#define SECTION_METADATA_H

#include <cstdint>
#include <vector>
#include <string>
#include <fstream>

// Section time class - stores ECMA-130 frame time as minutes, seconds, and frames (1/75th of a
// second)
class SectionTime 
{
public:
    SectionTime();
    explicit SectionTime(int32_t frames);
    SectionTime(uint8_t minutes, uint8_t seconds, uint8_t frames);

    int32_t frames() const { return m_frames; }
    void setFrames(int32_t frames);
    void setTime(uint8_t minutes, uint8_t seconds, uint8_t frames);

    int32_t minutes() const { return m_frames / (75 * 60); }
    int32_t seconds() const { return (m_frames / 75) % 60; }
    int32_t frameNumber() const { return m_frames % 75; }

    std::string toString() const;
    std::vector<uint8_t> toBcd() const;

    bool operator==(const SectionTime &other) const { return m_frames == other.m_frames; }
    bool operator!=(const SectionTime &other) const { return m_frames != other.m_frames; }
    bool operator<(const SectionTime &other) const { return m_frames < other.m_frames; }
    bool operator>(const SectionTime &other) const { return m_frames > other.m_frames; }
    bool operator<=(const SectionTime &other) const { return !(*this > other); }
    bool operator>=(const SectionTime &other) const { return !(*this < other); }
    SectionTime operator+(const SectionTime &other) const
    {
        return SectionTime(m_frames + other.m_frames);
    }
    SectionTime operator-(const SectionTime &other) const
    {
        return SectionTime(m_frames - other.m_frames);
    }
    SectionTime &operator++()
    {
        ++m_frames;
        return *this;
    }
    SectionTime operator++(int)
    {
        SectionTime tmp(*this);
        m_frames++;
        return tmp;
    }
    SectionTime &operator--()
    {
        --m_frames;
        return *this;
    }
    SectionTime operator--(int)
    {
        SectionTime tmp(*this);
        m_frames--;
        return tmp;
    }

    SectionTime operator+(int frames) const
    {
        return SectionTime(m_frames + frames);
    }

    SectionTime operator-(int frames) const
    {
        return SectionTime(m_frames - frames);
    }

    friend std::istream &operator>>(std::istream &in, SectionTime &time);
    friend std::ostream &operator<<(std::ostream &out, const SectionTime &time);

private:
    int32_t m_frames;
    static uint8_t intToBcd(uint32_t value);
};

// Section type class - stores the type of section (LEAD_IN, LEAD_OUT, USER_DATA)
class SectionType
{
public:
    enum Type { LeadIn, LeadOut, UserData };

    SectionType() : m_type(UserData) { }
    explicit SectionType(Type type) : m_type(type) { }

    Type type() const { return m_type; }
    void setType(Type type) { m_type = type; }

    std::string toString() const;

    bool operator==(const SectionType &other) const { return m_type == other.m_type; }
    bool operator!=(const SectionType &other) const { return m_type != other.m_type; }

    friend std::istream &operator>>(std::istream &in, SectionType &type);
    friend std::ostream &operator<<(std::ostream &out, const SectionType &type);

private:
    Type m_type;
};

// Section metadata class - stores the Section type, Section time, absolute Section time, and track
// number This data is common for Data24, F1 and F2 Sections
class SectionMetadata
{
public:
    enum QMode { QMode1, QMode2, QMode3, QMode4 };

    SectionMetadata() :
        m_pFlag(true),
        m_qMode(QMode1),
        m_sectionType(SectionType::UserData),
        m_sectionTime(SectionTime()),
        m_absoluteSectionTime(SectionTime()),
        m_trackNumber(0),
        m_isValid(false),
        m_isRepaired(false),
        m_isAudio(true),
        m_isCopyProhibited(true),
        m_hasPreemphasis(false),
        m_is2Channel(true),
        m_upcEanCode(0),
        m_isrcCode(0)
    {}

    SectionType sectionType() const { return m_sectionType; }
    void setSectionType(const SectionType &sectionType, uint8_t trackNumber);

    SectionTime sectionTime() const { return m_sectionTime; }
    void setSectionTime(const SectionTime &sectionTime) { m_sectionTime = sectionTime; }

    SectionTime absoluteSectionTime() const { return m_absoluteSectionTime; }
    void setAbsoluteSectionTime(const SectionTime &sectionTime)
    {
        m_absoluteSectionTime = sectionTime;
    }

    uint8_t trackNumber() const { return m_trackNumber; }
    void setTrackNumber(uint8_t trackNumber);

    QMode qMode() const { return m_qMode; }
    void setQMode(QMode qMode) { m_qMode = qMode; }

    bool isAudio() const { return m_isAudio; }
    void setAudio(bool audio) { m_isAudio = audio; }
    bool isCopyProhibited() const { return m_isCopyProhibited; }
    void setCopyProhibited(bool copyProhibited) { m_isCopyProhibited = copyProhibited; }
    bool hasPreemphasis() const { return m_hasPreemphasis; }
    void setPreemphasis(bool preemphasis) { m_hasPreemphasis = preemphasis; }
    bool is2Channel() const { return m_is2Channel; }
    void set2Channel(bool is2Channel) { m_is2Channel = is2Channel; }

    void setUpcEanCode(uint32_t upcEanCode) { m_upcEanCode = upcEanCode; }
    uint32_t upcEanCode() const { return m_upcEanCode; }
    void setIsrcCode(uint32_t isrcCode) { m_isrcCode = isrcCode; }
    uint32_t isrcCode() const { return m_isrcCode; }

    bool pFlag() const { return m_pFlag; }
    void setPFlag(bool pFlag) { m_pFlag = pFlag; }

    bool isValid() const { return m_isValid; }
    void setValid(bool valid) { m_isValid = valid; }

    bool isRepaired() const { return m_isRepaired; }
    void setRepaired(bool repaired) { m_isRepaired = repaired; }

    friend std::istream &operator>>(std::istream &in, SectionMetadata &metadata);
    friend std::ostream &operator<<(std::ostream &out, const SectionMetadata &metadata);

private:
    // P-Channel metadata
    bool m_pFlag;

    // Q-Channel metadata
    QMode m_qMode;
    SectionType m_sectionType;
    SectionTime m_sectionTime;
    SectionTime m_absoluteSectionTime;
    uint8_t m_trackNumber;
    bool m_isValid;
    bool m_isRepaired;

    // Q-Channel control metadata
    bool m_isAudio;
    bool m_isCopyProhibited;
    bool m_hasPreemphasis;
    bool m_is2Channel;

    // Q-Channel mode 2 and 3 metadata
    uint32_t m_upcEanCode;
    uint32_t m_isrcCode;
};

#endif // SECTION_METADATA_H