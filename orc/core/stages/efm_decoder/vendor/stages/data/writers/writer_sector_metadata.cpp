/*
 * File:        writer_sector_metadata.cpp
 * Module:      efm-decoder-data
 * Purpose:     EFM Data24 to data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "writer_sector_metadata.h"
#include <logging.h>
#include <fmt/format.h>

// This writer class writes metadata about sector data to a file

WriterSectorMetadata::WriterSectorMetadata()
{}

WriterSectorMetadata::~WriterSectorMetadata()
{
    if (m_file.is_open()) {
        m_file.close();
    }
}

bool WriterSectorMetadata::open(const std::string &filename)
{
    m_file.open(filename);
    if (!m_file.is_open()) {
        LOG_CRITICAL("WriterSectorMetadata::open() - Could not open file {} for writing", filename);
        return false;
    }
    LOG_DEBUG("WriterSectorMetadata::open() - Opened file {} for metadata writing", filename);

    return true;
}

void WriterSectorMetadata::write(const Sector &sector)
{
    if (!m_file.is_open()) {
        LOG_CRITICAL("WriterSectorMetadata::write() - File is not open for writing");
        return;
    }

    // If the sector is not valid, write a metadata entry for it
    if (!sector.isDataValid()) {
        // Write a metadata entry for the sector
        m_file << sector.address().address() << "\n";
    }
}

void WriterSectorMetadata::close()
{
    if (!m_file.is_open()) {
        return;
    }

    m_file.close();
    LOG_DEBUG("WriterSectorMetadata::close(): Closed the bad sector map metadata file");
}

int64_t WriterSectorMetadata::size()
{
    if (m_file.is_open()) {
        auto currentPos = m_file.tellp();
        return static_cast<int64_t>(currentPos);
    }

    return 0;
}
