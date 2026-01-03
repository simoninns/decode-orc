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

#ifdef _WIN32
    #include <io.h>
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <mutex>
    // Define POSIX type for Windows if not already defined
    #ifndef _SSIZE_T_DEFINED
        using ssize_t = intptr_t;
        #define _SSIZE_T_DEFINED
    #endif
    // Windows doesn't have pread, so we need to implement it with lseek + read + mutex
    namespace {
        std::mutex windows_pread_mutex;
        ssize_t pread(int fd, void* buf, size_t count, __int64 offset) {
            std::lock_guard<std::mutex> lock(windows_pread_mutex);
            if (_lseeki64(fd, offset, SEEK_SET) == -1) {
                return -1;
            }
            return _read(fd, buf, static_cast<unsigned int>(count));
        }
    }
#else
    #include <sys/types.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace orc {

TBCReader::TBCReader()
    : fd_(-1), is_open_(false), field_count_(0), field_length_(0), 
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
    filename_ = filename;
    
    // Open file with POSIX API (thread-safe for pread)
#ifdef _WIN32
    fd_ = _open(filename.c_str(), _O_RDONLY | _O_BINARY);
#else
    fd_ = ::open(filename.c_str(), O_RDONLY);
#endif
    if (fd_ < 0) {
        return false;
    }
    
    // Get file size
#ifdef _WIN32
    struct _stat64 st;
    if (_fstat64(fd_, &st) != 0) {
        _close(fd_);
#else
    struct stat st;
    if (fstat(fd_, &st) != 0) {
        ::close(fd_);
#endif
        fd_ = -1;
        return false;
    }
    
    if (st.st_size > 0) {
        field_count_ = static_cast<size_t>(st.st_size) / field_byte_length_;
    } else {
        field_count_ = 0;
    }
    
    is_open_ = true;
    field_cache_.clear();
    
    return true;
}

void TBCReader::close() {
    // Lock cache mutex to ensure no concurrent cache access
    std::lock_guard<std::mutex> cache_lock(cache_mutex_);
    
    if (fd_ >= 0) {
#ifdef _WIN32
        _close(fd_);
#else
        ::close(fd_);
#endif
        fd_ = -1;
    }
    is_open_ = false;
    field_cache_.clear();
}

std::vector<TBCReader::sample_type> TBCReader::read_field(FieldID field_id) {
    if (!is_open_) {
        throw std::runtime_error("TBC file not open");
    }
    
    // Check cache first (cache check doesn't need file mutex)
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
    
    // Allocate buffer for field data
    auto field_data = std::make_shared<std::vector<sample_type>>(field_length_);
    
    // Calculate file position
    off_t position = static_cast<off_t>(field_index * field_byte_length_);
    
    // Use pread for thread-safe reading (no seek needed, atomic operation)
    ssize_t bytes_read = pread(fd_, field_data->data(), field_byte_length_, position);
    
    if (bytes_read < 0) {
        throw std::runtime_error("Failed to read field from file: " + filename_);
    }
    
    if (static_cast<size_t>(bytes_read) != field_byte_length_) {
        throw std::runtime_error("Short read from file: " + filename_);
    }
    
    // Cache the field (cache access has its own mutex)
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
    std::lock_guard<std::mutex> lock(cache_mutex_);
    // Simple cache eviction: if full, remove first entry
    if (field_cache_.size() >= MAX_CACHE_SIZE) {
        field_cache_.erase(field_cache_.begin());
    }
    field_cache_[field_id] = data;
}

std::shared_ptr<std::vector<TBCReader::sample_type>> TBCReader::get_cached_field(FieldID field_id) {
    std::lock_guard<std::mutex> lock(cache_mutex_);
    auto it = field_cache_.find(field_id);
    if (it != field_cache_.end()) {
        return it->second;
    }
    return nullptr;
}

} // namespace orc
