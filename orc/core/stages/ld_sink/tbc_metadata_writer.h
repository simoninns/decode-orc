/*
 * File:        tbc_metadata_writer.h
 * Module:      orc-core
 * Purpose:     TBC Metadata Writer (SQLite)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "tbc_metadata.h"
#include "field_id.h"
#include "observer.h"
#include <string>
#include <memory>
#include <vector>

namespace orc {

/**
 * @brief Writer for TBC metadata (SQLite database)
 * 
 * Creates ld-decode compatible SQLite databases with capture metadata,
 * field records, and observer data (VBI, VITC, closed captions, VITS metrics).
 */
class TBCMetadataWriter {
public:
    TBCMetadataWriter();
    ~TBCMetadataWriter();
    
    // Open/create a metadata database file
    bool open(const std::string& filename);
    void close();
    
    bool is_open() const { return is_open_; }
    
    // Write video parameters (creates capture record)
    bool write_video_parameters(const VideoParameters& params);
    
    // Write PCM audio parameters (optional)
    bool write_pcm_audio_parameters(const PcmAudioParameters& params);
    
    // Write field metadata
    bool write_field_metadata(const FieldMetadata& field);
    
    // Update field metadata (for fields already written)
    bool update_field_median_burst_ire(FieldID field_id, double median_burst_ire);
    bool update_field_phase_id(FieldID field_id, int32_t field_phase_id);
    bool update_field_is_first_field(FieldID field_id, bool is_first_field);
    
    // Write observer data for a field
    bool write_vbi(FieldID field_id, const VbiData& vbi);
    bool write_vitc(FieldID field_id, const VitcData& vitc);
    bool write_closed_caption(FieldID field_id, const ClosedCaptionData& cc);
    bool write_vits_metrics(FieldID field_id, const VitsMetrics& metrics);
    bool write_dropout(FieldID field_id, const DropoutInfo& dropout);
    
    // Bulk write observations from observers
    bool write_observations(FieldID field_id, 
                           const std::vector<std::shared_ptr<Observation>>& observations);
    
    // Transaction support for bulk writes
    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();
    
private:
    class Impl;  // Forward declaration for pimpl
    std::unique_ptr<Impl> impl_;
    bool is_open_;
    int capture_id_;  // ID of the capture record
};

} // namespace orc
