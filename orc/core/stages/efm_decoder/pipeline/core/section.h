/*
 * File:        section.h
 * Module:      EFM-library
 * Purpose:     EFM Section classes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef SECTION_H
#define SECTION_H

#include <vector>
#include <fstream>
#include "frame.h"
#include "audio.h"
#include "section_metadata.h"

class F2Section
{
public:
    F2Section();
    void pushFrame(const F2Frame &inFrame);
    F2Frame frame(int index) const;
    void setFrame(int index, const F2Frame &inFrame);
    bool isComplete() const;
    void clear();
    void showData();

    SectionMetadata metadata;

private:
    std::vector<F2Frame> m_frames;
};

class F1Section
{
public:
    F1Section();
    void pushFrame(const F1Frame &inFrame);
    F1Frame frame(int index) const;
    void setFrame(int index, const F1Frame &inFrame);
    bool isComplete() const;
    void clear();
    void showData();

    SectionMetadata metadata;

private:
    std::vector<F1Frame> m_frames;
};

class Data24Section
{
public:
    Data24Section();
    void pushFrame(const Data24 &inFrame);
    Data24 frame(int index) const;
    const Data24 &frameRef(int index) const;
    void setFrame(int index, const Data24 &inFrame);
    bool isComplete() const;
    void clear();
    void showData();

    SectionMetadata metadata;

private:
    std::vector<Data24> m_frames;
};

class AudioSection
{
public:
    AudioSection();
    void pushFrame(const Audio &inFrame);
    Audio frame(int index) const;
    void setFrame(int index, const Audio &inFrame);
    bool isComplete() const;
    void clear();
    void showData();

    SectionMetadata metadata;

private:
    std::vector<Audio> m_frames;
};

#endif // SECTION_H
