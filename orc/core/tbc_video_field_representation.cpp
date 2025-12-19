/*
 * File:        tbc_video_field_representation.cpp
 * Module:      orc-core
 * Purpose:     TBC video field representation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "tbc_video_field_representation.h"
#include "dropout_decision.h"
#include <sstream>
#include <chrono>

namespace orc {

TBCVideoFieldRepresentation::TBCVideoFieldRepresentation(
    std::shared_ptr<TBCReader> tbc_reader,
    std::shared_ptr<TBCMetadataReader> metadata_reader,
    ArtifactID artifact_id,
    Provenance provenance
) : VideoFieldRepresentation(std::move(artifact_id), std::move(provenance)),
    tbc_reader_(std::move(tbc_reader)),
    metadata_reader_(std::move(metadata_reader)),
    last_cached_field_(FieldID::INVALID)
{
    ensure_video_parameters();
    // Metadata loaded lazily on first access
}

void TBCVideoFieldRepresentation::ensure_video_parameters() {
    if (metadata_reader_ && metadata_reader_->is_open()) {
        auto params_opt = metadata_reader_->read_video_parameters();
        if (params_opt) {
            video_params_ = *params_opt;
        }
    }
}

void TBCVideoFieldRepresentation::ensure_field_metadata() {
    if (field_metadata_cache_.empty() && metadata_reader_ && metadata_reader_->is_open()) {
        field_metadata_cache_ = metadata_reader_->read_all_field_metadata();
    }
}

FieldIDRange TBCVideoFieldRepresentation::field_range() const {
    if (!tbc_reader_ || !tbc_reader_->is_open()) {
        return FieldIDRange();
    }
    
    size_t count = tbc_reader_->get_field_count();
    return FieldIDRange(FieldID(0), FieldID(count));
}

size_t TBCVideoFieldRepresentation::field_count() const {
    if (!tbc_reader_ || !tbc_reader_->is_open()) {
        return 0;
    }
    return tbc_reader_->get_field_count();
}

bool TBCVideoFieldRepresentation::has_field(FieldID id) const {
    if (!tbc_reader_ || !tbc_reader_->is_open() || !id.is_valid()) {
        return false;
    }
    
    size_t count = tbc_reader_->get_field_count();
    return id.value() < count;
}

std::optional<FieldDescriptor> TBCVideoFieldRepresentation::get_descriptor(FieldID id) const {
    if (!has_field(id)) {
        return std::nullopt;
    }
    
    FieldDescriptor desc;
    desc.field_id = id;
    
    // Determine parity from field ID (alternating)
    desc.parity = (id.value() % 2 == 0) ? FieldParity::Top : FieldParity::Bottom;
    
    // Get format from video parameters
    switch (video_params_.system) {
        case VideoSystem::PAL:
        case VideoSystem::PAL_M:
            desc.format = VideoFormat::PAL;
            break;
        case VideoSystem::NTSC:
            desc.format = VideoFormat::NTSC;
            break;
        default:
            desc.format = VideoFormat::Unknown;
    }
    
    desc.width = video_params_.field_width;
    desc.height = video_params_.field_height;
    
    // Try to get frame number from metadata
    if (metadata_reader_) {
        auto metadata_opt = metadata_reader_->read_field_metadata(id);
        if (metadata_opt) {
            // Frame number could be derived from VBI or other metadata
            // For now, we'll leave it empty
        }
    }
    
    return desc;
}

const TBCVideoFieldRepresentation::sample_type* TBCVideoFieldRepresentation::get_line(
    FieldID id, size_t line) const {
    
    if (!has_field(id) || !tbc_reader_) {
        return nullptr;
    }
    
    // Check if we have this field cached
    if (last_cached_field_ != id) {
        // Load the entire field
        try {
            auto field_data = tbc_reader_->read_field(id);
            field_data_cache_[id] = std::move(field_data);
            last_cached_field_ = id;
        } catch (const std::exception&) {
            return nullptr;
        }
    }
    
    // Return pointer to requested line
    auto it = field_data_cache_.find(id);
    if (it == field_data_cache_.end()) {
        return nullptr;
    }
    
    size_t line_length = tbc_reader_->get_line_length();
    if (line_length == 0) {
        line_length = video_params_.field_width;
    }
    
    size_t offset = line * line_length;
    if (offset + line_length > it->second.size()) {
        return nullptr;
    }
    
    return &it->second[offset];
}

std::vector<TBCVideoFieldRepresentation::sample_type> 
TBCVideoFieldRepresentation::get_field(FieldID id) const {
    if (!has_field(id) || !tbc_reader_) {
        return {};
    }
    
    try {
        return tbc_reader_->read_field(id);
    } catch (const std::exception&) {
        return {};
    }
}

std::optional<FieldMetadata> TBCVideoFieldRepresentation::get_field_metadata(FieldID id) const {
    if (!metadata_reader_) {
        return std::nullopt;
    }
    
    // Check cache first
    auto it = field_metadata_cache_.find(id);
    if (it != field_metadata_cache_.end()) {
        return it->second;
    }
    
    // Read from database
    return metadata_reader_->read_field_metadata(id);
}

// ============================================================================
// Factory function
// ============================================================================

std::shared_ptr<TBCVideoFieldRepresentation> create_tbc_representation(
    const std::string& tbc_filename,
    const std::string& metadata_filename
) {
    // Create readers
    auto tbc_reader = std::make_shared<TBCReader>();
    auto metadata_reader = std::make_shared<TBCMetadataReader>();
    
    // Open metadata first to get parameters
    if (!metadata_reader->open(metadata_filename)) {
        return nullptr;
    }
    
    auto video_params_opt = metadata_reader->read_video_parameters();
    if (!video_params_opt) {
        return nullptr;
    }
    
    const auto& params = *video_params_opt;
    
    // Calculate field length
    size_t field_length = params.field_width * params.field_height;
    
    // Open TBC file
    if (!tbc_reader->open(tbc_filename, field_length, params.field_width)) {
        return nullptr;
    }
    
    // Create artifact ID and provenance
    std::ostringstream id_stream;
    id_stream << "tbc:" << tbc_filename;
    ArtifactID artifact_id(id_stream.str());
    
    Provenance provenance;
    provenance.stage_name = "tbc_input";
    provenance.stage_version = "1.0";
    provenance.created_at = std::chrono::system_clock::now();
    provenance.parameters["tbc_file"] = tbc_filename;
    provenance.parameters["metadata_file"] = metadata_filename;
    
    return std::make_shared<TBCVideoFieldRepresentation>(
        tbc_reader,
        metadata_reader,
        artifact_id,
        provenance
    );
}

std::vector<DropoutRegion> TBCVideoFieldRepresentation::get_dropout_hints(FieldID id) const {
    std::vector<DropoutRegion> regions;
    
    if (!metadata_reader_ || !metadata_reader_->is_open()) {
        return regions;
    }
    
    // Read dropout info from metadata
    auto dropout_infos = metadata_reader_->read_dropouts(id);
    
    // Convert DropoutInfo to DropoutRegion
    for (const auto& info : dropout_infos) {
        DropoutRegion region;
        region.line = info.line;
        region.start_sample = info.start_sample;
        region.end_sample = info.end_sample;
        region.basis = DropoutRegion::DetectionBasis::HINT_DERIVED;
        regions.push_back(region);
    }
    
    return regions;
}

} // namespace orc
