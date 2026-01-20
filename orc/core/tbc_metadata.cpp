/*
 * File:        tbc_metadata.cpp
 * Module:      orc-core
 * Purpose:     Tbc Metadata
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "tbc_metadata.h"
#include "logging.h"
#include <stdexcept>
#include <sqlite3.h>
#include <cstring>
#include <mutex>
#include <map>

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
    
    // Thread-safe cache for field metadata and dropouts
    mutable std::mutex cache_mutex_;
    std::map<FieldID, FieldMetadata> metadata_cache_;
    std::map<FieldID, std::vector<DropoutInfo>> dropout_cache_;
    bool cache_loaded_ = false;
    
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
    
    std::optional<int> get_optional_int(sqlite3_stmt* stmt, int col) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return std::nullopt;
        }
        return sqlite3_column_int(stmt, col);
    }
    
    int64_t get_int64(sqlite3_stmt* stmt, int col, int64_t default_val = -1) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return default_val;
        }
        return sqlite3_column_int64(stmt, col);
    }
    
    std::optional<int64_t> get_optional_int64(sqlite3_stmt* stmt, int col) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return std::nullopt;
        }
        return sqlite3_column_int64(stmt, col);
    }
    
    double get_double(sqlite3_stmt* stmt, int col, double default_val = -1.0) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return default_val;
        }
        return sqlite3_column_double(stmt, col);
    }
    
    std::optional<double> get_optional_double(sqlite3_stmt* stmt, int col) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return std::nullopt;
        }
        return sqlite3_column_double(stmt, col);
    }
    
    bool get_bool(sqlite3_stmt* stmt, int col, bool default_val = false) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return default_val;
        }
        return sqlite3_column_int(stmt, col) != 0;
    }
    
    std::optional<bool> get_optional_bool(sqlite3_stmt* stmt, int col) {
        if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
            return std::nullopt;
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
        ORC_LOG_DEBUG("read_video_parameters: Metadata database is not open");
        return std::nullopt;
    }
    
    VideoParameters params;
    
    // Try to read with all columns including blanking_16b_ire
    const char* sql_with_blanking = 
        "SELECT system, video_sample_rate, active_video_start, active_video_end, "
        "field_width, field_height, number_of_sequential_fields, "
        "colour_burst_start, colour_burst_end, is_mapped, is_subcarrier_locked, "
        "is_widescreen, blanking_16b_ire, black_16b_ire, white_16b_ire, decoder, git_branch, git_commit "
        "FROM capture WHERE capture_id = ?";
    
    // Fallback for older metadata that lacks blanking_16b_ire
    const char* sql_without_blanking = 
        "SELECT system, video_sample_rate, active_video_start, active_video_end, "
        "field_width, field_height, number_of_sequential_fields, "
        "colour_burst_start, colour_burst_end, is_mapped, is_subcarrier_locked, "
        "is_widescreen, black_16b_ire, white_16b_ire, decoder, git_branch, git_commit "
        "FROM capture WHERE capture_id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql_with_blanking, -1, &stmt, nullptr);
    bool has_blanking_column = true;
    
    if (rc != SQLITE_OK) {
        // Check if this is due to missing blanking_16b_ire column
        const char* err_msg = sqlite3_errmsg(impl_->db);
        if (std::string(err_msg).find("blanking_16b_ire") != std::string::npos) {
            ORC_LOG_WARN("read_video_parameters: blanking_16b_ire column not found in database, using fallback query");
            has_blanking_column = false;
            
            // Try fallback query without blanking_16b_ire
            rc = sqlite3_prepare_v2(impl_->db, sql_without_blanking, -1, &stmt, nullptr);
            if (rc != SQLITE_OK) {
                ORC_LOG_ERROR("read_video_parameters: Failed to prepare fallback SQL statement: {}", sqlite3_errmsg(impl_->db));
                return std::nullopt;
            }
        } else {
            ORC_LOG_ERROR("read_video_parameters: Failed to prepare SQL statement: {}", err_msg);
            return std::nullopt;
        }
    }
    
    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    
    int step_rc = sqlite3_step(stmt);
    if (step_rc == SQLITE_ROW) {
        params.system = video_system_from_string(impl_->get_string(stmt, 0));
        params.sample_rate = impl_->get_double(stmt, 1);
        // active_video_start/end in database are HORIZONTAL sample positions (x-axis)
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
        
        int col_offset = 12;
        if (has_blanking_column) {
            params.blanking_16b_ire = impl_->get_int(stmt, col_offset);
            col_offset++;
            params.black_16b_ire = impl_->get_int(stmt, col_offset);
            params.white_16b_ire = impl_->get_int(stmt, col_offset + 1);
            params.decoder = impl_->get_string(stmt, col_offset + 2);
            params.git_branch = impl_->get_string(stmt, col_offset + 3);
            params.git_commit = impl_->get_string(stmt, col_offset + 4);
        } else {
            // Fallback: blanking_16b_ire not in database, set it to black_16b_ire value
            params.black_16b_ire = impl_->get_int(stmt, col_offset);
            params.blanking_16b_ire = params.black_16b_ire;
            params.white_16b_ire = impl_->get_int(stmt, col_offset + 1);
            params.decoder = impl_->get_string(stmt, col_offset + 2);
            params.git_branch = impl_->get_string(stmt, col_offset + 3);
            params.git_commit = impl_->get_string(stmt, col_offset + 4);
            ORC_LOG_WARN("read_video_parameters: blanking_16b_ire not in database, defaulting to black_16b_ire value ({})", params.black_16b_ire);
        }
        
        // FSC is not stored in database - leave unset (-1.0)
        // Will be populated by source stage based on video system
        params.fsc = -1.0;
        
        // Vertical field line boundaries must be inferred from video system format defaults
        // These match the values from legacy-tools/library/tbc/lddecodemetadata.cpp
        // For PAL (even frame lines), field lines can be calculated as frame/2
        // For NTSC (odd frame lines), we use hardcoded values to match ld-chroma-decoder
        if (params.system == VideoSystem::PAL) {
            params.first_active_frame_line = 44;
            params.last_active_frame_line = 620;
            params.first_active_field_line = params.first_active_frame_line / 2;  // 22
            params.last_active_field_line = params.last_active_frame_line / 2;    // 310
        } else if (params.system == VideoSystem::NTSC) {
            params.first_active_frame_line = 40;
            params.last_active_frame_line = 525;
            params.first_active_field_line = 20;   // Hardcoded to match ld-chroma-decoder
            params.last_active_field_line = 259;   // Not 262 (525/2) - must match baseline
        } else if (params.system == VideoSystem::PAL_M) {
            // PAL-M uses same line boundaries as NTSC
            params.first_active_frame_line = 40;
            params.last_active_frame_line = 525;
            params.first_active_field_line = 20;
            params.last_active_field_line = 259;
        }
        
        sqlite3_finalize(stmt);
        ORC_LOG_DEBUG("read_video_parameters: Successfully read video parameters from capture_id {}", impl_->capture_id);
        return params;
    } else if (step_rc == SQLITE_DONE) {
        ORC_LOG_ERROR("read_video_parameters: No capture record found for capture_id {}", impl_->capture_id);
    } else {
        ORC_LOG_ERROR("read_video_parameters: SQL execution error: {}", sqlite3_errmsg(impl_->db));
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

void TBCMetadataReader::preload_cache() {
    if (!is_open_) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(impl_->cache_mutex_);
    
    if (!impl_->cache_loaded_) {
        ORC_LOG_DEBUG("Preloading metadata cache from database");
        impl_->metadata_cache_ = read_all_field_metadata();
        read_all_dropouts();
        impl_->cache_loaded_ = true;
        ORC_LOG_DEBUG("Preloaded {} field metadata records and dropouts", impl_->metadata_cache_.size());
    }
}

std::optional<FieldMetadata> TBCMetadataReader::read_field_metadata(FieldID field_id) {
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }
    
    // Check cache first
    {
        std::lock_guard<std::mutex> lock(impl_->cache_mutex_);
        
        // Load all metadata and dropouts on first access (if not already preloaded)
        if (!impl_->cache_loaded_) {
            impl_->metadata_cache_ = read_all_field_metadata();
            read_all_dropouts();  // Also load all dropouts
            impl_->cache_loaded_ = true;
        }
        
        auto it = impl_->metadata_cache_.find(field_id);
        if (it != impl_->metadata_cache_.end()) {
            return it->second;
        }
    }
    
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
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return result;
    }
    
    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        FieldMetadata metadata;
        metadata.seq_no = impl_->get_int(stmt, 0);
        metadata.is_first_field = impl_->get_optional_bool(stmt, 1);
        metadata.sync_confidence = impl_->get_optional_int(stmt, 2);
        metadata.median_burst_ire = impl_->get_optional_double(stmt, 3);
        metadata.field_phase_id = impl_->get_optional_int(stmt, 4);
        metadata.audio_samples = impl_->get_optional_int(stmt, 5);
        metadata.is_pad = impl_->get_optional_bool(stmt, 6);
        metadata.disk_location = impl_->get_optional_double(stmt, 7);
        metadata.file_location = impl_->get_optional_int64(stmt, 8);
        metadata.decode_faults = impl_->get_optional_int(stmt, 9);
        metadata.efm_t_values = impl_->get_optional_int(stmt, 10);
        
        result[FieldID(metadata.seq_no)] = metadata;
    }
    
    sqlite3_finalize(stmt);
    
    return result;
}

std::optional<VbiData> TBCMetadataReader::read_vbi(FieldID field_id) {
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT * FROM vbi WHERE capture_id = ? AND field_id = ?";
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        // Table may not exist in older databases
        return std::nullopt;
    }

    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    sqlite3_bind_int(stmt, 2, field_id.value());

    VbiData vbi;
    vbi.in_use = false;
    vbi.vbi_data = {0, 0, 0};

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        // Build a mapping from column name to index for flexible schema support
        int col_count = sqlite3_column_count(stmt);
        auto get_col = [&](const char* const names[]) -> std::optional<int> {
            for (int c = 0; c < col_count; ++c) {
                const char* cname = sqlite3_column_name(stmt, c);
                if (!cname) continue;
                for (int i = 0; names[i] != nullptr; ++i) {
                    if (strcasecmp(cname, names[i]) == 0) {
                        return c;
                    }
                }
            }
            return std::nullopt;
        };

        // Possible column name variants per line
        const char* line16_cols[] = {"l16", "line16", "line_16", "vbi16", "vbi_16", nullptr};
        const char* line17_cols[] = {"l17", "line17", "line_17", "vbi17", "vbi_17", nullptr};
        const char* line18_cols[] = {"l18", "line18", "line_18", "vbi18", "vbi_18", nullptr};

        auto c16 = get_col(line16_cols);
        auto c17 = get_col(line17_cols);
        auto c18 = get_col(line18_cols);

        bool found_any = false;
        if (c16.has_value() && sqlite3_column_type(stmt, *c16) != SQLITE_NULL) {
            vbi.vbi_data[0] = sqlite3_column_int(stmt, *c16);
            found_any = true;
        }
        if (c17.has_value() && sqlite3_column_type(stmt, *c17) != SQLITE_NULL) {
            vbi.vbi_data[1] = sqlite3_column_int(stmt, *c17);
            found_any = true;
        }
        if (c18.has_value() && sqlite3_column_type(stmt, *c18) != SQLITE_NULL) {
            vbi.vbi_data[2] = sqlite3_column_int(stmt, *c18);
            found_any = true;
        }

        vbi.in_use = found_any;
    }

    sqlite3_finalize(stmt);
    if (vbi.in_use) {
        return vbi;
    }
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
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "SELECT data0, data1 FROM closed_caption WHERE capture_id = ? AND field_id = ?";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        return std::nullopt;
    }
    
    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    sqlite3_bind_int(stmt, 2, field_id.value());
    
    ClosedCaptionData cc;
    cc.in_use = false;
    
    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        cc.in_use = true;
        cc.data0 = sqlite3_column_int(stmt, 0);
        cc.data1 = sqlite3_column_int(stmt, 1);
    }
    
    sqlite3_finalize(stmt);
    
    if (cc.in_use) {
        return cc;
    }
    
    return std::nullopt;
}

std::vector<DropoutInfo> TBCMetadataReader::read_dropouts(FieldID field_id) const {
    if (!is_open_ || !field_id.is_valid()) {
        return {};
    }
    
    // Check cache first (cache is loaded in read_field_metadata)
    {
        std::lock_guard<std::mutex> lock(impl_->cache_mutex_);
        auto it = impl_->dropout_cache_.find(field_id);
        if (it != impl_->dropout_cache_.end()) {
            return it->second;
        }
    }
    
    // If not in cache, field has no dropouts
    return {};
}

void TBCMetadataReader::read_all_dropouts() {
    if (!is_open_) {
        return;
    }
    
    impl_->dropout_cache_.clear();
    
    const char* sql = 
        "SELECT field_id, startx, endx, field_line "
        "FROM drop_outs WHERE capture_id = ? ORDER BY field_id";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return;
    }
    
    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t field_id_val = sqlite3_column_int64(stmt, 0);
        FieldID field_id(field_id_val);
        
        DropoutInfo dropout;
        dropout.start_sample = static_cast<uint32_t>(impl_->get_int(stmt, 1));
        dropout.end_sample = static_cast<uint32_t>(impl_->get_int(stmt, 2));
        // TBC database uses 1-based line numbers, convert to 0-based for internal use
        dropout.line = static_cast<uint32_t>(impl_->get_int(stmt, 3)) - 1;
        
        impl_->dropout_cache_[field_id].push_back(dropout);
    }
    
    sqlite3_finalize(stmt);
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

int32_t TBCMetadataReader::get_field_record_count() const {
    if (!is_open_) {
        return -1;
    }
    
    const char* sql = "SELECT COUNT(*) FROM field_record WHERE capture_id = ?";
    
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    
    if (rc != SQLITE_OK) {
        return -1;
    }
    
    sqlite3_bind_int(stmt, 1, impl_->capture_id);
    
    int32_t count = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    
    sqlite3_finalize(stmt);
    return count;
}

bool TBCMetadataReader::validate_metadata(std::string* error_message) const {
    if (!is_open_) {
        if (error_message) {
            *error_message = "Metadata database is not open";
        }
        ORC_LOG_ERROR("validate_metadata: {}", error_message ? *error_message : "Unknown error");
        return false;
    }
    
    // Read video parameters
    auto params_opt = const_cast<TBCMetadataReader*>(this)->read_video_parameters();
    if (!params_opt) {
        if (error_message) {
            *error_message = "Failed to read video parameters from metadata";
        }
        ORC_LOG_ERROR("validate_metadata: {} - check debug logs for details", error_message ? *error_message : "Unknown error");
        return false;
    }
    
    const auto& params = *params_opt;
    
    // Check that number_of_sequential_fields is set
    if (params.number_of_sequential_fields <= 0) {
        if (error_message) {
            *error_message = "Metadata does not specify valid number_of_sequential_fields (" + 
                           std::to_string(params.number_of_sequential_fields) + ")";
        }
        ORC_LOG_ERROR("validate_metadata: {}", *error_message);
        return false;
    }
    
    // Get actual field record count from database
    int32_t field_record_count = get_field_record_count();
    if (field_record_count < 0) {
        if (error_message) {
            *error_message = "Failed to count field records in database";
        }
        ORC_LOG_ERROR("validate_metadata: {}", *error_message);
        return false;
    }
    
    // Check consistency between capture table and field_record table
    // Warning: Some TBC files have mismatches where field_record has more entries
    // than number_of_sequential_fields indicates. This is a known issue with
    // certain ld-decode versions. We use field_record count to match ld-discmap behavior.
    if (field_record_count != params.number_of_sequential_fields) {
        if (error_message) {
            *error_message = "Metadata inconsistency: capture table specifies " +
                           std::to_string(params.number_of_sequential_fields) + 
                           " fields, but field_record table contains " +
                           std::to_string(field_record_count) + " records. " +
                           "This TBC file has inconsistent metadata, likely from a buggy " +
                           "ld-decode version or interrupted capture.";
        }
        return false;
    }
    
    // Validate field dimensions
    if (params.field_width <= 0 || params.field_height <= 0) {
        if (error_message) {
            *error_message = "Invalid field dimensions: " + 
                           std::to_string(params.field_width) + "x" + 
                           std::to_string(params.field_height);
        }
        return false;
    }
    
    // Validate video system
    if (params.system == VideoSystem::Unknown) {
        if (error_message) {
            *error_message = "Unknown or unsupported video system";
        }
        return false;
    }
    
    return true;
}

} // namespace orc
