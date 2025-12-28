/*
 * File:        tbc_reader.h
 * Module:      orc-core
 * Purpose:     Tbc Reader
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include "field_id.h"
#include "video_field_representation.h"
#include <string>
#include <fstream>
#include <memory>
#include <map>
#include <mutex>

namespace orc {

/**
 * @brief Reader for TBC (Time Base Corrected) video files
 * 
 * TBC files contain raw 16-bit video samples organized as sequential fields.
 * Based on legacy ld-decode SourceVideo class.
 */
class TBCReader {
public:
    using sample_type = uint16_t;
    
    TBCReader();
    ~TBCReader();
    
    // Open a TBC file
    bool open(const std::string& filename, size_t field_length, size_t line_length = 0);
    void close();
    
    // Query file properties
    bool is_open() const { return is_open_; }
    size_t get_field_count() const { return field_count_; }
    size_t get_field_length() const { return field_length_; }
    size_t get_line_length() const { return line_length_; }
    
    // Read a complete field
    std::vector<sample_type> read_field(FieldID field_id);
    
    // Read specific lines from a field
    std::vector<sample_type> read_field_lines(FieldID field_id, size_t start_line, size_t end_line);
    
    // Read a single line from a field
    std::vector<sample_type> read_line(FieldID field_id, size_t line_number);
    
private:
    std::ifstream file_;
    bool is_open_;
    size_t field_count_;      // Total fields in file (-1 if unknown)
    size_t field_length_;     // Samples per field
    size_t field_byte_length_; // Bytes per field (field_length * 2)
    size_t line_length_;      // Samples per line (0 if not set)
    
    // Simple LRU cache for recently accessed fields
    std::map<FieldID, std::shared_ptr<std::vector<sample_type>>> field_cache_;
    static constexpr size_t MAX_CACHE_SIZE = 100;
    mutable std::mutex cache_mutex_;  // Protects field_cache_
    
    void cache_field(FieldID field_id, std::shared_ptr<std::vector<sample_type>> data);
    std::shared_ptr<std::vector<sample_type>> get_cached_field(FieldID field_id);
};

} // namespace orc
