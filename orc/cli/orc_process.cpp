// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025
//
// orc-process: Execute DAG pipeline on TBC files

#include "tbc_video_field_representation.h"
#include "biphase_observer.h"
#include "vitc_observer.h"
#include "closed_caption_observer.h"
#include "video_id_observer.h"
#include "fm_code_observer.h"
#include "white_flag_observer.h"
#include "vits_observer.h"
#include "dropout_correct_stage.h"
#include "dropout_decision.h"
#include "observer.h"

#include <iostream>
#include <fstream>
#include <filesystem>
#include <memory>
#include <vector>
#include <string>
#include <map>
#include <cstring>
#include <cmath>
#include <iomanip>
#include <sqlite3.h>

namespace fs = std::filesystem;
using namespace orc;
struct ObserverConfig {
    std::string type;
    bool enabled;
};

struct StageConfig {
    std::string type;
    bool enabled;
    std::map<std::string, std::string> parameters;
};

struct DropoutDecisionConfig {
    uint64_t field_id;
    uint32_t line;
    uint32_t start_sample;
    uint32_t end_sample;
    std::string action;  // "add", "remove", or "modify"
    std::string notes;
};

struct DAGConfig {
    std::string name;
    std::string version;
    std::vector<ObserverConfig> observers;
    std::vector<StageConfig> stages;
    std::vector<DropoutDecisionConfig> dropout_decisions;
};

// Simple YAML parser for our specific format
DAGConfig parse_dag_yaml(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open DAG file: " + filename);
    }
    
    DAGConfig config;
    std::string line;
    bool in_observers = false;
    bool in_stages = false;
    bool in_decisions = false;
    ObserverConfig current_observer;
    StageConfig current_stage;
    DropoutDecisionConfig current_decision;
    bool has_observer_type = false;
    bool has_stage_type = false;
    bool has_decision_field = false;
    
    while (std::getline(file, line)) {
        // Trim whitespace
        size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        line = line.substr(start);
        
        // Skip comments
        if (line[0] == '#') continue;
        
        // Parse key-value pairs
        if (line.find("name:") == 0) {
            size_t quote1 = line.find('"');
            size_t quote2 = line.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                config.name = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        } else if (line.find("version:") == 0) {
            size_t quote1 = line.find('"');
            size_t quote2 = line.find('"', quote1 + 1);
            if (quote1 != std::string::npos && quote2 != std::string::npos) {
                config.version = line.substr(quote1 + 1, quote2 - quote1 - 1);
            }
        } else if (line.find("observers:") == 0) {
            // Save previous stage if exists
            if (has_stage_type) {
                config.stages.push_back(current_stage);
                has_stage_type = false;
            }
            in_observers = true;
            in_stages = false;
        } else if (line.find("stages:") == 0) {
            // Save previous observer if exists
            if (has_observer_type) {
                config.observers.push_back(current_observer);
                has_observer_type = false;
            }
            // Save previous decision if exists
            if (has_decision_field) {
                config.dropout_decisions.push_back(current_decision);
                has_decision_field = false;
            }
            in_observers = false;
            in_stages = true;
            in_decisions = false;
        } else if (line.find("dropout_decisions:") == 0) {
            // Save previous observer/stage if exists
            if (has_observer_type) {
                config.observers.push_back(current_observer);
                has_observer_type = false;
            }
            if (has_stage_type) {
                config.stages.push_back(current_stage);
                has_stage_type = false;
            }
            in_observers = false;
            in_stages = false;
            in_decisions = true;
        } else if (in_observers) {
            if (line.find("- type:") == 0) {
                // Save previous observer if exists
                if (has_observer_type) {
                    config.observers.push_back(current_observer);
                }
                // Start new observer
                current_observer = ObserverConfig();
                size_t colon = line.find(':');
                current_observer.type = line.substr(colon + 1);
                // Trim whitespace
                size_t t_start = current_observer.type.find_first_not_of(" \t");
                current_observer.type = current_observer.type.substr(t_start);
                current_observer.enabled = true; // default
                has_observer_type = true;
            } else if (line.find("enabled:") == 0) {
                size_t colon = line.find(':');
                std::string value = line.substr(colon + 1);
                size_t v_start = value.find_first_not_of(" \t");
                value = value.substr(v_start);
                current_observer.enabled = (value == "true");
            }
        } else if (in_stages) {
            if (line.find("- type:") == 0) {
                // Save previous stage if exists
                if (has_stage_type) {
                    config.stages.push_back(current_stage);
                }
                // Start new stage
                current_stage = StageConfig();
                size_t colon = line.find(':');
                current_stage.type = line.substr(colon + 1);
                // Trim whitespace
                size_t t_start = current_stage.type.find_first_not_of(" \t");
                current_stage.type = current_stage.type.substr(t_start);
                current_stage.enabled = true; // default
                has_stage_type = true;
            } else if (line.find("enabled:") == 0) {
                size_t colon = line.find(':');
                std::string value = line.substr(colon + 1);
                size_t v_start = value.find_first_not_of(" \t");
                value = value.substr(v_start);
                current_stage.enabled = (value == "true");
            } else if (has_stage_type && line.find(':') != std::string::npos) {
                // Parse parameter
                size_t colon = line.find(':');
                std::string key = line.substr(0, colon);
                std::string value = line.substr(colon + 1);
                // Trim whitespace
                size_t k_start = key.find_first_not_of(" \t");
                size_t v_start = value.find_first_not_of(" \t");
                if (k_start != std::string::npos && v_start != std::string::npos) {
                    key = key.substr(k_start);
                    value = value.substr(v_start);
                    current_stage.parameters[key] = value;
                }
            }
        } else if (in_decisions) {
            if (line.find("- field_id:") == 0) {
                // Save previous decision if exists
                if (has_decision_field) {
                    config.dropout_decisions.push_back(current_decision);
                }
                // Start new decision
                current_decision = DropoutDecisionConfig();
                size_t colon = line.find(':');
                std::string value = line.substr(colon + 1);
                size_t v_start = value.find_first_not_of(" \t");
                value = value.substr(v_start);
                current_decision.field_id = std::stoull(value);
                has_decision_field = true;
            } else if (has_decision_field) {
                size_t colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = line.substr(0, colon);
                    std::string value = line.substr(colon + 1);
                    // Trim whitespace
                    size_t k_start = key.find_first_not_of(" \t");
                    size_t v_start = value.find_first_not_of(" \t");
                    if (k_start != std::string::npos && v_start != std::string::npos) {
                        key = key.substr(k_start);
                        value = value.substr(v_start);
                        // Remove quotes if present
                        if (!value.empty() && value[0] == '"') {
                            size_t end_quote = value.find('"', 1);
                            if (end_quote != std::string::npos) {
                                value = value.substr(1, end_quote - 1);
                            }
                        }
                        
                        if (key == "line") current_decision.line = std::stoul(value);
                        else if (key == "start_sample") current_decision.start_sample = std::stoul(value);
                        else if (key == "end_sample") current_decision.end_sample = std::stoul(value);
                        else if (key == "action") current_decision.action = value;
                        else if (key == "notes") current_decision.notes = value;
                    }
                }
            }
        }
    }
    
    // Save last observer/stage/decision if exists
    if (has_observer_type) {
        config.observers.push_back(current_observer);
    }
    if (has_stage_type) {
        config.stages.push_back(current_stage);
    }
    if (has_decision_field) {
        config.dropout_decisions.push_back(current_decision);
    }
    
    return config;
}

