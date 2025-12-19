/*
 * File:        tbc_reader.cpp
 * Module:      orc-core
 * Purpose:     Tbc Reader
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "tbc_reader.h"
#include <cstring>
#include <stdexcept>
#include <algorithm>

namespace orc {

TBCReader::TBCReader()
    : is_open_(false), field_count_(0), field_length_(0), 
      field_byte_length_(0), line_length_(0) {
}

TBCReader::~TBCReader() {
    close();
}

bool TBCReader::open(const std::string& filename, size_t field_length, size_t line_length) {
    if (is_open_) {
        close();
    }
    
    field_length_ = field_length;
    field_byte_length_ = field_length * sizeof(sample_type);
    line_length_ = line_length;
    
    file_.open(filename, std::ios::binary | std::ios::in);
    if (!file_.is_open()) {
        return false;
    }
    
    // Determine number of fields
    file_.seekg(0, std::ios::end);
    std::streampos file_size = file_.tellg();
    file_.seekg(0, std::ios::beg);
    
    if (file_size > 0) {
        field_count_ = static_cast<size_t>(file_size) / field_byte_length_;
    } else {
        field_count_ = 0;
    }
    
    is_open_ = true;
    field_cache_.clear();
    
    return true;
}

void TBCReader::close() {
    if (file_.is_open()) {
        file_.close();
    }
    is_open_ = false;
    field_cache_.clear();
}

std::vector<TBCReader::sample_type> TBCReader::read_field(FieldID field_id) {
    if (!is_open_) {
        throw std::runtime_error("TBC file not open");
    }
    
    // Check cache first
    auto cached = get_cached_field(field_id);
    if (cached) {
        return *cached;
    }
    
    // Validate field number
    if (!field_id.is_valid()) {
        throw std::invalid_argument("Invalid FieldID");
    }
    
    size_t field_index = field_id.value();
    if (field_count_ > 0 && field_index >= field_count_) {
        throw std::out_of_range("Field ID beyond end of file");
    }
    
    // Seek to field position
    std::streampos position = static_cast<std::streampos>(field_index * field_byte_length_);
    file_.seekg(position);
    
    if (!file_.good()) {
        throw std::runtime_error("Failed to seek to field position");
    }
    
    // Read field data
    auto field_data = std::make_shared<std::vector<sample_type>>(field_length_);
    file_.read(reinterpret_cast<char*>(field_data->data()), field_byte_length_);
    
    std::streamsize bytes_read = file_.gcount();
    if (bytes_read != static_cast<std::streamsize>(field_byte_length_)) {
        throw std::runtime_error("Failed to read complete field");
    }
    
    // Cache the field
    cache_field(field_id, field_data);
    
    return *field_data;
}

std::vector<TBCReader::sample_type> TBCReader::read_field_lines(
    FieldID field_id, size_t start_line, size_t end_line) {
    
    if (line_length_ == 0) {
        throw std::runtime_error("Line length not set for this TBC file");
    }
    
    // Read the entire field and extract the requested lines
    auto field_data = read_field(field_id);
    
    size_t start_sample = start_line * line_length_;
    size_t end_sample = end_line * line_length_;
    
    if (end_sample > field_data.size()) {
        throw std::out_of_range("Line range exceeds field data");
    }
    
    return std::vector<sample_type>(
        field_data.begin() + start_sample,
        field_data.begin() + end_sample
    );
}

std::vector<TBCReader::sample_type> TBCReader::read_line(FieldID field_id, size_t line_number) {
    return read_field_lines(field_id, line_number, line_number + 1);
}

void TBCReader::cache_field(FieldID field_id, std::shared_ptr<std::vector<sample_type>> data) {
    // Simple cache eviction: if full, remove first entry
    if (field_cache_.size() >= MAX_CACHE_SIZE) {
        field_cache_.erase(field_cache_.begin());
    }
    field_cache_[field_id] = data;
}

std::shared_ptr<std::vector<TBCReader::sample_type>> TBCReader::get_cached_field(FieldID field_id) {
    auto it = field_cache_.find(field_id);
    if (it != field_cache_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace orc
