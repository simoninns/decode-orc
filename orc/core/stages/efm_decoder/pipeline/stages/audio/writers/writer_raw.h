/*
 * File:        writer_raw.h
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef WRITER_RAW_H
#define WRITER_RAW_H

#include <string>
#include <fstream>

#include "section.h"

class WriterRaw
{
public:
    WriterRaw();
    ~WriterRaw();

    bool open(const std::string &filename);
    void write(const AudioSection &audioSection);
    void close();
    int64_t size();
    bool isOpen() const { return m_file.is_open(); };
    bool isStdout() const;

private:
    std::ofstream m_file;
    bool m_usingStdout;
};

#endif // WRITER_RAW_H