/*
 * File:        writer_sector.h
 * Module:      efm-decoder-data
 * Purpose:     EFM Data24 to data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef WRITER_SECTOR_H
#define WRITER_SECTOR_H

#include <string>
#include <fstream>
#include <cstdint>

#include "sector.h"

class WriterSector
{
public:
    WriterSector();
    ~WriterSector();

    bool open(const std::string &filename);
    void write(const Sector &sector);
    void close();
    int64_t size();
    bool isOpen() const { return m_file.is_open() || m_usingStdout; };
    bool isStdout() const;

private:
    std::ofstream m_file;
    bool m_usingStdout;
};

#endif // WRITER_SECTOR_H