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
#include "logging.h"
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
        ORC_LOG_ERROR("Failed to open TBC metadata: {}", metadata_filename);
        return nullptr;
    }
    
    // Validate metadata consistency before proceeding
    std::string validation_error;
    if (!metadata_reader->validate_metadata(&validation_error)) {
        ORC_LOG_ERROR("TBC metadata validation failed: {}", validation_error);
        ORC_LOG_ERROR("  Metadata file: {}", metadata_filename);
        ORC_LOG_ERROR("  TBC file: {}", tbc_filename);
        return nullptr;
    }
    
    auto video_params_opt = metadata_reader->read_video_parameters();
    if (!video_params_opt) {
        ORC_LOG_ERROR("Failed to read video parameters from metadata: {}", metadata_filename);
        return nullptr;
    }
    
    const auto& params = *video_params_opt;
    
    // Calculate field length
    size_t field_length = params.field_width * params.field_height;
    
    // Open TBC file
    if (!tbc_reader->open(tbc_filename, field_length, params.field_width)) {
        ORC_LOG_ERROR("Failed to open TBC file: {}", tbc_filename);
        return nullptr;
    }
    
    // Validate TBC file size matches metadata field count
    size_t file_field_count = tbc_reader->get_field_count();
    size_t metadata_field_count = static_cast<size_t>(params.number_of_sequential_fields);
    
    if (file_field_count != metadata_field_count) {
        size_t field_size = field_length * sizeof(uint16_t);
        size_t expected_file_size = metadata_field_count * field_size;
        size_t actual_file_size = file_field_count * field_size;
        
        ORC_LOG_ERROR("TBC file size mismatch!");
        ORC_LOG_ERROR("  TBC file: {}", tbc_filename);
        ORC_LOG_ERROR("  File contains {} fields ({} bytes)", file_field_count, actual_file_size);
        ORC_LOG_ERROR("  Metadata specifies {} fields ({} bytes expected)", 
                     metadata_field_count, expected_file_size);
        ORC_LOG_ERROR("  The TBC file and metadata are inconsistent.");
        ORC_LOG_ERROR("  This file may be corrupted or truncated. Please regenerate the TBC file.");
        return nullptr;
    }
    
    ORC_LOG_DEBUG("TBC validation passed: {} fields, {}x{} pixels", 
                 metadata_field_count, params.field_width, params.field_height);
    
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

std::optional<FieldParityHint> TBCVideoFieldRepresentation::get_field_parity_hint(FieldID id) const {
    if (!metadata_reader_ || !metadata_reader_->is_open()) {
        return std::nullopt;
    }
    
    // Get field metadata from TBC database
    auto metadata_opt = get_field_metadata(id);
    if (!metadata_opt) {
        return std::nullopt;
    }
    
    const auto& metadata = metadata_opt.value();
    
    // Check if is_first_field is available in the metadata
    if (!metadata.is_first_field.has_value()) {
        return std::nullopt;
    }
    
    // Create hint from metadata
    FieldParityHint hint;
    hint.is_first_field = metadata.is_first_field.value();
    hint.source = HintSource::METADATA;
    hint.confidence_pct = HintTraits::METADATA_CONFIDENCE;
    
    return hint;
}

std::optional<PALPhaseHint> TBCVideoFieldRepresentation::get_pal_phase_hint(FieldID id) const {
    if (!metadata_reader_ || !metadata_reader_->is_open()) {
        return std::nullopt;
    }
    
    // Get field metadata from TBC database
    auto metadata_opt = get_field_metadata(id);
    if (!metadata_opt) {
        return std::nullopt;
    }
    
    const auto& metadata = metadata_opt.value();
    
    // Check if field_phase_id is available in the metadata
    if (!metadata.field_phase_id.has_value()) {
        return std::nullopt;
    }
    
    // Create hint from metadata
    PALPhaseHint hint;
    hint.field_phase_id = metadata.field_phase_id.value();
    hint.source = HintSource::METADATA;
    hint.confidence_pct = HintTraits::METADATA_CONFIDENCE;
    
    return hint;
}

std::vector<std::shared_ptr<Observation>> TBCVideoFieldRepresentation::get_observations(FieldID id) const {
    // TODO: Implement reading observations from TBC metadata database
    // This would convert metadata (VBI, VITC, field parity, etc.) into Observation objects
    // For now, return empty - observers will compute fresh from TBC signal data
    // 
    // Future implementation:
    // - Read field_record for is_first_field → FieldParityObservation
    // - Read vbi table → BiphaseObservation
    // - Read vitc table → VitcObservation
    // - Read closed_caption → ClosedCaptionObservation
    // - Read vits_metrics → VITSQualityObservation
    // - etc.
    
    (void)id;  // Unused for now
    return {};
}

} // namespace orc