std::unique_ptr<Observer> create_observer(const std::string& type) {
    if (type == "biphase") return std::make_unique<BiphaseObserver>();
    if (type == "vitc") return std::make_unique<VitcObserver>();
    if (type == "closed_caption") return std::make_unique<ClosedCaptionObserver>();
    if (type == "video_id") return std::make_unique<VideoIdObserver>();
    if (type == "fm_code") return std::make_unique<FmCodeObserver>();
    if (type == "white_flag") return std::make_unique<WhiteFlagObserver>();
    if (type == "vits") return std::make_unique<VITSQualityObserver>();
    
    throw std::runtime_error("Unknown observer type: " + type);
}

std::shared_ptr<DropoutCorrectStage> create_dropout_correct_stage(
    const std::map<std::string, std::string>& parameters) {
    
    DropoutCorrectionConfig config;
    
    // Parse parameters
    auto it = parameters.find("overcorrect_extension");
    if (it != parameters.end()) {
        config.overcorrect_extension = std::stoi(it->second);
    }
    
    it = parameters.find("intrafield_only");
    if (it != parameters.end()) {
        config.intrafield_only = (it->second == "true" || it->second == "1");
    }
    
    it = parameters.find("reverse_field_order");
    if (it != parameters.end()) {
        config.reverse_field_order = (it->second == "true" || it->second == "1");
    }
    
    it = parameters.find("max_replacement_distance");
    if (it != parameters.end()) {
        config.max_replacement_distance = std::stoi(it->second);
    }
    
    it = parameters.find("match_chroma_phase");
    if (it != parameters.end()) {
        config.match_chroma_phase = (it->second == "true" || it->second == "1");
    }
    
    return std::make_shared<DropoutCorrectStage>(config);
}

void copy_tbc_file(const std::string& input, const std::string& output) {
    std::ifstream src(input, std::ios::binary);
    std::ofstream dst(output, std::ios::binary);
    
    if (!src || !dst) {
        throw std::runtime_error("Failed to copy TBC file");
    }
    
    dst << src.rdbuf();
}

