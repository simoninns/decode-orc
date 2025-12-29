/*
 * File:        tbc_metadata.h
 * Module:      orc-core
 * Purpose:     Tbc Metadata
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include "field_id.h"
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <map>
#include <array>

namespace orc {

/**
 * @brief Video format/system
 */
enum class VideoSystem {
    PAL,    // 625-line PAL
    NTSC,   // 525-line NTSC
    PAL_M,  // 525-line PAL
    Unknown
};

std::string video_system_to_string(VideoSystem system);
VideoSystem video_system_from_string(const std::string& name);

/**
 * @brief Video parameters from TBC metadata
 * 
 * Based on legacy LdDecodeMetaData::VideoParameters
 */
struct VideoParameters {
    // Format
    VideoSystem system = VideoSystem::Unknown;
    bool is_subcarrier_locked = false;
    bool is_widescreen = false;
    
    // Field/frame dimensions
    int32_t field_width = -1;
    int32_t field_height = -1;
    int32_t number_of_sequential_fields = -1;
    
    // Field ordering
    bool is_first_field_first = true;  // True if frame N uses fields (N*2-1, N*2), false if (N*2, N*2-1)
    
    // Sample ranges
    int32_t colour_burst_start = -1;
    int32_t colour_burst_end = -1;
    int32_t active_video_start = -1;
    int32_t active_video_end = -1;
    
    // Active line ranges (field-based)
    int32_t first_active_field_line = -1;
    int32_t last_active_field_line = -1;
    
    // Active line ranges (frame-based, interlaced)
    int32_t first_active_frame_line = -1;
    int32_t last_active_frame_line = -1;
    
    // IRE levels (16-bit)
    int32_t white_16b_ire = -1;
    int32_t black_16b_ire = -1;
    
    // Sample rate (Hz)
    double sample_rate = -1.0;
    
    // Color subcarrier frequency (Hz)
    double fsc = -1.0;
    
    // Mapping and format
    bool is_mapped = false;
    std::string tape_format;
    
    // Source information
    std::string decoder;       // Decoder used (e.g., "ld-decode", "vhs-decode")
    std::string git_branch;
    std::string git_commit;
    
    // Active area cropping flag - when true, decoders should write to 0-based ComponentFrame
    bool active_area_cropping_applied = false;
    
    bool is_valid() const { return system != VideoSystem::Unknown && field_width > 0; }
};

/**
 * @brief VBI (Vertical Blanking Interval) data
 */
struct VbiData {
    bool in_use = false;
    std::array<int32_t, 3> vbi_data = {0, 0, 0};
};

/**
 * @brief VITC (Vertical Interval Timecode) data
 */
struct VitcData {
    bool in_use = false;
    std::array<int32_t, 8> vitc_data = {0, 0, 0, 0, 0, 0, 0, 0};
};

/**
 * @brief NTSC-specific field data
 */
struct NtscData {
    bool in_use = false;
    bool is_fm_code_data_valid = false;
    int32_t fm_code_data = 0;
    bool field_flag = false;
    bool is_video_id_data_valid = false;
    int32_t video_id_data = 0;
    bool white_flag = false;
};

/**
 * @brief Closed Caption data
 */
struct ClosedCaptionData {
    bool in_use = false;
    int32_t data0 = -1;
    int32_t data1 = -1;
};

/**
 * @brief VITS (Vertical Interval Test Signals) metrics
 */
struct VitsMetrics {
    bool in_use = false;
    double white_snr = 0.0;
    double black_psnr = 0.0;
};

/**
 * @brief Dropout information for a field
 */
struct DropoutInfo {
    uint32_t line = 0;           ///< Line number (0-based, converted from 1-based database values)
    uint32_t start_sample = 0;   ///< Start sample within line
    uint32_t end_sample = 0;     ///< End sample within line (exclusive)
};

/**
 * @brief Collection of dropout information for a field
 */
struct DropoutData {
    std::vector<DropoutInfo> dropouts;
};

/**
 * @brief Complete metadata for a single field
 * 
 * Based on legacy LdDecodeMetaData::Field
 */
struct FieldMetadata {
    int32_t seq_no = 0;  // Sequence number (primary key in DB)
    
    // Fields from observers (written by sink observers)
    std::optional<bool> is_first_field;        // From FieldParityObserver
    std::optional<int32_t> field_phase_id;     // From PALPhaseObserver
    std::optional<double> median_burst_ire;    // From BurstLevelObserver
    
    // Fields from hints (typically from decoder metadata)
    std::optional<int32_t> audio_samples;
    std::optional<int32_t> decode_faults;
    std::optional<double> disk_location;
    std::optional<int32_t> efm_t_values;
    std::optional<int64_t> file_location;
    std::optional<int32_t> sync_confidence;
    std::optional<bool> is_pad;
    
    // VBI/metadata structures (from observers)
    VitsMetrics vits_metrics;
    VbiData vbi;
    NtscData ntsc;
    VitcData vitc;
    ClosedCaptionData closed_caption;
    std::vector<DropoutInfo> dropouts;
};

/**
 * @brief PCM audio parameters
 */
struct PcmAudioParameters {
    double sample_rate = -1.0;
    bool is_little_endian = false;
    bool is_signed = false;
    int32_t bits = -1;
    
    bool is_valid() const { return sample_rate > 0 && bits > 0; }
};

/**
 * @brief Reader for TBC metadata (SQLite database)
 * 
 * Based on legacy SqliteReader and LdDecodeMetaData classes.
 * Provides access to field metadata, VBI data, dropouts, etc.
 */
class TBCMetadataReader {
public:
    TBCMetadataReader();
    ~TBCMetadataReader();
    
    // Open a metadata database file
    bool open(const std::string& filename);
    void close();
    
    bool is_open() const { return is_open_; }
    
    // Read video parameters
    std::optional<VideoParameters> read_video_parameters();
    
    // Read PCM audio parameters
    std::optional<PcmAudioParameters> read_pcm_audio_parameters();
    
    // Read field metadata
    std::optional<FieldMetadata> read_field_metadata(FieldID field_id);
    
    // Read all field metadata (bulk operation)
    std::map<FieldID, FieldMetadata> read_all_field_metadata();
    void read_all_dropouts();  // Load all dropouts into cache
    
    // Preload all metadata and dropouts into cache
    // Call this when opening a project or adding a source stage to avoid lazy loading during analysis
    void preload_cache();
    
    // Read specific field data
    std::optional<VbiData> read_vbi(FieldID field_id);
    std::optional<VitcData> read_vitc(FieldID field_id);
    std::optional<ClosedCaptionData> read_closed_caption(FieldID field_id);
    std::optional<DropoutData> read_dropout(FieldID field_id) const;
    std::vector<DropoutInfo> read_dropouts(FieldID field_id) const;  // Legacy compatibility
    
    // Validation and diagnostics
    int32_t get_field_record_count() const;
    bool validate_metadata(std::string* error_message = nullptr) const;
    
private:
    class Impl;  // Forward declaration for pimpl
    std::unique_ptr<Impl> impl_;
    bool is_open_;
};

} // namespace orc
