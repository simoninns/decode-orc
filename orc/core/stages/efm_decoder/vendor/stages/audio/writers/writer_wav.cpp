/*
 * File:        writer_wav.cpp
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "writer_wav.h"
#include "logging.h"

// This writer class writes audio data to a file in WAV format
// This is used when the output is stereo audio data

WriterWav::WriterWav() { }

WriterWav::~WriterWav()
{
    if (m_file.is_open()) {
        m_file.close();
    }
}

bool WriterWav::open(const std::string &filename)
{
    m_file.open(filename, std::ios::binary);
    if (!m_file.is_open()) {
        LOG_CRITICAL("WriterWav::open() - Could not open file {} for writing", filename);
        return false;
    }
    LOG_DEBUG("WriterWav::open() - Opened file {} for data writing", filename);

    // Add 44 bytes of blank header data to the file
    // (we will fill this in later once we know the size of the data)
    std::vector<uint8_t> header(44, 0);
    m_file.write(reinterpret_cast<const char*>(header.data()), header.size());

    return true;
}

void WriterWav::write(const AudioSection &audioSection)
{
    if (!m_file.is_open()) {
        LOG_CRITICAL("WriterWav::write() - File is not open for writing");
        return;
    }

    // Each Audio section contains 98 frames that we need to write to the output file
    for (int index = 0; index < 98; ++index) {
        Audio audio = audioSection.frame(index);
        m_file.write(reinterpret_cast<const char *>(audio.data().data()),
                     audio.frameSize() * sizeof(int16_t));
    }
}

void WriterWav::close()
{
    if (!m_file.is_open()) {
        return;
    }

    // Fill out the WAV header
    LOG_DEBUG("WriterWav::close(): Filling out the WAV header before closing the wav file");

    // WAV file header
    struct WAVHeader
    {
        char riff[4] = { 'R', 'I', 'F', 'F' };
        uint32_t chunkSize;
        char wave[4] = { 'W', 'A', 'V', 'E' };
        char fmt[4] = { 'f', 'm', 't', ' ' };
        uint32_t subchunk1Size = 16; // PCM
        uint16_t audioFormat = 1; // PCM
        uint16_t numChannels = 2; // Stereo
        uint32_t sampleRate = 44100; // 44.1kHz
        uint32_t byteRate;
        uint16_t blockAlign;
        uint16_t bitsPerSample = 16; // 16 bits
        char data[4] = { 'd', 'a', 't', 'a' };
        uint32_t subchunk2Size;
    };

    WAVHeader header;
    int64_t currentSize = m_file.tellp();
    header.chunkSize = 36 + (currentSize - 44); // Subtract header size
    header.byteRate = header.sampleRate * header.numChannels * header.bitsPerSample / 8;
    header.blockAlign = header.numChannels * header.bitsPerSample / 8;
    header.subchunk2Size = currentSize - 44; // Subtract header size

    // Move to the beginning of the file to write the header
    m_file.seekp(0, std::ios::beg);
    m_file.write(reinterpret_cast<const char *>(&header), sizeof(WAVHeader));

    // Now close the file
    m_file.close();
    LOG_DEBUG("WriterWav::close(): Closed the WAV file");
}

int64_t WriterWav::size()
{
    if (m_file.is_open()) {
        return m_file.tellp();
    }

    return 0;
}
