/*
 * File:        tbc_metadata_json_reader.h
 * Module:      orc-metadata
 * Purpose:     JSON-backed TBC metadata reader (Phase 4)
 *
 * Reads legacy .tbc.json metadata produced by older ld-decode / vhs-decode
 * versions directly into C++ data structures via LdDecodeMetaData.
 * No SQLite intermediate database is required.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <tbc_metadata.h>
#include <map>
#include <vector>
#include <optional>
#include <string>

namespace orc {

/**
 * @brief ITBCMetadataReader implementation backed by a legacy JSON file.
 *
 * On open() the JSON is parsed directly by LdDecodeMetaData into C++ data
 * structures stored in in-memory maps.  All reads are served from those maps;
 * no SQLite is involved.
 */
class TBCMetadataJsonReader : public ITBCMetadataReader {
public:
    TBCMetadataJsonReader();
    ~TBCMetadataJsonReader() override;

    bool open(const std::string& json_path) override;
    void close() override;
    bool is_open() const override;

    std::optional<SourceParameters>    read_video_parameters() override;
    std::optional<PcmAudioParameters>  read_pcm_audio_parameters() override;

    std::optional<FieldMetadata>             read_field_metadata(FieldID field_id) override;
    std::map<FieldID, FieldMetadata>         read_all_field_metadata() override;
    void                                     read_all_dropouts() override;
    void                                     preload_cache() override;

    std::optional<VbiData>           read_vbi(FieldID field_id) override;
    std::optional<VitcData>          read_vitc(FieldID field_id) override;
    std::optional<ClosedCaptionData> read_closed_caption(FieldID field_id) override;
    std::optional<DropoutData>       read_dropout(FieldID field_id) const override;
    std::vector<DropoutInfo>         read_dropouts(FieldID field_id) const override;

    int32_t get_field_record_count() const override;
    bool    validate_metadata(std::string* error_message = nullptr) const override;

private:
    bool is_open_ = false;
    std::optional<SourceParameters>              source_params_;
    std::optional<PcmAudioParameters>            audio_params_;
    std::map<FieldID, FieldMetadata>             field_cache_;
    std::map<FieldID, VbiData>                   vbi_cache_;
    std::map<FieldID, VitcData>                  vitc_cache_;
    std::map<FieldID, ClosedCaptionData>         cc_cache_;
    std::map<FieldID, std::vector<DropoutInfo>>  dropout_cache_;
};

} // namespace orc
