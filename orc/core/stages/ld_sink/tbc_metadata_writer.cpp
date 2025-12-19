/*
 * File:        tbc_metadata_writer.cpp
 * Module:      orc-core
 * Purpose:     TBC Metadata Writer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "tbc_metadata_writer.h"
#include "vitc_observer.h"
#include "biphase_observer.h"
#include "closed_caption_observer.h"
#include "vits_observer.h"
#include "burst_level_observer.h"
#include "field_parity_observer.h"
#include "pal_phase_observer.h"
#include "logging.h"
#include <sqlite3.h>
#include <stdexcept>

namespace orc {

// TBCMetadataWriter::Impl (Private implementation using SQLite)
class TBCMetadataWriter::Impl {
public:
    sqlite3* db = nullptr;
    int capture_id = -1;
    
    ~Impl() {
        if (db) {
            sqlite3_close(db);
        }
    }
    
    bool exec_sql(const std::string& sql) {
        char* err_msg = nullptr;
        int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err_msg);
        if (rc != SQLITE_OK) {
            std::string error = err_msg ? err_msg : "Unknown error";
            sqlite3_free(err_msg);
            ORC_LOG_ERROR("SQL error: {}", error);
            return false;
        }
        return true;
    }
    
    bool create_schema() {
        // Create tables matching legacy ld-decode schema
        const char* schema_sql = R"(
            CREATE TABLE IF NOT EXISTS capture (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                system TEXT NOT NULL,
                decoder TEXT,
                git_branch TEXT,
                git_commit TEXT,
                video_sample_rate REAL,
                active_video_start INTEGER,
                active_video_end INTEGER,
                field_width INTEGER,
                field_height INTEGER,
                number_of_sequential_fields INTEGER,
                colour_burst_start INTEGER,
                colour_burst_end INTEGER,
                is_mapped INTEGER,
                is_subcarrier_locked INTEGER,
                is_widescreen INTEGER,
                white_16b_ire INTEGER,
                black_16b_ire INTEGER,
                capture_notes TEXT
            );
            
            CREATE TABLE IF NOT EXISTS pcm_audio_parameters (
                capture_id INTEGER PRIMARY KEY,
                bits INTEGER NOT NULL,
                is_signed INTEGER NOT NULL,
                is_little_endian INTEGER NOT NULL,
                sample_rate REAL NOT NULL,
                FOREIGN KEY (capture_id) REFERENCES capture(id)
            );
            
            CREATE TABLE IF NOT EXISTS field_record (
                capture_id INTEGER NOT NULL,
                field_id INTEGER NOT NULL,
                audio_samples INTEGER,
                decode_faults INTEGER,
                disk_loc REAL,
                efm_t_values INTEGER,
                field_phase_id INTEGER,
                file_loc INTEGER,
                is_first_field INTEGER,
                median_burst_ire REAL,
                pad INTEGER,
                sync_conf INTEGER,
                ntsc_is_fm_code_data_valid INTEGER,
                ntsc_fm_code_data INTEGER,
                ntsc_field_flag INTEGER,
                ntsc_is_video_id_data_valid INTEGER,
                ntsc_video_id_data INTEGER,
                ntsc_white_flag INTEGER,
                PRIMARY KEY (capture_id, field_id),
                FOREIGN KEY (capture_id) REFERENCES capture(id)
            );
            
            CREATE TABLE IF NOT EXISTS vits_metrics (
                capture_id INTEGER NOT NULL,
                field_id INTEGER NOT NULL,
                white_snr REAL,
                black_psnr REAL,
                PRIMARY KEY (capture_id, field_id),
                FOREIGN KEY (capture_id, field_id) REFERENCES field_record(capture_id, field_id)
            );
            
            CREATE TABLE IF NOT EXISTS vbi (
                capture_id INTEGER NOT NULL,
                field_id INTEGER NOT NULL,
                vbi0 INTEGER,
                vbi1 INTEGER,
                vbi2 INTEGER,
                PRIMARY KEY (capture_id, field_id),
                FOREIGN KEY (capture_id, field_id) REFERENCES field_record(capture_id, field_id)
            );
            
            CREATE TABLE IF NOT EXISTS vitc (
                capture_id INTEGER NOT NULL,
                field_id INTEGER NOT NULL,
                vitc0 INTEGER,
                vitc1 INTEGER,
                vitc2 INTEGER,
                vitc3 INTEGER,
                vitc4 INTEGER,
                vitc5 INTEGER,
                vitc6 INTEGER,
                vitc7 INTEGER,
                PRIMARY KEY (capture_id, field_id),
                FOREIGN KEY (capture_id, field_id) REFERENCES field_record(capture_id, field_id)
            );
            
            CREATE TABLE IF NOT EXISTS closed_caption (
                capture_id INTEGER NOT NULL,
                field_id INTEGER NOT NULL,
                data0 INTEGER,
                data1 INTEGER,
                PRIMARY KEY (capture_id, field_id),
                FOREIGN KEY (capture_id, field_id) REFERENCES field_record(capture_id, field_id)
            );
            
            CREATE TABLE IF NOT EXISTS dropouts (
                capture_id INTEGER NOT NULL,
                field_id INTEGER NOT NULL,
                field_line INTEGER NOT NULL,
                startx INTEGER NOT NULL,
                endx INTEGER NOT NULL,
                FOREIGN KEY (capture_id, field_id) REFERENCES field_record(capture_id, field_id)
            );
            
            CREATE INDEX IF NOT EXISTS idx_field_record_capture ON field_record(capture_id);
            CREATE INDEX IF NOT EXISTS idx_dropouts_capture_field ON dropouts(capture_id, field_id);
        )";
        
        return exec_sql(schema_sql);
    }
};

// TBCMetadataWriter implementation

TBCMetadataWriter::TBCMetadataWriter()
    : impl_(std::make_unique<Impl>())
    , is_open_(false)
    , capture_id_(-1)
{
}

TBCMetadataWriter::~TBCMetadataWriter() {
    close();
}

bool TBCMetadataWriter::open(const std::string& filename) {
    if (is_open_) {
        close();
    }
    
    int rc = sqlite3_open(filename.c_str(), &impl_->db);
    if (rc != SQLITE_OK) {
        ORC_LOG_ERROR("Failed to open metadata database: {}", filename);
        return false;
    }
    
    is_open_ = true;
    
    // Create schema
    if (!impl_->create_schema()) {
        close();
        return false;
    }
    
    return true;
}

void TBCMetadataWriter::close() {
    if (impl_->db) {
        sqlite3_close(impl_->db);
        impl_->db = nullptr;
    }
    is_open_ = false;
    capture_id_ = -1;
}

bool TBCMetadataWriter::write_video_parameters(const VideoParameters& params) {
    if (!is_open_) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO capture (
            system, decoder, git_branch, git_commit,
            video_sample_rate, active_video_start, active_video_end,
            field_width, field_height, number_of_sequential_fields,
            colour_burst_start, colour_burst_end,
            is_mapped, is_subcarrier_locked, is_widescreen,
            white_16b_ire, black_16b_ire, capture_notes
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        ORC_LOG_ERROR("Failed to prepare capture insert");
        return false;
    }
    
    std::string system_str = video_system_to_string(params.system);
    sqlite3_bind_text(stmt, 1, system_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, params.decoder.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, params.git_branch.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 4, params.git_commit.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(stmt, 5, params.sample_rate);
    sqlite3_bind_int(stmt, 6, params.active_video_start);
    sqlite3_bind_int(stmt, 7, params.active_video_end);
    sqlite3_bind_int(stmt, 8, params.field_width);
    sqlite3_bind_int(stmt, 9, params.field_height);
    sqlite3_bind_int(stmt, 10, params.number_of_sequential_fields);
    sqlite3_bind_int(stmt, 11, params.colour_burst_start);
    sqlite3_bind_int(stmt, 12, params.colour_burst_end);
    sqlite3_bind_int(stmt, 13, params.is_mapped ? 1 : 0);
    sqlite3_bind_int(stmt, 14, params.is_subcarrier_locked ? 1 : 0);
    sqlite3_bind_int(stmt, 15, params.is_widescreen ? 1 : 0);
    sqlite3_bind_int(stmt, 16, params.white_16b_ire);
    sqlite3_bind_int(stmt, 17, params.black_16b_ire);
    sqlite3_bind_text(stmt, 18, "", -1, SQLITE_TRANSIENT);  // capture_notes
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        ORC_LOG_ERROR("Failed to insert capture record");
        return false;
    }
    
    capture_id_ = sqlite3_last_insert_rowid(impl_->db);
    impl_->capture_id = capture_id_;
    return true;
}

bool TBCMetadataWriter::write_pcm_audio_parameters(const PcmAudioParameters& params) {
    if (!is_open_ || capture_id_ < 0) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO pcm_audio_parameters (
            capture_id, bits, is_signed, is_little_endian, sample_rate
        ) VALUES (?, ?, ?, ?, ?)
    )";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, capture_id_);
    sqlite3_bind_int(stmt, 2, params.bits);
    sqlite3_bind_int(stmt, 3, params.is_signed ? 1 : 0);
    sqlite3_bind_int(stmt, 4, params.is_little_endian ? 1 : 0);
    sqlite3_bind_double(stmt, 5, params.sample_rate);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::write_field_metadata(const FieldMetadata& field) {
    if (!is_open_ || capture_id_ < 0) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO field_record (
            capture_id, field_id, audio_samples, decode_faults,
            disk_loc, efm_t_values, field_phase_id, file_loc,
            is_first_field, median_burst_ire, pad, sync_conf,
            ntsc_is_fm_code_data_valid, ntsc_fm_code_data, ntsc_field_flag,
            ntsc_is_video_id_data_valid, ntsc_video_id_data, ntsc_white_flag
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    // field_id is 0-based in database (seq_no - 1)
    int field_id = field.seq_no - 1;
    
    sqlite3_bind_int(stmt, 1, capture_id_);
    sqlite3_bind_int(stmt, 2, field_id);
    sqlite3_bind_int(stmt, 3, field.audio_samples);
    sqlite3_bind_int(stmt, 4, field.decode_faults);
    sqlite3_bind_double(stmt, 5, field.disk_location);
    sqlite3_bind_int(stmt, 6, field.efm_t_values);
    sqlite3_bind_int(stmt, 7, field.field_phase_id);
    sqlite3_bind_int64(stmt, 8, field.file_location);
    sqlite3_bind_int(stmt, 9, field.is_first_field ? 1 : 0);
    sqlite3_bind_double(stmt, 10, field.median_burst_ire);
    sqlite3_bind_int(stmt, 11, field.is_pad ? 1 : 0);
    sqlite3_bind_int(stmt, 12, field.sync_confidence);
    sqlite3_bind_int(stmt, 13, field.ntsc.is_fm_code_data_valid ? 1 : 0);
    sqlite3_bind_int(stmt, 14, field.ntsc.fm_code_data);
    sqlite3_bind_int(stmt, 15, field.ntsc.field_flag ? 1 : 0);
    sqlite3_bind_int(stmt, 16, field.ntsc.is_video_id_data_valid ? 1 : 0);
    sqlite3_bind_int(stmt, 17, field.ntsc.video_id_data);
    sqlite3_bind_int(stmt, 18, field.ntsc.white_flag ? 1 : 0);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::update_field_median_burst_ire(FieldID field_id, double median_burst_ire) {
    if (!is_open_ || capture_id_ < 0) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE field_record SET median_burst_ire = ? WHERE capture_id = ? AND field_id = ?";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_double(stmt, 1, median_burst_ire);
    sqlite3_bind_int(stmt, 2, capture_id_);
    sqlite3_bind_int(stmt, 3, field_id.value());
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::update_field_phase_id(FieldID field_id, int32_t field_phase_id) {
    if (!is_open_ || capture_id_ < 0) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE field_record SET field_phase_id = ? WHERE capture_id = ? AND field_id = ?";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, field_phase_id);
    sqlite3_bind_int(stmt, 2, capture_id_);
    sqlite3_bind_int(stmt, 3, field_id.value());
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::update_field_is_first_field(FieldID field_id, bool is_first_field) {
    if (!is_open_ || capture_id_ < 0) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "UPDATE field_record SET is_first_field = ? WHERE capture_id = ? AND field_id = ?";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, is_first_field ? 1 : 0);
    sqlite3_bind_int(stmt, 2, capture_id_);
    sqlite3_bind_int(stmt, 3, field_id.value());
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::write_vbi(FieldID field_id, const VbiData& vbi) {
    if (!is_open_ || capture_id_ < 0 || !vbi.in_use) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO vbi (capture_id, field_id, vbi0, vbi1, vbi2) VALUES (?, ?, ?, ?, ?)";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, capture_id_);
    sqlite3_bind_int(stmt, 2, field_id.value());
    sqlite3_bind_int(stmt, 3, vbi.vbi_data[0]);
    sqlite3_bind_int(stmt, 4, vbi.vbi_data[1]);
    sqlite3_bind_int(stmt, 5, vbi.vbi_data[2]);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::write_vitc(FieldID field_id, const VitcData& vitc) {
    if (!is_open_ || capture_id_ < 0 || !vitc.in_use) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = R"(
        INSERT INTO vitc (capture_id, field_id, vitc0, vitc1, vitc2, vitc3, vitc4, vitc5, vitc6, vitc7)
        VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
    )";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, capture_id_);
    sqlite3_bind_int(stmt, 2, field_id.value());
    for (int i = 0; i < 8; ++i) {
        sqlite3_bind_int(stmt, 3 + i, vitc.vitc_data[i]);
    }
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::write_closed_caption(FieldID field_id, const ClosedCaptionData& cc) {
    if (!is_open_ || capture_id_ < 0 || !cc.in_use) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO closed_caption (capture_id, field_id, data0, data1) VALUES (?, ?, ?, ?)";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, capture_id_);
    sqlite3_bind_int(stmt, 2, field_id.value());
    sqlite3_bind_int(stmt, 3, cc.data0);
    sqlite3_bind_int(stmt, 4, cc.data1);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::write_vits_metrics(FieldID field_id, const VitsMetrics& metrics) {
    if (!is_open_ || capture_id_ < 0 || !metrics.in_use) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO vits_metrics (capture_id, field_id, white_snr, black_psnr) VALUES (?, ?, ?, ?)";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, capture_id_);
    sqlite3_bind_int(stmt, 2, field_id.value());
    sqlite3_bind_double(stmt, 3, metrics.white_snr);
    sqlite3_bind_double(stmt, 4, metrics.black_psnr);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::write_dropout(FieldID field_id, const DropoutInfo& dropout) {
    if (!is_open_ || capture_id_ < 0) return false;
    
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO dropouts (capture_id, field_id, field_line, startx, endx) VALUES (?, ?, ?, ?, ?)";
    
    int rc = sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) return false;
    
    sqlite3_bind_int(stmt, 1, capture_id_);
    sqlite3_bind_int(stmt, 2, field_id.value());
    // Convert from 0-based (internal) to 1-based (database) line numbering
    sqlite3_bind_int(stmt, 3, dropout.line + 1);
    sqlite3_bind_int(stmt, 4, dropout.start_sample);
    sqlite3_bind_int(stmt, 5, dropout.end_sample);
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

bool TBCMetadataWriter::write_observations(FieldID field_id, 
                                          const std::vector<std::shared_ptr<Observation>>& observations) {
    if (!is_open_ || capture_id_ < 0) return false;
    
    for (const auto& obs : observations) {
        if (obs->confidence == ConfidenceLevel::NONE) continue;
        
        std::string type = obs->observation_type();
        
        if (type == "Biphase" || type == "VBI") {
            auto* vbi_obs = dynamic_cast<BiphaseObservation*>(obs.get());
            if (vbi_obs) {
                VbiData vbi;
                vbi.in_use = true;
                vbi.vbi_data = vbi_obs->vbi_data;
                write_vbi(field_id, vbi);
            }
        }
        else if (type == "VITC") {
            auto* vitc_obs = dynamic_cast<VitcObservation*>(obs.get());
            if (vitc_obs) {
                VitcData vitc;
                vitc.in_use = true;
                for (size_t i = 0; i < 8; ++i) {
                    vitc.vitc_data[i] = vitc_obs->vitc_data[i];
                }
                write_vitc(field_id, vitc);
            }
        }
        else if (type == "ClosedCaption") {
            auto* cc_obs = dynamic_cast<ClosedCaptionObservation*>(obs.get());
            if (cc_obs) {
                ClosedCaptionData cc;
                cc.in_use = true;
                cc.data0 = cc_obs->data0;
                cc.data1 = cc_obs->data1;
                write_closed_caption(field_id, cc);
            }
        }
        else if (type == "VITSQuality") {
            auto* vits_obs = dynamic_cast<VITSQualityObservation*>(obs.get());
            if (vits_obs && vits_obs->white_snr.has_value() && vits_obs->black_psnr.has_value()) {
                VitsMetrics metrics;
                metrics.in_use = true;
                metrics.white_snr = vits_obs->white_snr.value();
                metrics.black_psnr = vits_obs->black_psnr.value();
                write_vits_metrics(field_id, metrics);
            }
        }
        else if (type == "BurstLevel") {
            auto* burst_obs = dynamic_cast<BurstLevelObservation*>(obs.get());
            if (burst_obs) {
                update_field_median_burst_ire(field_id, burst_obs->median_burst_ire);
            }
        }
        else if (type == "PALPhase") {
            auto* phase_obs = dynamic_cast<PALPhaseObservation*>(obs.get());
            if (phase_obs && phase_obs->field_phase_id > 0) {
                update_field_phase_id(field_id, phase_obs->field_phase_id);
            }
        }
        else if (type == "FieldParity") {
            auto* parity_obs = dynamic_cast<FieldParityObservation*>(obs.get());
            if (parity_obs && parity_obs->confidence_pct >= 25) {
                update_field_is_first_field(field_id, parity_obs->is_first_field);
            }
        }
    }
    
    return true;
}

bool TBCMetadataWriter::begin_transaction() {
    return is_open_ && impl_->exec_sql("BEGIN TRANSACTION");
}

bool TBCMetadataWriter::commit_transaction() {
    return is_open_ && impl_->exec_sql("COMMIT");
}

bool TBCMetadataWriter::rollback_transaction() {
    return is_open_ && impl_->exec_sql("ROLLBACK");
}

} // namespace orc
