/*
 * File:        writer_raw.cpp
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "writer_raw.h"
#include "logging.h"
#include <iostream>

// This writer class writes audio data to a file in raw format (no header)
// This is used when the output is stereo audio data without WAV header

WriterRaw::WriterRaw() : m_usingStdout(false) { }

WriterRaw::~WriterRaw()
{
    if (m_file.is_open()) {
        m_file.close();
    }
}

bool WriterRaw::open(const std::string &filename)
{
    if (filename == "-") {
        // Use stdout
        m_usingStdout = true;
        LOG_DEBUG("WriterRaw::open() - Using stdout for raw audio data writing");
    } else {
        // Use regular file
        m_usingStdout = false;
        m_file.open(filename, std::ios::binary);
        if (!m_file.is_open()) {
            LOG_CRITICAL("WriterRaw::open() - Could not open file {} for writing", filename);
            return false;
        }
        LOG_DEBUG("WriterRaw::open() - Opened file {} for raw audio data writing", filename);
    }

    return true;
}

void WriterRaw::write(const AudioSection &audioSection)
{
    if (!m_usingStdout && !m_file.is_open()) {
        LOG_CRITICAL("WriterRaw::write() - File is not open for writing");
        return;
    }

    // Each Audio section contains 98 frames that we need to write to the output file
    for (int index = 0; index < 98; ++index) {
        Audio audio = audioSection.frame(index);
        if (m_usingStdout) {
            std::cout.write(reinterpret_cast<const char *>(audio.data().data()),
                           audio.frameSize() * sizeof(int16_t));
        } else {
            m_file.write(reinterpret_cast<const char *>(audio.data().data()),
                        audio.frameSize() * sizeof(int16_t));
        }
    }
}

void WriterRaw::close()
{
    if (m_usingStdout) {
        std::cout.flush();
        LOG_DEBUG("WriterRaw::close(): Closed stdout");
        m_usingStdout = false;
        return;
    }
    
    if (!m_file.is_open()) {
        return;
    }

    // For raw audio, we just close the file - no header to write
    m_file.close();
    LOG_DEBUG("WriterRaw::close(): Closed the raw audio file");
}

int64_t WriterRaw::size()
{
    if (m_usingStdout) {
        // Cannot determine size when writing to stdout
        return -1;
    }
    if (m_file.is_open()) {
        return m_file.tellp();
    }

    return 0;
}

bool WriterRaw::isStdout() const
{
    return m_usingStdout;
}
