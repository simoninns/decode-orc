/*
 * File:        tbc_metadata.h
 * Module:      orc-metadata
 * Purpose:     Tbc Metadata
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#pragma once

// Forward-declare the SQLite handle so callers that only need open_from_handle
// do not need to include <sqlite3.h>.
struct sqlite3;

#include <field_id.h>
#include <common_types.h>  // For VideoSystem enum
#include <orc_source_parameters.h>  // For SourceParameters
#include <video_metadata_types.h>  // For VbiData
#include <string>
#include <vector>
#include <optional>
#include <memory>
#include <map>
#include <array>

namespace orc {

// VideoSystem now defined in common_types.h

std::string video_system_to_string(VideoSystem system);
VideoSystem video_system_from_string(const std::string& name);

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
    
    // Cumulative byte offsets for efficient O(1) random access
    // These are computed from audio_samples and efm_t_values counts
    // when the metadata is loaded, eliminating the need for offset caching
    std::optional<uint64_t> audio_byte_start;    // Start offset in PCM file (bytes)
    std::optional<uint64_t> audio_byte_end;      // End offset in PCM file (bytes, exclusive)
    std::optional<uint64_t> efm_byte_start;      // Start offset in EFM file (bytes)
    std::optional<uint64_t> efm_byte_end;        // End offset in EFM file (bytes, exclusive)
    
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
 * @brief Pure-virtual interface for TBC metadata readers
 *
 * Both TBCMetadataSqliteReader (Phase 1-3) and TBCMetadataJsonReader (Phase 4)
 * implement this interface so callers can substitute either backend transparently.
 */
class ITBCMetadataReader {
public:
    virtual ~ITBCMetadataReader() = default;

    virtual bool open(const std::string& filename) = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;

    virtual std::optional<SourceParameters> read_video_parameters() = 0;
    virtual std::optional<PcmAudioParameters> read_pcm_audio_parameters() = 0;

    virtual std::optional<FieldMetadata> read_field_metadata(FieldID field_id) = 0;
    virtual std::map<FieldID, FieldMetadata> read_all_field_metadata() = 0;
    virtual void read_all_dropouts() = 0;
    virtual void preload_cache() = 0;

    virtual std::optional<VbiData> read_vbi(FieldID field_id) = 0;
    virtual std::optional<VitcData> read_vitc(FieldID field_id) = 0;
    virtual std::optional<ClosedCaptionData> read_closed_caption(FieldID field_id) = 0;
    virtual std::optional<DropoutData> read_dropout(FieldID field_id) const = 0;
    virtual std::vector<DropoutInfo> read_dropouts(FieldID field_id) const = 0;

    virtual int32_t get_field_record_count() const = 0;
    virtual bool validate_metadata(std::string* error_message = nullptr) const = 0;
};

/**
 * @brief SQLite-backed reader for TBC metadata
 *
 * Based on legacy SqliteReader and LdDecodeMetaData classes.
 * Provides access to field metadata, VBI data, dropouts, etc.
 */
class TBCMetadataSqliteReader : public ITBCMetadataReader {
public:
    TBCMetadataSqliteReader();
    ~TBCMetadataSqliteReader() override;

    // Open a metadata database file
    bool open(const std::string& filename) override;

    // Attach an already-open sqlite3 connection (takes ownership — the handle
    // will be closed by close() / the destructor).  Used by TBCMetadataJsonReader
    // to back a reader with an in-memory database.
    bool open_from_handle(sqlite3* db);

    void close() override;

    bool is_open() const override { return is_open_; }

    // Read video parameters
    std::optional<SourceParameters> read_video_parameters() override;

    // Read PCM audio parameters
    std::optional<PcmAudioParameters> read_pcm_audio_parameters() override;

    // Read field metadata
    std::optional<FieldMetadata> read_field_metadata(FieldID field_id) override;

    // Read all field metadata (bulk operation)
    std::map<FieldID, FieldMetadata> read_all_field_metadata() override;
    void read_all_dropouts() override;

    // Preload all metadata and dropouts into cache
    // Call this when opening a project or adding a source stage to avoid lazy loading during analysis
    void preload_cache() override;

    // Read specific field data
    std::optional<VbiData> read_vbi(FieldID field_id) override;
    std::optional<VitcData> read_vitc(FieldID field_id) override;
    std::optional<ClosedCaptionData> read_closed_caption(FieldID field_id) override;
    std::optional<DropoutData> read_dropout(FieldID field_id) const override;
    std::vector<DropoutInfo> read_dropouts(FieldID field_id) const override;

    // Validation and diagnostics
    int32_t get_field_record_count() const override;
    bool validate_metadata(std::string* error_message = nullptr) const override;

private:
    class Impl;  // Forward declaration for pimpl
    std::unique_ptr<Impl> impl_;
    bool is_open_;
};

// Backward-compatibility alias – prefer ITBCMetadataReader in new code
using TBCMetadataReader = TBCMetadataSqliteReader;

} // namespace orc
