/*
 * File:        open_tbc_metadata.cpp
 * Module:      orc-metadata
 * Purpose:     Factory function for opening TBC metadata readers
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include <open_tbc_metadata.h>
#include <tbc_metadata.h>
#include <tbc_metadata_json_reader.h>
#include "logging.h"
#include <filesystem>

namespace orc {

std::unique_ptr<ITBCMetadataReader> open_tbc_metadata(const std::string& db_path)
{
    // Primary path: open the SQLite .tbc.db file if it exists.
    if (std::filesystem::exists(db_path)) {
        auto reader = std::make_unique<TBCMetadataSqliteReader>();
        if (!reader->open(db_path)) {
            ORC_LOG_ERROR("open_tbc_metadata: failed to open SQLite database: {}", db_path);
            return nullptr;
        }
        return reader;
    }

    // Fallback: look for a legacy .tbc.json alongside the expected .tbc.db path.
    // The db_path convention is "<stem>.tbc.db"; the JSON path is "<stem>.tbc.json".
    if (db_path.size() > 3 && db_path.compare(db_path.size() - 3, 3, ".db") == 0) {
        std::string json_path = db_path.substr(0, db_path.size() - 3) + ".json";
        if (std::filesystem::exists(json_path)) {
            ORC_LOG_WARN("open_tbc_metadata: .tbc.db not found; falling back to legacy JSON: {}", json_path);
            auto reader = std::make_unique<TBCMetadataJsonReader>();
            if (!reader->open(json_path)) {
                ORC_LOG_ERROR("open_tbc_metadata: failed to convert JSON metadata: {}", json_path);
                return nullptr;
            }
            return reader;
        }
    }

    // Neither file exists.
    ORC_LOG_ERROR("open_tbc_metadata: metadata file not found: {}", db_path);
    return nullptr;
}

} // namespace orc
