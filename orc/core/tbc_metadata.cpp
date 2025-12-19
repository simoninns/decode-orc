/*
 * File:        tbc_metadata.cpp
 * Module:      orc-core
 * Purpose:     Tbc Metadata
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "tbc_metadata.h"
#include <stdexcept>
#include <sqlite3.h>
#include <cstring>

namespace orc {

// ============================================================================
// Video System helpers
// ============================================================================

std::string video_system_to_string(VideoSystem system) {
    switch (system) {
        case VideoSystem::PAL: return "PAL";
        case VideoSystem::NTSC: return "NTSC";
        case VideoSystem::PAL_M: return "PAL-M";
        default: return "Unknown";
    }
}

VideoSystem video_system_from_string(const std::string& name) {
    if (name == "PAL") return VideoSystem::PAL;
    if (name == "NTSC") return VideoSystem::NTSC;
    if (name == "PAL-M" || name == "PAL_M") return VideoSystem::PAL_M;
    return VideoSystem::Unknown;
}

// ============================================================================
// TBCMetadataReader::Impl (Private implementation using SQLite)
// ============================================================================

class TBCMetadataReader::Impl {
public:
    sqlite3* db = nullptr;
    int capture_id = 1;  // Default capture ID
    
    ~Impl() {
        if (db) {
            sqlite3_close(db);
            db = nullptr;
        }
    }
    
    bool execute_query(const char* sql, 
                       std::function<bool(sqlite3_stmt*)> callback) {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
        
        if (rc != SQLITE_OK) {
            return false;
        }
        
        bool success = true;
        while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
            if (!callback(stmt)) {
                success = false;
                break;
            }
        }
        
        sqlite3_finalize(stmt);
        return success && (rc == SQLITE_DONE || rc == SQLITE_ROW);
    }
    
    int get_int(sqlite3_stmt* stmt, int col, int default_val = -1) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return default_val;
        }
        return sqlite3_column_int(stmt, col);
    }
    
    int64_t get_int64(sqlite3_stmt* stmt, int col, int64_t default_val = -1) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return default_val;
        }
        return sqlite3_column_int64(stmt, col);
    }
    
    double get_double(sqlite3_stmt* stmt, int col, double default_val = -1.0) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return default_val;
        }
        return sqlite3_column_double(stmt, col);
    }
    
    bool get_bool(sqlite3_stmt* stmt, int col, bool default_val = false) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return default_val;
        }
        return sqlite3_column_int(stmt, col) != 0;
    }
    
    std::string get_string(sqlite3_stmt* stmt, int col, const std::string& default_val = "") {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return default_val;
        }
        const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
        return text ? std::string(text) : default_val;
    }
};

// ============================================================================
// TBCMetadataReader implementation
// ============================================================================

TBCMetadataReader::TBCMetadataReader()
    : impl_(std::make_unique<Impl>()), is_open_(false) {
}

TBCMetadataReader::~TBCMetadataReader() {
    close();
}

bool TBCMetadataReader::open(const std::string& filename) {
    close();
    
    int rc = sqlite3_open_v2(filename.c_str(), &impl_->db, 
                             SQLITE_OPEN_READONLY, nullptr);
    
    if (rc != SQLITE_OK) {
        if (impl_->db) {
            sqlite3_close(impl_->db);
            impl_->db = nullptr;
        }
        return false;
    }
    
    is_open_ = true;
    return true;
}

void TBCMetadataReader::close() {
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
    is_open_ = false;
}

std::optional<VideoParameters> TBCMetadataReader::read_video_parameters() {
    if (!is_open_) {
        return std::nullopt;
    }
    
    VideoParameters params;
    
    const char* sql = 
        "SELECT system, video_sample_rate, active_video_start, active_video_end, "
        "field_width, field_height, number_of_sequential_fields, "
        "colour_burst_start, colour_burst_end, is_mapped, is_subcarrier_locked, "
        "is_widescreen, white_16b_ire, black_16b_ire, git_branch, git_commit "
        "FROM capture WHERE capture_id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        params.system = video_system_from_string(impl_->get_string(stmt, 0));
        params.sample_rate = impl_->get_double(stmt, 1);
        params.active_video_start = impl_->get_int(stmt, 2);
        params.active_video_end = impl_->get_int(stmt, 3);
        params.field_width = impl_->get_int(stmt, 4);
        params.field_height = impl_->get_int(stmt, 5);
        params.number_of_sequential_fields = impl_->get_int(stmt, 6);
        params.colour_burst_start = impl_->get_int(stmt, 7);
        params.colour_burst_end = impl_->get_int(stmt, 8);
        params.is_mapped = impl_->get_bool(stmt, 9);
        params.is_subcarrier_locked = impl_->get_bool(stmt, 10);
        params.is_widescreen = impl_->get_bool(stmt, 11);
        params.white_16b_ire = impl_->get_int(stmt, 12);
        params.black_16b_ire = impl_->get_int(stmt, 13);
        params.git_branch = impl_->get_string(stmt, 14);
        params.git_commit = impl_->get_string(stmt, 15);
        
        sqlite3_finalize(stmt);
        return params;
    }
    
    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::optional<PcmAudioParameters> TBCMetadataReader::read_pcm_audio_parameters() {
    if (!is_open_) {
        return std::nullopt;
    }
    
    PcmAudioParameters params;
    
    const char* sql = 
        "SELECT sample_rate, bits, is_signed, is_little_endian "
        "FROM pcm_audio_parameters WHERE capture_id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        params.sample_rate = impl_->get_double(stmt, 0);
        params.bits = impl_->get_int(stmt, 1);
        params.is_signed = impl_->get_bool(stmt, 2);
        params.is_little_endian = impl_->get_bool(stmt, 3);
        
        sqlite3_finalize(stmt);
        return params;
    }
    
    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::optional<FieldMetadata> TBCMetadataReader::read_field_metadata(FieldID field_id) {
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }
    
    FieldMetadata metadata;
    
    const char* sql = 
        "SELECT field_id, is_first_field, sync_conf, median_burst_ire, field_phase_id, "
        "audio_samples, pad, disk_loc, file_loc, decode_faults, efm_t_values "
        "FROM field_record WHERE capture_id = ? AND field_id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    sqlite3_bind_int64(stmt, 2, field_id.value());
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        metadata.seq_no = impl_->get_int(stmt, 0);
        metadata.is_first_field = impl_->get_bool(stmt, 1);
        metadata.sync_confidence = impl_->get_int(stmt, 2);
        metadata.median_burst_ire = impl_->get_double(stmt, 3);
        metadata.field_phase_id = impl_->get_int(stmt, 4);
        metadata.audio_samples = impl_->get_int(stmt, 5);
        metadata.is_pad = impl_->get_bool(stmt, 6);
        metadata.disk_location = impl_->get_double(stmt, 7);
        metadata.file_location = impl_->get_int64(stmt, 8);
        metadata.decode_faults = impl_->get_int(stmt, 9);
        metadata.efm_t_values = impl_->get_int(stmt, 10);
        
        sqlite3_finalize(stmt);
        return metadata;
    }
    
    sqlite3_finalize(stmt);
    return std::nullopt;
}

std::map<FieldID, FieldMetadata> TBCMetadataReader::read_all_field_metadata() {
    std::map<FieldID, FieldMetadata> result;
    
    if (!is_open_) {
        return result;
    }
    
    const char* sql = 
        "SELECT field_id, is_first_field, sync_conf, median_burst_ire, field_phase_id, "
        "audio_samples, pad, disk_loc, file_loc, decode_faults, efm_t_values "
        "FROM field_record WHERE capture_id = ? ORDER BY field_id";
    
    impl_->execute_query(sql, [&](sqlite3_stmt* stmt) {
        FieldMetadata metadata;
        metadata.seq_no = impl_->get_int(stmt, 0);
        metadata.is_first_field = impl_->get_bool(stmt, 1);
        metadata.sync_confidence = impl_->get_int(stmt, 2);
        metadata.median_burst_ire = impl_->get_double(stmt, 3);
        metadata.field_phase_id = impl_->get_int(stmt, 4);
        metadata.audio_samples = impl_->get_int(stmt, 5);
        metadata.is_pad = impl_->get_bool(stmt, 6);
        metadata.disk_location = impl_->get_double(stmt, 7);
        metadata.file_location = impl_->get_int64(stmt, 8);
        metadata.decode_faults = impl_->get_int(stmt, 9);
        metadata.efm_t_values = impl_->get_int(stmt, 10);
        
        result[FieldID(metadata.seq_no)] = metadata;
        return true;
    });
    
    return result;
}

std::optional<VbiData> TBCMetadataReader::read_vbi(FieldID field_id) {
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }
    
    // VBI reading implementation would go here
    // For now, return empty optional
    return std::nullopt;
}

std::optional<VitcData> TBCMetadataReader::read_vitc(FieldID field_id) {
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }
    
    // VITC reading implementation would go here
    return std::nullopt;
}

std::optional<ClosedCaptionData> TBCMetadataReader::read_closed_caption(FieldID field_id) {
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }
    
    // Closed caption reading implementation would go here
    return std::nullopt;
}

std::vector<DropoutInfo> TBCMetadataReader::read_dropouts(FieldID field_id) const {
    std::vector<DropoutInfo> dropouts;
    
    if (!is_open_ || !field_id.is_valid()) {
        return dropouts;
    }
    
    const char* sql = 
        "SELECT startx, endx, field_line "
        "FROM drop_outs WHERE capture_id = ? AND field_id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return dropouts;
    }
    
    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    sqlite3_bind_int64(stmt, 2, field_id.value());
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DropoutInfo dropout;
        dropout.start_sample = static_cast<uint32_t>(impl_->get_int(stmt, 0));
        dropout.end_sample = static_cast<uint32_t>(impl_->get_int(stmt, 1));
        dropout.line = static_cast<uint32_t>(impl_->get_int(stmt, 2));
        dropouts.push_back(dropout);
    }
    
    sqlite3_finalize(stmt);
    return dropouts;
}

std::optional<DropoutData> TBCMetadataReader::read_dropout(FieldID field_id) const {
    auto dropouts = read_dropouts(field_id);
    if (dropouts.empty()) {
        return std::nullopt;
    }
    
    DropoutData data;
    data.dropouts = dropouts;
    return data;
}

} // namespace orc
