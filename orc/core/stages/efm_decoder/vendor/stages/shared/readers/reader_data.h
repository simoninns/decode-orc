/*
 * File:        reader_data.h
 * Module:      efm-decoder-f2
 * Purpose:     EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef READER_DATA_H
#define READER_DATA_H

#include <string>
#include <vector>
#include <fstream>
#include <cstdint>

class ReaderData
{
public:
    ReaderData();
    ~ReaderData();

    bool open(const std::string &filename);
    std::vector<uint8_t> read(uint32_t chunkSize);
    void close();
    int64_t size() const;
    bool isStdin() const;

private:
    std::ifstream m_file;
    bool m_usingStdin;
    int64_t m_fileSize;
};

#endif // READER_DATA_H