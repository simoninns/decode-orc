/*
 * File:        writer_wav.h
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef WRITER_WAV_H
#define WRITER_WAV_H

#include <string>
#include <fstream>

#include "section.h"

class WriterWav
{
public:
    WriterWav();
    ~WriterWav();

    bool open(const std::string &filename);
    void write(const AudioSection &audioSection);
    void close();
    int64_t size();
    bool isOpen() const { return m_file.is_open(); };

private:
    std::ofstream m_file;
};

#endif // WRITER_WAV_H