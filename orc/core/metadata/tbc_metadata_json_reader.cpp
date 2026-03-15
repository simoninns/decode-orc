/*
 * File:        tbc_metadata_json_reader.cpp
 * Module:      orc-metadata
 * Purpose:     JSON-backed TBC metadata reader (Phase 4)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <tbc_metadata_json_reader.h>
#include "logging.h"
#include "lddecodemetadata.h"

namespace orc {

TBCMetadataJsonReader::TBCMetadataJsonReader() = default;

TBCMetadataJsonReader::~TBCMetadataJsonReader()
{
    close();
}

bool TBCMetadataJsonReader::open(const std::string& json_path)
{
    close();

    LdDecodeMetaData meta;
    if (!meta.read(json_path)) {
        ORC_LOG_ERROR("TBCMetadataJsonReader: failed to read JSON metadata from {}", json_path);
        return false;
    }

    // ---- Source (video) parameters ----
    const auto& vp = meta.getVideoParameters();
    SourceParameters sp;

    switch (vp.system) {
        case LdVideoSystem::PAL:   sp.system = VideoSystem::PAL;   break;
        case LdVideoSystem::NTSC:  sp.system = VideoSystem::NTSC;  break;
        case LdVideoSystem::PAL_M: sp.system = VideoSystem::PAL_M; break;
    }

    sp.sample_rate                = vp.sampleRate;
    sp.active_video_start         = vp.activeVideoStart;
    sp.active_video_end           = vp.activeVideoEnd;
    sp.field_width                = vp.fieldWidth;
    sp.field_height               = vp.fieldHeight;
    sp.number_of_sequential_fields = vp.numberOfSequentialFields;
    sp.colour_burst_start         = vp.colourBurstStart;
    sp.colour_burst_end           = vp.colourBurstEnd;
    sp.is_mapped                  = vp.isMapped;
    sp.is_subcarrier_locked       = vp.isSubcarrierLocked;
    sp.is_widescreen              = vp.isWidescreen;
    sp.white_16b_ire              = vp.white16bIre;
    sp.black_16b_ire              = vp.black16bIre;
    sp.blanking_16b_ire           = vp.black16bIre; // legacy JSON: no blanking level, use black
    sp.git_branch                 = vp.gitBranch;
    sp.git_commit                 = vp.gitCommit;
    sp.tape_format                = vp.tapeFormat;
    sp.decoder                    = "ld-decode";
    sp.fsc                        = -1.0; // not stored; computed by source stage from video system

    // Active line boundaries — mirrors TBCMetadataSqliteReader::read_video_parameters()
    if (sp.system == VideoSystem::PAL) {
        sp.first_active_frame_line = 44;
        sp.last_active_frame_line  = 620;
        sp.first_active_field_line = 22;
        sp.last_active_field_line  = 310;
    } else { // NTSC and PAL_M
        sp.first_active_frame_line = 40;
        sp.last_active_frame_line  = 525;
        sp.first_active_field_line = 20;
        sp.last_active_field_line  = 259;
    }

    source_params_ = sp;

    // ---- PCM audio parameters ----
    const auto& ap = meta.getPcmAudioParameters();
    if (ap.sampleRate > 0) {
        PcmAudioParameters orc_ap;
        orc_ap.sample_rate      = ap.sampleRate;
        orc_ap.bits             = ap.bits;
        orc_ap.is_signed        = ap.isSigned;
        orc_ap.is_little_endian = ap.isLittleEndian;
        audio_params_ = orc_ap;
    }

    // ---- Per-field data ----
    // Field IDs are 0-based (fieldNum 1 → FieldID 0), matching the SQLite reader.
    int32_t num_fields = meta.getNumberOfFields();
    for (int32_t field_num = 1; field_num <= num_fields; ++field_num) {
        const auto& f  = meta.getField(field_num);
        FieldID fid(static_cast<FieldID::value_type>(field_num - 1));

        // Core field record — mirrors read_all_field_metadata() SQLite query columns
        FieldMetadata fm;
        fm.seq_no          = static_cast<int32_t>(fid.value());
        fm.is_first_field  = f.isFirstField;
        fm.sync_confidence = f.syncConf;
        fm.median_burst_ire = f.medianBurstIRE;
        fm.field_phase_id  = f.fieldPhaseID;
        fm.is_pad          = f.pad;
        if (f.audioSamples > 0) fm.audio_samples = f.audioSamples;
        if (f.decodeFaults > 0) fm.decode_faults  = f.decodeFaults;
        if (f.diskLoc > 0.0)    fm.disk_location  = f.diskLoc;
        if (f.efmTValues > 0)   fm.efm_t_values   = f.efmTValues;
        if (f.fileLoc > 0)      fm.file_location  = f.fileLoc;
        field_cache_[fid] = fm;

        // VBI
        if (f.vbi.inUse) {
            VbiData orc_vbi;
            orc_vbi.in_use   = true;
            orc_vbi.vbi_data = f.vbi.vbiData;
            vbi_cache_[fid]  = orc_vbi;
        }

        // VITC
        if (f.vitc.inUse) {
            VitcData orc_vitc;
            orc_vitc.in_use    = true;
            orc_vitc.vitc_data = f.vitc.vitcData;
            vitc_cache_[fid]   = orc_vitc;
        }

        // Closed caption
        if (f.closedCaption.inUse) {
            ClosedCaptionData orc_cc;
            orc_cc.in_use = true;
            orc_cc.data0  = f.closedCaption.data0;
            orc_cc.data1  = f.closedCaption.data1;
            cc_cache_[fid] = orc_cc;
        }

        // Dropouts (legacy JSON uses 1-based line numbers; convert to 0-based to match SQLite reader)
        const auto& dos = f.dropOuts;
        if (dos.size() > 0) {
            std::vector<DropoutInfo> orc_dos;
            orc_dos.reserve(static_cast<size_t>(dos.size()));
            for (int32_t i = 0; i < dos.size(); ++i) {
                DropoutInfo di;
                di.line         = static_cast<uint32_t>(dos.fieldLine(i)) - 1;
                di.start_sample = static_cast<uint32_t>(dos.startx(i));
                di.end_sample   = static_cast<uint32_t>(dos.endx(i));
                orc_dos.push_back(di);
            }
            dropout_cache_[fid] = std::move(orc_dos);
        }
    }

    is_open_ = true;
    ORC_LOG_INFO("TBCMetadataJsonReader: loaded {} fields from {}", num_fields, json_path);
    return true;
}

void TBCMetadataJsonReader::close()
{
    is_open_ = false;
    source_params_.reset();
    audio_params_.reset();
    field_cache_.clear();
    vbi_cache_.clear();
    vitc_cache_.clear();
    cc_cache_.clear();
    dropout_cache_.clear();
}

bool TBCMetadataJsonReader::is_open() const
{
    return is_open_;
}

std::optional<SourceParameters> TBCMetadataJsonReader::read_video_parameters()
{
    return source_params_;
}

std::optional<PcmAudioParameters> TBCMetadataJsonReader::read_pcm_audio_parameters()
{
    return audio_params_;
}

std::optional<FieldMetadata> TBCMetadataJsonReader::read_field_metadata(FieldID field_id)
{
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }
    auto it = field_cache_.find(field_id);
    if (it != field_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::map<FieldID, FieldMetadata> TBCMetadataJsonReader::read_all_field_metadata()
{
    return field_cache_;
}

void TBCMetadataJsonReader::read_all_dropouts()
{
    // All dropouts are already loaded eagerly in open(); nothing to do.
}

void TBCMetadataJsonReader::preload_cache()
{
    // All data is loaded eagerly in open(); nothing to do.
}

std::optional<VbiData> TBCMetadataJsonReader::read_vbi(FieldID field_id)
{
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }
    auto it = vbi_cache_.find(field_id);
    if (it != vbi_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<VitcData> TBCMetadataJsonReader::read_vitc(FieldID field_id)
{
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }
    auto it = vitc_cache_.find(field_id);
    if (it != vitc_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<ClosedCaptionData> TBCMetadataJsonReader::read_closed_caption(FieldID field_id)
{
    if (!is_open_ || !field_id.is_valid()) {
        return std::nullopt;
    }
    auto it = cc_cache_.find(field_id);
    if (it != cc_cache_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::optional<DropoutData> TBCMetadataJsonReader::read_dropout(FieldID field_id) const
{
    auto dropouts = read_dropouts(field_id);
    if (dropouts.empty()) {
        return std::nullopt;
    }
    DropoutData data;
    data.dropouts = std::move(dropouts);
    return data;
}

std::vector<DropoutInfo> TBCMetadataJsonReader::read_dropouts(FieldID field_id) const
{
    if (!is_open_ || !field_id.is_valid()) {
        return {};
    }
    auto it = dropout_cache_.find(field_id);
    if (it != dropout_cache_.end()) {
        return it->second;
    }
    return {};
}

int32_t TBCMetadataJsonReader::get_field_record_count() const
{
    return static_cast<int32_t>(field_cache_.size());
}

bool TBCMetadataJsonReader::validate_metadata(std::string* error_message) const
{
    if (!is_open_) {
        if (error_message) *error_message = "Metadata is not open";
        ORC_LOG_ERROR("validate_metadata: {}", error_message ? *error_message : "not open");
        return false;
    }

    if (!source_params_) {
        if (error_message) *error_message = "Failed to read video parameters from metadata";
        ORC_LOG_ERROR("validate_metadata: {} - check debug logs for details",
                      error_message ? *error_message : "Unknown error");
        return false;
    }

    const auto& params = *source_params_;

    if (params.number_of_sequential_fields <= 0) {
        if (error_message) {
            *error_message = "Metadata does not specify valid number_of_sequential_fields (" +
                             std::to_string(params.number_of_sequential_fields) + ")";
        }
        ORC_LOG_ERROR("validate_metadata: {}",
                      error_message ? *error_message : "invalid number_of_sequential_fields");
        return false;
    }

    int32_t field_count = get_field_record_count();
    if (field_count != params.number_of_sequential_fields) {
        if (error_message) {
            *error_message = "Metadata inconsistency: video parameters specifies " +
                             std::to_string(params.number_of_sequential_fields) +
                             " fields, but " + std::to_string(field_count) + " field records loaded. "
                             "This TBC file has inconsistent metadata, likely from a buggy "
                             "ld-decode version or interrupted capture.";
        }
        return false;
    }

    if (params.field_width <= 0 || params.field_height <= 0) {
        if (error_message) {
            *error_message = "Invalid field dimensions: " +
                             std::to_string(params.field_width) + "x" +
                             std::to_string(params.field_height);
        }
        return false;
    }

    if (params.system == VideoSystem::Unknown) {
        if (error_message) *error_message = "Unknown or unsupported video system";
        return false;
    }

    return true;
}

} // namespace orc
