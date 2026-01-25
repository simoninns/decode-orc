/*
 * File:        tbc_audio_efm_handler.h
 * Module:      orc-core
 * Purpose:     Shared audio/EFM handling for TBC sources
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "tbc_metadata.h"
#include "buffered_file_io.h"
#include <memory>
#include <mutex>
#include <map>

namespace orc {

/**
 * @brief Helper class to handle audio and EFM data loading for TBC sources
 * 
 * This class encapsulates all audio (.pcm) and EFM (.efm) file handling logic
 * that is common to both composite (TBCVideoFieldRepresentation) and YC 
 * (TBCYCVideoFieldRepresentation) sources.
 * 
 * Audio and EFM data are stored in separate files independent of whether the
 * video source is composite or YC, so the handling logic is identical.
 * 
 * This class provides:
 * - PCM audio file loading and validation
 * - EFM data file loading and validation
 * - Efficient buffered I/O with precomputed offsets
 * - Thread-safe access to audio/EFM data
 */
class TBCAudioEFMHandler {
public:
    /**
     * @brief Interface for accessing field metadata
     * 
     * Both TBC representation classes must implement this interface
     * to provide the handler with access to field metadata.
     */
    class MetadataProvider {
    public:
        virtual ~MetadataProvider() = default;
        virtual FieldIDRange field_range() const = 0;
        virtual std::optional<FieldMetadata> get_field_metadata(FieldID id) const = 0;
        virtual std::map<FieldID, FieldMetadata>& get_field_metadata_cache() = 0;
    };
    
    explicit TBCAudioEFMHandler(MetadataProvider* provider);
    ~TBCAudioEFMHandler() = default;
    
    // Prevent copying and moving
    TBCAudioEFMHandler(const TBCAudioEFMHandler&) = delete;
    TBCAudioEFMHandler& operator=(const TBCAudioEFMHandler&) = delete;
    TBCAudioEFMHandler(TBCAudioEFMHandler&&) = delete;
    TBCAudioEFMHandler& operator=(TBCAudioEFMHandler&&) = delete;
    
    // Audio interface
    bool set_audio_file(const std::string& pcm_path);
    uint32_t get_audio_sample_count(FieldID id) const;
    std::vector<int16_t> get_audio_samples(FieldID id) const;
    bool has_audio() const { return has_audio_; }
    
    // EFM interface
    bool set_efm_file(const std::string& efm_path);
    uint32_t get_efm_sample_count(FieldID id) const;
    std::vector<uint8_t> get_efm_samples(FieldID id) const;
    bool has_efm() const { return has_efm_; }
    
private:
    void compute_audio_offsets();
    void compute_efm_offsets();
    
    MetadataProvider* provider_;
    
    // PCM audio file handle and path
    std::string pcm_audio_path_;
    mutable std::unique_ptr<BufferedFileReader<int16_t>> pcm_audio_reader_;
    mutable std::mutex audio_mutex_;
    bool has_audio_;
    
    // EFM data file handle and path
    std::string efm_data_path_;
    mutable std::unique_ptr<BufferedFileReader<uint8_t>> efm_data_reader_;
    mutable std::mutex efm_mutex_;
    bool has_efm_;
};

} // namespace orc
