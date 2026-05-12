/*
 * File:        section_io.h
 * Purpose:     EFM-library - Binary section serialization helpers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef SECTION_IO_H
#define SECTION_IO_H

#include <cstdint>
#include <cstring>
#include <istream>
#include <ostream>
#include <vector>

#include "section.h"
#include "section_metadata.h"

namespace SectionIO {

static constexpr uint32_t kSectionIoVersion = 1;
static constexpr size_t kHeaderSize = 8;
static constexpr size_t kMetadataSize = 33; // 3×int32 + 7×uint8 + 2×uint32 + int32 + 2×uint8 = 33 bytes
static constexpr size_t kFramesPerSection = 98;

inline bool writeUint8(std::ostream &out, uint8_t value)
{
    out.write(reinterpret_cast<const char *>(&value), 1);
    return out.good();
}

inline bool readUint8(std::istream &in, uint8_t &value)
{
    in.read(reinterpret_cast<char *>(&value), 1);
    return in.good();
}

inline bool writeUint32LE(std::ostream &out, uint32_t value)
{
    uint8_t bytes[4] = {
        static_cast<uint8_t>((value >> 0) & 0xFF),
        static_cast<uint8_t>((value >> 8) & 0xFF),
        static_cast<uint8_t>((value >> 16) & 0xFF),
        static_cast<uint8_t>((value >> 24) & 0xFF)
    };
    out.write(reinterpret_cast<const char *>(bytes), 4);
    return out.good();
}

inline bool readUint32LE(std::istream &in, uint32_t &value)
{
    uint8_t bytes[4] = {0, 0, 0, 0};
    in.read(reinterpret_cast<char *>(bytes), 4);
    if (!in.good()) {
        return false;
    }
    value = (static_cast<uint32_t>(bytes[0]) << 0) |
            (static_cast<uint32_t>(bytes[1]) << 8) |
            (static_cast<uint32_t>(bytes[2]) << 16) |
            (static_cast<uint32_t>(bytes[3]) << 24);
    return true;
}

inline bool writeInt32LE(std::ostream &out, int32_t value)
{
    return writeUint32LE(out, static_cast<uint32_t>(value));
}

inline bool readInt32LE(std::istream &in, int32_t &value)
{
    uint32_t raw = 0;
    if (!readUint32LE(in, raw)) {
        return false;
    }
    value = static_cast<int32_t>(raw);
    return true;
}

inline bool writeBool(std::ostream &out, bool value)
{
    return writeUint8(out, value ? 1 : 0);
}

inline bool readBool(std::istream &in, bool &value)
{
    uint8_t raw = 0;
    if (!readUint8(in, raw)) {
        return false;
    }
    value = (raw != 0);
    return true;
}

inline bool writeHeader(std::ostream &out, const char *magic)
{
    out.write(magic, 4);
    if (!out.good()) {
        return false;
    }
    return writeUint32LE(out, kSectionIoVersion);
}

inline bool readHeader(std::istream &in, const char *magic, uint32_t &version)
{
    char buffer[4] = {0, 0, 0, 0};
    in.read(buffer, 4);
    if (!in.good()) {
        return false;
    }
    if (std::memcmp(buffer, magic, 4) != 0) {
        return false;
    }
    if (!readUint32LE(in, version)) {
        return false;
    }
    return version >= 1 && version <= kSectionIoVersion;
}

inline bool writeMetadata(std::ostream &out, const SectionMetadata &metadata)
{
    if (!writeInt32LE(out, static_cast<int32_t>(metadata.sectionType().type()))) return false;
    if (!writeInt32LE(out, metadata.sectionTime().frames())) return false;
    if (!writeInt32LE(out, metadata.absoluteSectionTime().frames())) return false;
    if (!writeUint8(out, metadata.trackNumber())) return false;
    if (!writeBool(out, metadata.isValid())) return false;
    if (!writeBool(out, metadata.isAudio())) return false;
    if (!writeBool(out, metadata.isCopyProhibited())) return false;
    if (!writeBool(out, metadata.hasPreemphasis())) return false;
    if (!writeBool(out, metadata.is2Channel())) return false;
    if (!writeBool(out, metadata.pFlag())) return false;
    if (!writeUint32LE(out, metadata.upcEanCode())) return false;
    if (!writeUint32LE(out, metadata.isrcCode())) return false;
    if (!writeInt32LE(out, static_cast<int32_t>(metadata.qMode()))) return false;
    if (!writeBool(out, metadata.isRepaired())) return false;
    return writeUint8(out, 0);
}

inline bool readMetadata(std::istream &in, SectionMetadata &metadata, uint32_t version)
{
    int32_t sectionTypeRaw = 0;
    int32_t sectionTimeFrames = 0;
    int32_t absoluteSectionTimeFrames = 0;
    uint8_t trackNumber = 0;
    bool isValid = false;
    bool isAudio = false;
    bool isCopyProhibited = false;
    bool hasPreemphasis = false;
    bool is2Channel = false;
    bool pFlag = false;
    uint32_t upcEanCode = 0;
    uint32_t isrcCode = 0;
    int32_t qModeRaw = 0;
    bool isRepaired = false;
    uint8_t reserved = 0;

    if (!readInt32LE(in, sectionTypeRaw)) return false;
    if (!readInt32LE(in, sectionTimeFrames)) return false;
    if (!readInt32LE(in, absoluteSectionTimeFrames)) return false;
    if (!readUint8(in, trackNumber)) return false;
    if (!readBool(in, isValid)) return false;
    if (!readBool(in, isAudio)) return false;
    if (!readBool(in, isCopyProhibited)) return false;
    if (!readBool(in, hasPreemphasis)) return false;
    if (!readBool(in, is2Channel)) return false;
    if (!readBool(in, pFlag)) return false;
    if (!readUint32LE(in, upcEanCode)) return false;
    if (!readUint32LE(in, isrcCode)) return false;
    if (!readInt32LE(in, qModeRaw)) return false;
    if (version >= 1) {
        if (!readBool(in, isRepaired)) return false;
        if (!readUint8(in, reserved)) return false;
    }

    metadata.setSectionType(SectionType(static_cast<SectionType::Type>(sectionTypeRaw)), trackNumber);
    metadata.setSectionTime(SectionTime(sectionTimeFrames));
    metadata.setAbsoluteSectionTime(SectionTime(absoluteSectionTimeFrames));
    metadata.setValid(isValid);
    metadata.setAudio(isAudio);
    metadata.setCopyProhibited(isCopyProhibited);
    metadata.setPreemphasis(hasPreemphasis);
    metadata.set2Channel(is2Channel);
    metadata.setPFlag(pFlag);
    metadata.setUpcEanCode(upcEanCode);
    metadata.setIsrcCode(isrcCode);
    metadata.setQMode(static_cast<SectionMetadata::QMode>(qModeRaw));
    metadata.setRepaired(isRepaired);

    return true;
}

template <typename FrameType>
inline bool writeFrame(std::ostream &out, const FrameType &frame)
{
    std::vector<uint8_t> data = frame.data();
    const std::vector<uint8_t>& errorData = frame.errorData();
    const std::vector<uint8_t>& paddedData = frame.paddedData();
    const size_t frameSize = static_cast<size_t>(frame.frameSize());

    if (data.size() != frameSize || errorData.size() != frameSize || paddedData.size() != frameSize) {
        return false;
    }

    out.write(reinterpret_cast<const char *>(data.data()), data.size());
    if (!out.good()) {
        return false;
    }

    for (size_t i = 0; i < frameSize; ++i) {
        if (!writeUint8(out, errorData[i])) return false;
    }

    for (size_t i = 0; i < frameSize; ++i) {
        if (!writeUint8(out, paddedData[i])) return false;
    }

    return true;
}

template <typename FrameType>
inline bool readFrame(std::istream &in, FrameType &frame)
{
    const size_t frameSize = static_cast<size_t>(frame.frameSize());
    std::vector<uint8_t> data(frameSize, 0);
    std::vector<uint8_t> errorData(frameSize, 0);
    std::vector<uint8_t> paddedData(frameSize, 0);

    in.read(reinterpret_cast<char *>(data.data()), frameSize);
    if (!in.good()) {
        return false;
    }

    for (size_t i = 0; i < frameSize; ++i) {
        uint8_t value = 0;
        if (!readUint8(in, value)) return false;
        errorData[i] = value;
    }

    for (size_t i = 0; i < frameSize; ++i) {
        uint8_t value = 0;
        if (!readUint8(in, value)) return false;
        paddedData[i] = value;
    }

    frame.setData(data);
    frame.setErrorData(errorData);
    frame.setPaddedData(paddedData);
    return true;
}

inline size_t f2SectionSize()
{
    return kMetadataSize + kFramesPerSection * (32 * 3);
}

inline size_t data24SectionSize()
{
    return kMetadataSize + kFramesPerSection * (24 * 3);
}

inline bool writeF2Section(std::ostream &out, const F2Section &section)
{
    if (!writeMetadata(out, section.metadata)) {
        return false;
    }
    for (size_t i = 0; i < kFramesPerSection; ++i) {
        if (!writeFrame(out, section.frame(static_cast<int>(i)))) {
            return false;
        }
    }
    return true;
}

inline bool readF2Section(std::istream &in, F2Section &section, uint32_t version)
{
    section.clear();
    SectionMetadata metadata;
    if (!readMetadata(in, metadata, version)) {
        return false;
    }
    section.metadata = metadata;
    for (size_t i = 0; i < kFramesPerSection; ++i) {
        F2Frame frame;
        if (!readFrame(in, frame)) {
            return false;
        }
        section.pushFrame(frame);
    }
    return true;
}

inline bool writeData24Section(std::ostream &out, const Data24Section &section)
{
    if (!writeMetadata(out, section.metadata)) {
        return false;
    }
    for (size_t i = 0; i < kFramesPerSection; ++i) {
        if (!writeFrame(out, section.frame(static_cast<int>(i)))) {
            return false;
        }
    }
    return true;
}

inline bool readData24Section(std::istream &in, Data24Section &section, uint32_t version)
{
    section.clear();
    SectionMetadata metadata;
    if (!readMetadata(in, metadata, version)) {
        return false;
    }
    section.metadata = metadata;
    for (size_t i = 0; i < kFramesPerSection; ++i) {
        Data24 frame;
        if (!readFrame(in, frame)) {
            return false;
        }
        section.pushFrame(frame);
    }
    return true;
}

}

#endif // SECTION_IO_H
