/*
 * File:        section.cpp
 * Module:      EFM-library
 * Purpose:     EFM Section classes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "section.h"
#include "logging.h"
#include <cstdlib>

F2Section::F2Section()
{
    m_frames.reserve(98);
}

void F2Section::pushFrame(const F2Frame &inFrame)
{
    if (m_frames.size() >= 98) {
        LOG_ERROR("F2Section::pushFrame - Section is full");
        std::exit(1);
    }
    m_frames.push_back(inFrame);
}

F2Frame F2Section::frame(int32_t index) const
{
    if (index >= m_frames.size() || index < 0) {
        LOG_ERROR("F2Section::frame - Index {} out of range", index);
        std::exit(1);
    }
    return m_frames.at(index);
}

void F2Section::setFrame(int32_t index, const F2Frame &inFrame)
{
    if (index >= m_frames.size() || index < 0) {
        LOG_ERROR("F2Section::setFrame - Index {} out of range", index);
        std::exit(1);
    }
    m_frames[index] = inFrame;
}

bool F2Section::isComplete() const
{
    return m_frames.size() == 98;
}

void F2Section::clear()
{
    m_frames.clear();
}

void F2Section::showData()
{
    for (int32_t i = 0; i < m_frames.size(); ++i) {
        m_frames[i].showData();
    }
}

F1Section::F1Section()
{
    m_frames.reserve(98);
}

void F1Section::pushFrame(const F1Frame &inFrame)
{
    if (m_frames.size() >= 98) {
        LOG_ERROR("F1Section::pushFrame - Section is full");
        std::exit(1);
    }
    m_frames.push_back(inFrame);
}

F1Frame F1Section::frame(int32_t index) const
{
    if (index >= m_frames.size() || index < 0) {
        LOG_ERROR("F1Section::frame - Index {} out of range", index);
        std::exit(1);
    }
    return m_frames.at(index);
}

void F1Section::setFrame(int32_t index, const F1Frame &inFrame)
{
    if (index >= 98 || index < 0) {
        LOG_ERROR("F1Section::setFrame - Index {} out of range", index);
        std::exit(1);
    }
    m_frames[index] = inFrame;
}

bool F1Section::isComplete() const
{
    return m_frames.size() == 98;
}

void F1Section::clear()
{
    m_frames.clear();
}

void F1Section::showData()
{
    for (int32_t i = 0; i < m_frames.size(); ++i) {
        m_frames[i].showData();
    }
}

Data24Section::Data24Section()
{
    m_frames.reserve(98);
}

void Data24Section::pushFrame(const Data24 &inFrame)
{
    if (m_frames.size() >= 98) {
        LOG_ERROR("Data24Section::pushFrame - Section is full");
        std::exit(1);
    }
    m_frames.push_back(inFrame);
}

Data24 Data24Section::frame(int32_t index) const
{
    if (index >= m_frames.size() || index < 0) {
        LOG_ERROR("Data24Section::frame - Index {} out of range", index);
        std::exit(1);
    }
    return m_frames.at(index);
}

const Data24 &Data24Section::frameRef(int32_t index) const
{
    if (index >= m_frames.size() || index < 0) {
        LOG_ERROR("Data24Section::frameRef - Index {} out of range", index);
        std::exit(1);
    }
    return m_frames.at(index);
}

void Data24Section::setFrame(int32_t index, const Data24 &inFrame)
{
    if (index >= m_frames.size() || index < 0) {
        LOG_ERROR("Data24Section::setFrame - Index {} out of range", index);
        std::exit(1);
    }
    m_frames[index] = inFrame;
}

bool Data24Section::isComplete() const
{
    return m_frames.size() == 98;
}

void Data24Section::clear()
{
    m_frames.clear();
}

void Data24Section::showData()
{
    for (int32_t i = 0; i < m_frames.size(); ++i) {
        m_frames[i].showData();
    }
}

AudioSection::AudioSection()
{
    m_frames.reserve(98);
}

void AudioSection::pushFrame(const Audio &inFrame)
{
    if (m_frames.size() >= 98) {
        LOG_ERROR("AudioSection::pushFrame - Section is full");
        std::exit(1);
    }
    m_frames.push_back(inFrame);
}

Audio AudioSection::frame(int32_t index) const
{
    if (index >= m_frames.size() || index < 0) {
        LOG_ERROR("AudioSection::frame - Index {} out of range", index);
        std::exit(1);
    }
    return m_frames.at(index);
}

void AudioSection::setFrame(int32_t index, const Audio &inFrame)
{
    if (index >= m_frames.size() || index < 0) {
        LOG_ERROR("AudioSection::setFrame - Index {} out of range", index);
        std::exit(1);
    }
    m_frames[index] = inFrame;
}

bool AudioSection::isComplete() const
{
    return m_frames.size() == 98;
}

void AudioSection::clear()
{
    m_frames.clear();
}

void AudioSection::showData()
{
    for (int32_t i = 0; i < m_frames.size(); ++i) {
        m_frames[i].showData();
    }
}

// Stream write and read operators for F2Section and Data24Section
// NOTE: QDataStream operators disabled for C++17 migration
// Serialization of Section objects is not currently supported
/*
QDataStream& operator<<(QDataStream& stream, const F2Section& section)
{
    // Write metadata
    stream << section.metadata;
    
    // Write number of frames
    stream << static_cast<int32_t>(section.m_frames.size());
    
    // Write frames
    for (const auto& frame : section.m_frames) {
        stream << frame;
    }
    
    return stream;
}

QDataStream& operator>>(QDataStream& stream, F2Section& section)
{
    // Clear existing data
    section.clear();
    
    // Read metadata
    stream >> section.metadata;
    
    // Read number of frames
    int32_t frameCount;
    stream >> frameCount;
    
    // Read frames
    for (int32_t i = 0; i < frameCount; ++i) {
        F2Frame frame;
        stream >> frame;
        section.pushFrame(frame);
    }
    
    return stream;
}

QDataStream& operator<<(QDataStream& stream, const Data24Section& section)
{
    // Write metadata
    stream << section.metadata;
    
    // Write number of frames
    stream << static_cast<int32_t>(section.m_frames.size());
    
    // Write frames
    for (const auto& frame : section.m_frames) {
        stream << frame;
    }
    
    return stream;
}

QDataStream& operator>>(QDataStream& stream, Data24Section& section)
{
    // Clear existing data
    section.clear();
    
    // Read metadata
    stream >> section.metadata;
    
    // Read number of frames
    int32_t frameCount;
    stream >> frameCount;
    
    // Read frames
    for (int32_t i = 0; i < frameCount; ++i) {
        Data24 frame;
        stream >> frame;
        section.pushFrame(frame);
    }
    
    return stream;
}
*/