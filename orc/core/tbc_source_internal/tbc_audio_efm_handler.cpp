/*
 * File:        tbc_audio_efm_handler.cpp
 * Module:      orc-core
 * Purpose:     Shared audio/EFM handling for TBC sources
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "tbc_audio_efm_handler.h"
#include "../include/logging.h"

namespace orc {

TBCAudioEFMHandler::TBCAudioEFMHandler(MetadataProvider* provider)
    : provider_(provider),
      has_audio_(false),
      has_efm_(false),
      has_ac3_rf_(false)
{
}

// ============================================================================
// Audio interface implementation
// ============================================================================

uint32_t TBCAudioEFMHandler::get_audio_sample_count(FieldID id) const {
    if (!has_audio_) {
        return 0;
    }
    
    // Get audio sample count from field metadata
    auto metadata = provider_->get_field_metadata(id);
    if (!metadata || !metadata->audio_samples) {
        return 0;
    }
    
    return static_cast<uint32_t>(metadata->audio_samples.value());
}

std::vector<int16_t> TBCAudioEFMHandler::get_audio_samples(FieldID id) const {
    if (!has_audio_) {
        return {};
    }
    
    // Get metadata with precomputed offsets
    auto metadata = provider_->get_field_metadata(id);
    if (!metadata || !metadata->audio_byte_start || !metadata->audio_byte_end) {
        return {};
    }
    
    uint64_t start_offset = metadata->audio_byte_start.value();
    uint64_t end_offset = metadata->audio_byte_end.value();
    uint64_t byte_count = end_offset - start_offset;
    
    if (byte_count == 0) {
        return {};
    }
    
    // Calculate sample count (stereo 16-bit = 4 bytes per sample pair)
    size_t sample_count = byte_count / 2;  // Total int16 values (L+R interleaved)
    
    // Read audio data using buffered reader
    std::lock_guard<std::mutex> lock(audio_mutex_);
    
    if (!pcm_audio_reader_ || !pcm_audio_reader_->is_open()) {
        ORC_LOG_WARN("TBCAudioEFMHandler: PCM audio file not open");
        return {};
    }
    
    try {
        return pcm_audio_reader_->read(start_offset, sample_count);
    } catch (const std::exception& e) {
        ORC_LOG_WARN("TBCAudioEFMHandler: Failed to read audio for field {}: {}", 
                    id.value(), e.what());
        return {};
    }
}

bool TBCAudioEFMHandler::set_audio_file(const std::string& pcm_path) {
    if (pcm_path.empty()) {
        has_audio_ = false;
        return true;
    }
    
    std::lock_guard<std::mutex> lock(audio_mutex_);
    
    // Create and open buffered reader (4MB buffer)
    pcm_audio_reader_ = std::make_unique<BufferedFileReader<int16_t>>(4 * 1024 * 1024);
    
    if (!pcm_audio_reader_->open(pcm_path)) {
        ORC_LOG_ERROR("TBCAudioEFMHandler: Failed to open PCM audio file: {}", pcm_path);
        has_audio_ = false;
        pcm_audio_reader_.reset();
        return false;
    }
    
    // Validate PCM file size matches metadata expectations
    uint64_t actual_file_size = pcm_audio_reader_->file_size();
    
    // Calculate expected file size from metadata
    uint64_t expected_samples = 0;
    auto field_range = provider_->field_range();
    
    for (FieldID fid = field_range.start; fid < field_range.end; ++fid) {
        auto metadata = provider_->get_field_metadata(fid);
        if (metadata && metadata->audio_samples) {
            expected_samples += metadata->audio_samples.value();
        }
    }
    
    // Each sample is 2 channels * 2 bytes (16-bit signed stereo)
    uint64_t expected_file_size = expected_samples * 4;
    
    // Always log the comparison for debugging
    uint64_t actual_samples = actual_file_size / 4;
    ORC_LOG_DEBUG("  PCM file size: {} bytes ({} samples)", actual_file_size, actual_samples);
    ORC_LOG_DEBUG("  Expected from metadata: {} samples ({} bytes)", expected_samples, expected_file_size);
    
    if (actual_file_size != expected_file_size) {
        ORC_LOG_ERROR("PCM audio file size mismatch!");
        ORC_LOG_ERROR("  PCM file: {}", pcm_path);
        ORC_LOG_ERROR("  File contains {} bytes ({} samples)", actual_file_size, actual_samples);
        ORC_LOG_ERROR("  Metadata specifies {} samples ({} bytes expected)", 
                     expected_samples, expected_file_size);
        ORC_LOG_ERROR("  The PCM file and metadata are inconsistent.");
        ORC_LOG_ERROR("  This file may be corrupted, truncated, or not match the TBC metadata.");
        pcm_audio_reader_.reset();
        has_audio_ = false;
        return false;
    }
    
    ORC_LOG_DEBUG("TBCAudioEFMHandler: Opened PCM audio file: {}", pcm_path);
    ORC_LOG_DEBUG("  PCM validation passed: {} samples match metadata", expected_samples);
    
    // Compute cumulative byte offsets for O(1) access
    compute_audio_offsets();
    
    pcm_audio_path_ = pcm_path;
    has_audio_ = true;
    
    return true;
}

// ============================================================================
// EFM interface implementation
// ============================================================================

uint32_t TBCAudioEFMHandler::get_efm_sample_count(FieldID id) const {
    if (!has_efm_) {
        return 0;
    }
    
    // Get EFM sample count from field metadata
    auto metadata = provider_->get_field_metadata(id);
    if (!metadata || !metadata->efm_t_values) {
        return 0;
    }
    
    return static_cast<uint32_t>(metadata->efm_t_values.value());
}

std::vector<uint8_t> TBCAudioEFMHandler::get_efm_samples(FieldID id) const {
    if (!has_efm_) {
        return {};
    }
    
    // Get metadata with precomputed offsets
    auto metadata = provider_->get_field_metadata(id);
    if (!metadata || !metadata->efm_byte_start || !metadata->efm_byte_end) {
        return {};
    }
    
    uint64_t start_offset = metadata->efm_byte_start.value();
    uint64_t end_offset = metadata->efm_byte_end.value();
    uint64_t byte_count = end_offset - start_offset;
    
    if (byte_count == 0) {
        return {};
    }
    
    // Read EFM data using buffered reader
    std::vector<uint8_t> samples;
    
    {
        std::lock_guard<std::mutex> lock(efm_mutex_);
        
        if (!efm_data_reader_ || !efm_data_reader_->is_open()) {
            ORC_LOG_WARN("TBCAudioEFMHandler: EFM data file not open");
            return {};
        }
        
        try {
            samples = efm_data_reader_->read(start_offset, byte_count);
        } catch (const std::exception& e) {
            ORC_LOG_WARN("TBCAudioEFMHandler: Failed to read EFM for field {}: {}", 
                        id.value(), e.what());
            return {};
        }
    }
    
    // Validate t-values are in range [3, 11]
    for (size_t i = 0; i < samples.size(); ++i) {
        if (samples[i] < 3 || samples[i] > 11) {
            ORC_LOG_WARN("TBCAudioEFMHandler: Invalid EFM t-value {} at index {} for field {}", 
                        samples[i], i, id.value());
        }
    }
    
    return samples;
}

bool TBCAudioEFMHandler::set_efm_file(const std::string& efm_path) {
    if (efm_path.empty()) {
        has_efm_ = false;
        return true;
    }
    
    std::lock_guard<std::mutex> lock(efm_mutex_);
    
    // Create and open buffered reader (4MB buffer)
    efm_data_reader_ = std::make_unique<BufferedFileReader<uint8_t>>(4 * 1024 * 1024);
    
    if (!efm_data_reader_->open(efm_path)) {
        ORC_LOG_ERROR("TBCAudioEFMHandler: Failed to open EFM data file: {}", efm_path);
        has_efm_ = false;
        efm_data_reader_.reset();
        return false;
    }
    
    // Validate EFM file size matches metadata expectations
    uint64_t actual_file_size = efm_data_reader_->file_size();
    
    // Calculate expected file size from metadata
    uint64_t expected_tvalues = 0;
    auto field_range = provider_->field_range();
    
    for (FieldID fid = field_range.start; fid < field_range.end; ++fid) {
        auto metadata = provider_->get_field_metadata(fid);
        if (metadata && metadata->efm_t_values) {
            expected_tvalues += metadata->efm_t_values.value();
        }
    }
    
    // Each t-value is 1 byte
    uint64_t expected_file_size = expected_tvalues;
    
    // Always log the comparison for debugging
    ORC_LOG_DEBUG("  EFM file size: {} bytes ({} t-values)", actual_file_size, actual_file_size);
    ORC_LOG_DEBUG("  Expected from metadata: {} t-values ({} bytes)", expected_tvalues, expected_file_size);
    
    if (actual_file_size != expected_file_size) {
        ORC_LOG_ERROR("EFM data file size mismatch!");
        ORC_LOG_ERROR("  EFM file: {}", efm_path);
        ORC_LOG_ERROR("  File contains {} bytes ({} t-values)", actual_file_size, actual_file_size);
        ORC_LOG_ERROR("  Metadata specifies {} t-values ({} bytes expected)", 
                     expected_tvalues, expected_file_size);
        ORC_LOG_ERROR("  The EFM file and metadata are inconsistent.");
        ORC_LOG_ERROR("  This file may be corrupted, truncated, or not match the TBC metadata.");
        efm_data_reader_.reset();
        has_efm_ = false;
        return false;
    }
    
    ORC_LOG_DEBUG("TBCAudioEFMHandler: Opened EFM data file: {}", efm_path);
    ORC_LOG_DEBUG("  EFM validation passed: {} t-values match metadata", expected_tvalues);
    
    // Compute cumulative byte offsets for O(1) access
    compute_efm_offsets();
    
    efm_data_path_ = efm_path;
    has_efm_ = true;
    
    return true;
}

// ============================================================================
// Cumulative offset computation
// ============================================================================

void TBCAudioEFMHandler::compute_audio_offsets() {
    uint64_t byte_offset = 0;
    auto field_range = provider_->field_range();
    auto& cache = provider_->get_field_metadata_cache();
    
    for (FieldID fid = field_range.start; fid < field_range.end; ++fid) {
        auto it = cache.find(fid);
        if (it != cache.end()) {
            auto& metadata = it->second;
            
            // Set start offset
            metadata.audio_byte_start = byte_offset;
            
            // Advance offset by this field's sample count
            if (metadata.audio_samples) {
                // Stereo 16-bit samples = 4 bytes per sample
                byte_offset += static_cast<uint64_t>(metadata.audio_samples.value()) * 4;
            }
            
            // Set end offset (exclusive)
            metadata.audio_byte_end = byte_offset;
        }
    }
    
    ORC_LOG_DEBUG("TBCAudioEFMHandler: Computed audio offsets, total size: {} bytes", byte_offset);
}

void TBCAudioEFMHandler::compute_efm_offsets() {
    uint64_t byte_offset = 0;
    auto field_range = provider_->field_range();
    auto& cache = provider_->get_field_metadata_cache();
    
    for (FieldID fid = field_range.start; fid < field_range.end; ++fid) {
        auto it = cache.find(fid);
        if (it != cache.end()) {
            auto& metadata = it->second;
            
            // Set start offset
            metadata.efm_byte_start = byte_offset;
            
            // Advance offset by this field's t-value count
            if (metadata.efm_t_values) {
                // Each t-value is 1 byte
                byte_offset += static_cast<uint64_t>(metadata.efm_t_values.value());
            }
            
            // Set end offset (exclusive)
            metadata.efm_byte_end = byte_offset;
        }
    }
    
    ORC_LOG_DEBUG("TBCAudioEFMHandler: Computed EFM offsets, total size: {} bytes", byte_offset);
}

// ============================================================================
// AC3 RF symbols interface implementation
// ============================================================================

uint32_t TBCAudioEFMHandler::get_ac3_symbol_count(FieldID id) const {
    if (!has_ac3_rf_) return 0;
    auto metadata = provider_->get_field_metadata(id);
    if (!metadata || !metadata->ac3rf_byte_start || !metadata->ac3rf_byte_end) return 0;
    return static_cast<uint32_t>(metadata->ac3rf_byte_end.value() - metadata->ac3rf_byte_start.value());
}

std::vector<uint8_t> TBCAudioEFMHandler::get_ac3_symbols(FieldID id) const {
    if (!has_ac3_rf_) return {};

    auto metadata = provider_->get_field_metadata(id);
    if (!metadata || !metadata->ac3rf_byte_start || !metadata->ac3rf_byte_end) return {};

    uint64_t start_offset = metadata->ac3rf_byte_start.value();
    uint64_t end_offset   = metadata->ac3rf_byte_end.value();
    uint64_t count        = end_offset - start_offset;
    if (count == 0) return {};

    std::lock_guard<std::mutex> lock(ac3rf_mutex_);
    if (!ac3rf_symbols_reader_ || !ac3rf_symbols_reader_->is_open()) {
        ORC_LOG_WARN("TBCAudioEFMHandler: AC3 RF symbols file not open");
        return {};
    }
    try {
        return ac3rf_symbols_reader_->read(start_offset, count);
    } catch (const std::exception& e) {
        ORC_LOG_WARN("TBCAudioEFMHandler: Failed to read AC3 symbols for field {}: {}",
                     id.value(), e.what());
        return {};
    }
}

bool TBCAudioEFMHandler::set_ac3rf_symbols_file(const std::string& ac3rf_path) {
    if (ac3rf_path.empty()) {
        has_ac3_rf_ = false;
        return true;
    }

    std::lock_guard<std::mutex> lock(ac3rf_mutex_);

    ac3rf_symbols_reader_ = std::make_unique<BufferedFileReader<uint8_t>>(4 * 1024 * 1024);
    if (!ac3rf_symbols_reader_->open(ac3rf_path)) {
        ORC_LOG_ERROR("TBCAudioEFMHandler: Failed to open AC3 RF symbols file: {}", ac3rf_path);
        has_ac3_rf_ = false;
        ac3rf_symbols_reader_.reset();
        return false;
    }

    uint64_t total_symbols = ac3rf_symbols_reader_->file_size();
    ORC_LOG_DEBUG("TBCAudioEFMHandler: Opened AC3 RF symbols file: {}", ac3rf_path);
    ORC_LOG_DEBUG("  Total symbols: {}", total_symbols);

    compute_ac3rf_offsets(total_symbols);

    ac3rf_symbols_path_ = ac3rf_path;
    has_ac3_rf_ = true;
    return true;
}

void TBCAudioEFMHandler::compute_ac3rf_offsets(uint64_t total_symbols) {
    auto field_range = provider_->field_range();
    auto& cache = provider_->get_field_metadata_cache();

    // Check whether per-field counts are available in the metadata.
    // If any field has ac3rf_symbols set, trust the metadata for all fields.
    bool have_per_field_counts = false;
    for (FieldID fid = field_range.start; fid < field_range.end; ++fid) {
        auto it = cache.find(fid);
        if (it != cache.end() && it->second.ac3rf_symbols.has_value()) {
            have_per_field_counts = true;
            break;
        }
    }

    uint64_t byte_offset = 0;
    uint64_t total_fields = field_range.end.value() - field_range.start.value();

    for (FieldID fid = field_range.start; fid < field_range.end; ++fid) {
        auto it = cache.find(fid);
        if (it == cache.end()) continue;
        auto& metadata = it->second;

        metadata.ac3rf_byte_start = byte_offset;

        if (have_per_field_counts && metadata.ac3rf_symbols.has_value()) {
            byte_offset += static_cast<uint64_t>(metadata.ac3rf_symbols.value());
        } else {
            // Uniform distribution fallback: divide total symbols evenly across fields.
            // This is accurate enough in practice since the DPLL symbol rate is stable.
            uint64_t field_index = fid.value() - field_range.start.value();
            uint64_t next_index  = field_index + 1;
            uint64_t end_sym = (total_symbols * next_index) / total_fields;
            uint64_t start_sym = (total_symbols * field_index) / total_fields;
            byte_offset = end_sym;
            metadata.ac3rf_byte_start = start_sym;
        }

        metadata.ac3rf_byte_end = byte_offset;
    }

    ORC_LOG_DEBUG("TBCAudioEFMHandler: Computed AC3 RF offsets, total: {} symbols ({} mode)",
                  total_symbols, have_per_field_counts ? "per-field metadata" : "uniform fallback");
}

} // namespace orc