// Write video parameters and field metadata to recreate base tables
void write_base_metadata(sqlite3* db, TBCVideoFieldRepresentation& representation) {
    // Get video parameters from the representation
    auto video_params = representation.video_parameters();
    
    // Create capture table
    const char* create_capture =
        "CREATE TABLE IF NOT EXISTS capture ("
        "capture_id INTEGER PRIMARY KEY,"
        "system TEXT NOT NULL,"
        "decoder TEXT NOT NULL,"
        "git_branch TEXT,"
        "git_commit TEXT,"
        "video_sample_rate REAL,"
        "active_video_start INTEGER,"
        "active_video_end INTEGER,"
        "field_width INTEGER,"
        "field_height INTEGER,"
        "number_of_sequential_fields INTEGER,"
        "colour_burst_start INTEGER,"
        "colour_burst_end INTEGER,"
        "is_mapped INTEGER,"
        "is_subcarrier_locked INTEGER,"
        "is_widescreen INTEGER,"
        "white_16b_ire INTEGER,"
        "black_16b_ire INTEGER,"
        "capture_notes TEXT"
        ");";
    
    sqlite3_exec(db, create_capture, nullptr, nullptr, nullptr);
    
    // Insert capture record (capture_id = 1)
    sqlite3_stmt* capture_stmt;
    const char* insert_capture = 
        "INSERT INTO capture (capture_id, system, decoder, video_sample_rate, "
        "active_video_start, active_video_end, field_width, field_height, "
        "number_of_sequential_fields, colour_burst_start, colour_burst_end, "
        "is_mapped, is_subcarrier_locked, is_widescreen, white_16b_ire, black_16b_ire) "
        "VALUES (1, ?, 'orc-process', ?, ?, ?, ?, ?, ?, ?, ?, 0, 0, 0, ?, ?);";
    
    sqlite3_prepare_v2(db, insert_capture, -1, &capture_stmt, nullptr);
    
    std::string system_str = (video_params.system == VideoSystem::PAL) ? "PAL" : "NTSC";
    sqlite3_bind_text(capture_stmt, 1, system_str.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_double(capture_stmt, 2, video_params.sample_rate);
    sqlite3_bind_int(capture_stmt, 3, video_params.active_video_start);
    sqlite3_bind_int(capture_stmt, 4, video_params.active_video_end);
    sqlite3_bind_int(capture_stmt, 5, video_params.field_width);
    sqlite3_bind_int(capture_stmt, 6, video_params.field_height);
    sqlite3_bind_int(capture_stmt, 7, representation.field_range().size());
    sqlite3_bind_int(capture_stmt, 8, video_params.colour_burst_start);
    sqlite3_bind_int(capture_stmt, 9, video_params.colour_burst_end);
    sqlite3_bind_int(capture_stmt, 10, video_params.white_16b_ire);
    sqlite3_bind_int(capture_stmt, 11, video_params.black_16b_ire);
    
    sqlite3_step(capture_stmt);
    sqlite3_finalize(capture_stmt);
    
    // Create and populate pcm_audio_parameters table if present
    if (auto pcm_reader = const_cast<TBCMetadataReader*>(representation.metadata_reader())) {
        auto pcm_opt = pcm_reader->read_pcm_audio_parameters();
        if (pcm_opt) {
            auto& pcm = *pcm_opt;
            
            const char* create_pcm =
                "CREATE TABLE IF NOT EXISTS pcm_audio_parameters ("
                "capture_id INTEGER PRIMARY KEY,"
                "bits INTEGER,"
                "is_signed INTEGER,"
                "is_little_endian INTEGER,"
                "sample_rate REAL"
                ");";
            
            sqlite3_exec(db, create_pcm, nullptr, nullptr, nullptr);
            
            sqlite3_stmt* pcm_stmt;
            const char* insert_pcm =
                "INSERT INTO pcm_audio_parameters (capture_id, bits, is_signed, is_little_endian, sample_rate) "
                "VALUES (1, ?, ?, ?, ?);";
            
            sqlite3_prepare_v2(db, insert_pcm, -1, &pcm_stmt, nullptr);
            sqlite3_bind_int(pcm_stmt, 1, pcm.bits);
            sqlite3_bind_int(pcm_stmt, 2, pcm.is_signed ? 1 : 0);
            sqlite3_bind_int(pcm_stmt, 3, pcm.is_little_endian ? 1 : 0);
            sqlite3_bind_double(pcm_stmt, 4, pcm.sample_rate);
            sqlite3_step(pcm_stmt);
            sqlite3_finalize(pcm_stmt);
        }
    }
    
    // Create field_record table
    const char* create_field_record =
        "CREATE TABLE IF NOT EXISTS field_record ("
        "capture_id INTEGER NOT NULL,"
        "field_id INTEGER NOT NULL,"
        "is_first_field INTEGER,"
        "sync_conf INTEGER,"
        "median_burst_ire REAL,"
        "field_phase_id INTEGER,"
        "audio_samples INTEGER,"
        "pad INTEGER,"
        "disk_loc REAL,"
        "file_loc INTEGER,"
        "decode_faults INTEGER,"
        "efm_t_values INTEGER,"
        "ntsc_is_fm_code_data_valid INTEGER,"
        "ntsc_fm_code_data INTEGER,"
        "ntsc_field_flag INTEGER,"
        "ntsc_is_video_id_data_valid INTEGER,"
        "ntsc_video_id_data INTEGER,"
        "ntsc_white_flag INTEGER,"
        "PRIMARY KEY (capture_id, field_id)"
        ");";
    
    sqlite3_exec(db, create_field_record, nullptr, nullptr, nullptr);
    
    // Insert field records from ingested metadata
    sqlite3_stmt* field_stmt;
    const char* insert_field =
        "INSERT INTO field_record (capture_id, field_id, is_first_field, sync_conf, "
        "median_burst_ire, field_phase_id, audio_samples, pad, disk_loc, file_loc, "
        "decode_faults, efm_t_values, ntsc_is_fm_code_data_valid, ntsc_fm_code_data, "
        "ntsc_field_flag, ntsc_is_video_id_data_valid, ntsc_video_id_data, ntsc_white_flag) "
        "VALUES (1, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);";
    
    sqlite3_prepare_v2(db, insert_field, -1, &field_stmt, nullptr);
    
    auto field_range = representation.field_range();
    for (size_t i = 0; i < field_range.size(); ++i) {
        FieldID field_id = field_range.start + i;
        auto metadata_opt = representation.get_field_metadata(field_id);
        
        if (metadata_opt) {
            auto& metadata = *metadata_opt;
            // field_id in database must be 0-indexed (sequential)
            sqlite3_bind_int(field_stmt, 1, static_cast<int>(i));
            sqlite3_bind_int(field_stmt, 2, metadata.is_first_field ? 1 : 0);
            sqlite3_bind_int(field_stmt, 3, metadata.sync_confidence);
            sqlite3_bind_double(field_stmt, 4, metadata.median_burst_ire);
            sqlite3_bind_int(field_stmt, 5, metadata.field_phase_id);
            sqlite3_bind_int(field_stmt, 6, metadata.audio_samples);
            sqlite3_bind_int(field_stmt, 7, metadata.is_pad ? 1 : 0);
            sqlite3_bind_double(field_stmt, 8, metadata.disk_location);
            sqlite3_bind_int64(field_stmt, 9, metadata.file_location);
            sqlite3_bind_int(field_stmt, 10, metadata.decode_faults);
            sqlite3_bind_int(field_stmt, 11, metadata.efm_t_values);
            // NTSC-specific fields
            sqlite3_bind_int(field_stmt, 12, metadata.ntsc.is_fm_code_data_valid ? 1 : 0);
            sqlite3_bind_int(field_stmt, 13, metadata.ntsc.fm_code_data);
            sqlite3_bind_int(field_stmt, 14, metadata.ntsc.field_flag ? 1 : 0);
            sqlite3_bind_int(field_stmt, 15, metadata.ntsc.is_video_id_data_valid ? 1 : 0);
            sqlite3_bind_int(field_stmt, 16, metadata.ntsc.video_id_data);
            sqlite3_bind_int(field_stmt, 17, metadata.ntsc.white_flag ? 1 : 0);
            
            sqlite3_step(field_stmt);
            sqlite3_reset(field_stmt);
        }
    }
    
    sqlite3_finalize(field_stmt);
}

void write_observations_to_db(const std::string& db_path,
                              TBCVideoFieldRepresentation& representation,
                              const std::vector<std::unique_ptr<Observer>>& observers,
                              bool dropout_corrected) {
    
    // Statistics
    size_t vbi_count = 0;
    size_t vitc_count = 0;
    size_t cc_count = 0;
    size_t video_id_count = 0;
    size_t fm_code_count = 0;
    size_t white_flag_count = 0;
    size_t vits_count = 0;
    
    // Open/create database
    sqlite3* db;
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("Cannot open database: " + std::string(sqlite3_errmsg(db)));
    }
    
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    
    // Write base metadata (capture, field_record) from ingested data
    write_base_metadata(db, representation);
    
    // Create observer result tables
    const char* create_vbi_table = 
        "CREATE TABLE IF NOT EXISTS vbi ("
        "capture_id INTEGER NOT NULL,"
        "field_id INTEGER NOT NULL,"
        "vbi0 INTEGER,"
        "vbi1 INTEGER,"
        "vbi2 INTEGER,"
        "PRIMARY KEY (capture_id, field_id)"
        ");";
    
    const char* create_vitc_table =
        "CREATE TABLE IF NOT EXISTS vitc ("
        "capture_id INTEGER NOT NULL,"
        "field_id INTEGER NOT NULL,"
        "line_number INTEGER,"
        "frames INTEGER,"
        "hours INTEGER,"
        "minutes INTEGER,"
        "seconds INTEGER,"
        "PRIMARY KEY (capture_id, field_id)"
        ");";
    
    const char* create_cc_table =
        "CREATE TABLE IF NOT EXISTS closed_caption ("
        "capture_id INTEGER NOT NULL,"
        "field_id INTEGER NOT NULL,"
        "data0 INTEGER,"
        "data1 INTEGER,"
        "PRIMARY KEY (capture_id, field_id)"
        ");";
    
    const char* create_vits_table =
        "CREATE TABLE IF NOT EXISTS vits_metrics ("
        "capture_id INTEGER NOT NULL,"
        "field_id INTEGER NOT NULL,"
        "w_snr REAL,"
        "b_psnr REAL,"
        "PRIMARY KEY (capture_id, field_id)"
        ");";
    
    const char* create_dropout_table =
        "CREATE TABLE IF NOT EXISTS drop_outs ("
        "capture_id INTEGER NOT NULL,"
        "field_id INTEGER NOT NULL,"
        "field_line INTEGER,"
        "startx INTEGER,"
        "endx INTEGER"
        ");";
    
    sqlite3_exec(db, create_vbi_table, nullptr, nullptr, nullptr);
    sqlite3_exec(db, create_vitc_table, nullptr, nullptr, nullptr);
    sqlite3_exec(db, create_cc_table, nullptr, nullptr, nullptr);
    sqlite3_exec(db, create_vits_table, nullptr, nullptr, nullptr);
    sqlite3_exec(db, create_dropout_table, nullptr, nullptr, nullptr);
    
    // Handle dropout metadata based on correction state
    if (!dropout_corrected) {
        // No correction stage - copy original dropouts
        const auto* metadata = representation.metadata_reader();
        if (metadata) {
            sqlite3_stmt* dropout_stmt;
            sqlite3_prepare_v2(db, "INSERT INTO drop_outs (capture_id, field_id, field_line, startx, endx) VALUES (1, ?, ?, ?, ?);",
                              -1, &dropout_stmt, nullptr);
            
            auto field_range = representation.field_range();
            for (size_t i = 0; i < field_range.size(); ++i) {
                FieldID field_id = field_range.start + i;
                auto dropout_data = metadata->read_dropout(field_id);
                if (dropout_data) {
                    for (const auto& dropout : dropout_data->dropouts) {
                        sqlite3_bind_int(dropout_stmt, 1, static_cast<int>(i));
                        sqlite3_bind_int(dropout_stmt, 2, dropout.line);
                        sqlite3_bind_int(dropout_stmt, 3, dropout.start_sample);
                        sqlite3_bind_int(dropout_stmt, 4, dropout.end_sample);
                        sqlite3_step(dropout_stmt);
                        sqlite3_reset(dropout_stmt);
                    }
                }
            }
            sqlite3_finalize(dropout_stmt);
        }
    }
    // If dropout_corrected == true, leave table empty (all dropouts corrected)
    
    // Prepare statements
    sqlite3_stmt* vbi_stmt;
    sqlite3_stmt* vitc_stmt;
    sqlite3_stmt* cc_stmt;
    sqlite3_stmt* vits_stmt;
    
    sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO vbi (capture_id, field_id, vbi0, vbi1, vbi2) VALUES (1, ?, ?, ?, ?);",
                      -1, &vbi_stmt, nullptr);
    sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO vitc (capture_id, field_id, line_number, frames, hours, minutes, seconds) VALUES (1, ?, ?, ?, ?, ?, ?);",
                      -1, &vitc_stmt, nullptr);
    sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO closed_caption (capture_id, field_id, data0, data1) VALUES (1, ?, ?, ?);",
                      -1, &cc_stmt, nullptr);
    sqlite3_prepare_v2(db, "INSERT OR REPLACE INTO vits_metrics (capture_id, field_id, w_snr, b_psnr) VALUES (1, ?, ?, ?);",
                      -1, &vits_stmt, nullptr);
    
    // Process all fields
    auto field_range = representation.field_range();
    size_t total_fields = field_range.size();
    size_t processed = 0;
    
    std::cout << "Processing " << total_fields << " fields..." << std::endl;
    
    sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);
    
    for (size_t i = 0; i < total_fields; ++i) {
        FieldID field_id = field_range.start + i;
        
        // Run all observers
        for (const auto& observer : observers) {
            auto observations = observer->process_field(representation, field_id);
            
            for (const auto& obs : observations) {
                // Write biphase observations
                if (auto biphase = std::dynamic_pointer_cast<BiphaseObservation>(obs)) {
                    if (biphase->confidence != ConfidenceLevel::NONE) {
                        // field_id in database must be 0-indexed
                        sqlite3_bind_int(vbi_stmt, 1, static_cast<int>(i));
                        sqlite3_bind_int(vbi_stmt, 2, biphase->vbi_data[0]);
                        sqlite3_bind_int(vbi_stmt, 3, biphase->vbi_data[1]);
                        sqlite3_bind_int(vbi_stmt, 4, biphase->vbi_data[2]);
                        sqlite3_step(vbi_stmt);
                        sqlite3_reset(vbi_stmt);
                        vbi_count++;
                    }
                }
                // Write VITC observations
                else if (auto vitc = std::dynamic_pointer_cast<VitcObservation>(obs)) {
                    if (vitc->confidence != ConfidenceLevel::NONE) {
                        sqlite3_bind_int(vitc_stmt, 1, static_cast<int>(i));
                        sqlite3_bind_int(vitc_stmt, 2, vitc->line_number);
                        sqlite3_bind_int(vitc_stmt, 3, vitc->frames);
                        sqlite3_bind_int(vitc_stmt, 4, vitc->hours);
                        sqlite3_bind_int(vitc_stmt, 5, vitc->minutes);
                        sqlite3_bind_int(vitc_stmt, 6, vitc->seconds);
                        sqlite3_step(vitc_stmt);
                        sqlite3_reset(vitc_stmt);
                        vitc_count++;
                    }
                }
                // Write closed caption observations
                else if (auto cc = std::dynamic_pointer_cast<ClosedCaptionObservation>(obs)) {
                    if (cc->confidence != ConfidenceLevel::NONE) {
                        sqlite3_bind_int(cc_stmt, 1, static_cast<int>(i));
                        sqlite3_bind_int(cc_stmt, 2, cc->data0);
                        sqlite3_bind_int(cc_stmt, 3, cc->data1);
                        sqlite3_step(cc_stmt);
                        sqlite3_reset(cc_stmt);
                        cc_count++;
                    }
                }
                // Write VITS quality observations
                else if (auto vits = std::dynamic_pointer_cast<VITSQualityObservation>(obs)) {
                    if (vits->confidence != ConfidenceLevel::NONE) {
                        sqlite3_bind_int(vits_stmt, 1, static_cast<int>(i));
                        if (vits->white_snr) {
                            // Round to 4 decimal places
                            double rounded = std::round(*vits->white_snr * 10000.0) / 10000.0;
                            sqlite3_bind_double(vits_stmt, 2, rounded);
                        } else {
                            sqlite3_bind_null(vits_stmt, 2);
                        }
                        if (vits->black_psnr) {
                            // Round to 4 decimal places
                            double rounded = std::round(*vits->black_psnr * 10000.0) / 10000.0;
                            sqlite3_bind_double(vits_stmt, 3, rounded);
                        } else {
                            sqlite3_bind_null(vits_stmt, 3);
                        }
                        sqlite3_step(vits_stmt);
                        sqlite3_reset(vits_stmt);
                        vits_count++;
                    }
                }
                // Count other observer types
                else if (auto vid = std::dynamic_pointer_cast<VideoIdObservation>(obs)) {
                    if (vid->confidence != ConfidenceLevel::NONE) video_id_count++;
                }
                else if (auto fm = std::dynamic_pointer_cast<FmCodeObservation>(obs)) {
                    if (fm->confidence != ConfidenceLevel::NONE) fm_code_count++;
                }
                else if (auto wf = std::dynamic_pointer_cast<WhiteFlagObservation>(obs)) {
                    if (wf->confidence != ConfidenceLevel::NONE) white_flag_count++;
                }
            }
        }
        
        processed++;
        if (processed % 100 == 0 || processed == total_fields) {
            std::cout << "\r  Progress: " << processed << "/" << total_fields 
                     << " (" << (100 * processed / total_fields) << "%)" << std::flush;
        }
    }
    
    std::cout << std::endl;
    
    sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr);
    
    sqlite3_finalize(vbi_stmt);
    sqlite3_finalize(vitc_stmt);
    sqlite3_finalize(cc_stmt);
    sqlite3_finalize(vits_stmt);
    
    // Calculate VITS averages
    double avg_white_snr = 0.0, avg_black_psnr = 0.0;
    if (vits_count > 0) {
        sqlite3_stmt* avg_stmt;
        const char* avg_query = "SELECT AVG(w_snr), AVG(b_psnr) FROM vits_metrics WHERE w_snr IS NOT NULL";
        if (sqlite3_prepare_v2(db, avg_query, -1, &avg_stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(avg_stmt) == SQLITE_ROW) {
                avg_white_snr = sqlite3_column_double(avg_stmt, 0);
                avg_black_psnr = sqlite3_column_double(avg_stmt, 1);
            }
            sqlite3_finalize(avg_stmt);
        }
    }
    
    sqlite3_close(db);
    
    // Print statistics
    std::cout << "\nObserver Results:" << std::endl;
    std::cout << "  Biphase (VBI):        " << vbi_count << " fields" << std::endl;
    std::cout << "  VITC Timecode:        " << vitc_count << " fields" << std::endl;
    std::cout << "  Closed Captions:      " << cc_count << " fields" << std::endl;
    std::cout << "  VITS Metrics:         " << vits_count << " fields";
    if (vits_count > 0) {
        std::cout << std::fixed << std::setprecision(4);
        std::cout << " (avg white SNR: " << avg_white_snr << " dB, avg black PSNR: " << avg_black_psnr << " dB)";
        std::cout << std::defaultfloat;
    }
    std::cout << std::endl;
    std::cout << "  Video ID:             " << video_id_count << " fields" << std::endl;
    std::cout << "  FM Code:              " << fm_code_count << " fields" << std::endl;
    std::cout << "  White Flag:           " << white_flag_count << " fields" << std::endl;
}

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " --dag <pipeline.yaml> <input.tbc> <output.tbc>\n\n";
    std::cout << "Execute a DAG pipeline on TBC files\n\n";
    std::cout << "Arguments:\n";
    std::cout << "  --dag <file>     YAML file describing the processing pipeline\n";
    std::cout << "  input.tbc        Input TBC file (with .tbc.db)\n";
    std::cout << "  output.tbc       Output TBC file (creates .tbc and .tbc.db)\n\n";
    std::cout << "Example:\n";
    std::cout << "  " << program_name << " --dag vbi-observers.yaml input.tbc output.tbc\n";
}

