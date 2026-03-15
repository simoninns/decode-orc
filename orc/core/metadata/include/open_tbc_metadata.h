/*
 * File:        open_tbc_metadata.h
 * Module:      orc-metadata
 * Purpose:     Factory function for opening TBC metadata readers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <itbc_metadata_reader.h>
#include <memory>
#include <string>

namespace orc {

/**
 * @brief Open a TBC metadata reader for the given database path.
 *
 * Auto-detects the metadata format:
 *  - If @p db_path exists as a SQLite .tbc.db file, returns a TBCMetadataSqliteReader.
 *  - If @p db_path does not exist but a sibling .tbc.json file does, returns a
 *    TBCMetadataJsonReader that converts the legacy JSON on-the-fly into an in-memory
 *    SQLite database.
 *  - Otherwise returns nullptr.
 *
 * @param db_path  Expected path to the .tbc.db (SQLite) metadata file.
 * @return         An open ITBCMetadataReader, or nullptr if no metadata file was found
 *                 or conversion failed.
 */
std::unique_ptr<ITBCMetadataReader> open_tbc_metadata(const std::string& db_path);

} // namespace orc