int main(int argc, char* argv[]) {
    if (argc != 5 || std::string(argv[1]) != "--dag") {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string dag_file = argv[2];
    std::string input_tbc = argv[3];
    std::string output_tbc = argv[4];
    std::string input_db = input_tbc + ".db";
    std::string output_db = output_tbc + ".db";
    
    try {
        // Parse DAG configuration
        std::cout << "Loading DAG: " << dag_file << std::endl;
        auto config = parse_dag_yaml(dag_file);
        std::cout << "  Pipeline: " << config.name << " v" << config.version << std::endl;
        std::cout << "  Observers configured: " << config.observers.size() << std::endl;
        
        // Check input files exist
        if (!fs::exists(input_tbc)) {
            throw std::runtime_error("Input TBC file not found: " + input_tbc);
        }
        if (!fs::exists(input_db)) {
            throw std::runtime_error("Input database not found: " + input_db);
        }
        
        // Create observers
        std::vector<std::unique_ptr<Observer>> observers;
        for (const auto& obs_config : config.observers) {
            if (obs_config.enabled) {
                std::cout << "  Enabling observer: " << obs_config.type << std::endl;
                observers.push_back(create_observer(obs_config.type));
            }
        }
        
        if (observers.empty()) {
            std::cout << "\nWarning: No observers enabled in DAG\n";
        }
        
        // Load dropout decisions if present
        DropoutDecisions dropout_decisions;
        if (!config.dropout_decisions.empty()) {
            std::cout << "  Loading " << config.dropout_decisions.size() << " dropout decisions" << std::endl;
            for (const auto& dec : config.dropout_decisions) {
                DropoutDecision::Action action;
                if (dec.action == "add") action = DropoutDecision::Action::ADD;
                else if (dec.action == "remove") action = DropoutDecision::Action::REMOVE;
                else if (dec.action == "modify") action = DropoutDecision::Action::MODIFY;
                else {
                    std::cerr << "Warning: Unknown action '" << dec.action << "', skipping decision" << std::endl;
                    continue;
                }
                dropout_decisions.add_decision(
                    DropoutDecision(FieldID(dec.field_id), dec.line, 
                                   dec.start_sample, dec.end_sample, 
                                   action, dec.notes));
            }
        }
        
        // Check if we have stages that transform video data
        bool has_video_transform_stages = false;
        std::shared_ptr<DropoutCorrectStage> dropout_stage;
        
        for (const auto& stage_config : config.stages) {
            if (stage_config.enabled && stage_config.type == "dropout_correct") {
                has_video_transform_stages = true;
                std::cout << "  Enabling stage: dropout_correct" << std::endl;
                dropout_stage = create_dropout_correct_stage(stage_config.parameters);
                break;
            }
        }
        
        // Load representation
        std::cout << "\nLoading TBC representation..." << std::endl;
        auto representation = create_tbc_representation(input_tbc, input_db);
        
        // Execute stages if present to build final representation
        std::shared_ptr<VideoFieldRepresentation> final_representation = representation;
        
        if (dropout_stage) {
            std::cout << "\nExecuting dropout correction stage..." << std::endl;
            
            auto field_range = representation->field_range();
            size_t total_fields = field_range.size();
            size_t corrected_fields = 0;
            size_t total_dropouts = 0;
            
            std::cout << "  Loading dropout hints and applying corrections..." << std::endl;
            
            // Build corrected representation by processing all fields
            std::map<FieldID, std::shared_ptr<CorrectedVideoFieldRepresentation>> corrected_fields_map;
            
            for (size_t i = 0; i < total_fields; ++i) {
                FieldID field_id = field_range.start + i;
                
                // Load dropout hints directly from TBC metadata
                std::vector<DropoutRegion> dropout_regions;
                
                const auto* metadata = representation->metadata_reader();
                if (metadata) {
                    auto dropout_data = metadata->read_dropout(field_id);
                    if (dropout_data) {
                        for (const auto& dropout : dropout_data->dropouts) {
                            DropoutRegion region;
                            // Database uses 1-indexed line numbers, convert to 0-indexed
                            region.line = (dropout.line > 0) ? dropout.line - 1 : 0;
                            region.start_sample = dropout.start_sample;
                            region.end_sample = dropout.end_sample;
                            region.basis = DropoutRegion::DetectionBasis::HINT_DERIVED;
                            dropout_regions.push_back(region);
                        }
                    }
                }
                
                // Apply user decisions
                dropout_regions = dropout_decisions.apply_decisions(field_id, dropout_regions);
                
                // Correct field if it has dropouts
                if (!dropout_regions.empty()) {
                    total_dropouts += dropout_regions.size();
                    auto corrected = dropout_stage->correct_field(representation, field_id, dropout_regions);
                    if (corrected) {
                        corrected_fields_map[field_id] = corrected;
                        corrected_fields++;
                    }
                }
                
                if ((i + 1) % 100 == 0 || (i + 1) == total_fields) {
                    std::cout << "\r    Progress: " << (i + 1) << "/" << total_fields 
                             << " (" << (100 * (i + 1) / total_fields) << "%) - "
                             << corrected_fields << " fields corrected, "
                             << total_dropouts << " dropouts" << std::flush;
                }
            }
            
            std::cout << "\n  Corrected " << corrected_fields << " fields (" 
                     << total_dropouts << " dropout regions)" << std::endl;
            
            // Write output TBC by requesting fields from appropriate representation
            std::cout << "\nWriting output TBC..." << std::endl;
            std::ofstream tbc_out(output_tbc, std::ios::binary);
            if (!tbc_out) {
                throw std::runtime_error("Cannot open output TBC file for writing");
            }
            
            for (size_t i = 0; i < total_fields; ++i) {
                FieldID field_id = field_range.start + i;
                
                // Use corrected representation if available, otherwise original
                if (corrected_fields_map.count(field_id)) {
                    auto field_data = corrected_fields_map[field_id]->get_field(field_id);
                    tbc_out.write(reinterpret_cast<const char*>(field_data.data()), 
                                 field_data.size() * sizeof(uint16_t));
                } else {
                    auto field_data = representation->get_field(field_id);
                    tbc_out.write(reinterpret_cast<const char*>(field_data.data()), 
                                 field_data.size() * sizeof(uint16_t));
                }
            }
            
            tbc_out.close();
            std::cout << "  TBC file written" << std::endl;
        } else {
            // No stages - write original TBC data
            std::cout << "\nWriting output TBC..." << std::endl;
            std::ofstream tbc_out(output_tbc, std::ios::binary);
            if (!tbc_out) {
                throw std::runtime_error("Cannot open output TBC file for writing");
            }
            
            auto field_range = representation->field_range();
            for (size_t i = 0; i < field_range.size(); ++i) {
                FieldID field_id = field_range.start + i;
                auto field_data = representation->get_field(field_id);
                tbc_out.write(reinterpret_cast<const char*>(field_data.data()), 
                             field_data.size() * sizeof(uint16_t));
            }
            
            tbc_out.close();
            std::cout << "  TBC file written" << std::endl;
        }
        
        // Execute observers (always use original TBC representation for now)
        std::cout << "\nExecuting observers..." << std::endl;
        write_observations_to_db(output_db, *representation, observers, dropout_stage != nullptr);
        std::cout << "\nDone! Output written to:\n";
        std::cout << "  " << output_tbc << "\n";
        std::cout << "  " << output_db << "\n";
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
