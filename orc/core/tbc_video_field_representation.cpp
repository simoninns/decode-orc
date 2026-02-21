/*
 * File:        tbc_video_field_representation.cpp
 * Module:      orc-core
 * Purpose:     TBC video field representation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "tbc_source_internal/tbc_video_field_representation.h"
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
    audio_efm_handler_(std::make_unique<TBCAudioEFMHandler>(this)),
    field_data_cache_(MAX_CACHED_TBC_FIELDS)
{
    ensure_video_parameters();
    // Metadata loaded lazily on first access
}

void TBCVideoFieldRepresentation::ensure_video_parameters() {
    if (metadata_reader_ && metadata_reader_->is_open()) {
        auto params_opt = metadata_reader_->read_video_parameters();
        if (params_opt) {
            video_params_ = *params_opt;
            
            // Apply FSC defaults if not set in metadata
            // FSC is not stored in TBC database - use standard format values
            if (video_params_.fsc <= 0.0) {
                if (video_params_.system == VideoSystem::PAL) {
                    video_params_.fsc = (283.75 * 15625.0) + 25.0;  // 4433618.75 Hz
                } else if (video_params_.system == VideoSystem::NTSC) {
                    video_params_.fsc = 315.0e6 / 88.0;  // 3579545.454... Hz
                } else if (video_params_.system == VideoSystem::PAL_M) {
                    video_params_.fsc = 5.0e6 * (63.0 / 88.0) * (909.0 / 910.0);  // ~3575611.89 Hz
                }
                ORC_LOG_DEBUG("TBCVideoFieldRepresentation: Applied format-default FSC = {} Hz", 
                             video_params_.fsc);
            }
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
    
    // Calculate standards-compliant field height based on parity hint
    // Try to get field parity from TBC metadata (which knows if this is first/second field)
    bool is_first_field = false;
    auto parity_hint = get_field_parity_hint(id);
    if (parity_hint.has_value()) {
        is_first_field = parity_hint->is_first_field;
        ORC_LOG_TRACE("TBCVideoFieldRepresentation: Field {} parity hint: is_first_field={}", 
                  id.value(), is_first_field);
    } else {
        // Fallback: infer from field ID (even ID = first field)
        is_first_field = (id.value() % 2 == 0);
        ORC_LOG_WARN("TBCVideoFieldRepresentation: No parity hint for field {}, using ID-based inference", 
                     id.value());
    }
    
    // Use standards-compliant height (VFR representation - no padding)
    desc.height = calculate_standard_field_height(video_params_.system, is_first_field);
    
    ORC_LOG_TRACE("TBCVideoFieldRepresentation: Field {} descriptor: is_first_field={}, height={}", 
                  id.value(), is_first_field, desc.height);
    
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
    
    // Validate line number against standards-compliant height (not padded height)
    auto descriptor = get_descriptor(id);
    if (!descriptor || line >= descriptor->height) {
        // Line number exceeds actual field height
        return nullptr;
    }
    
    // Check LRU cache for this field using get_ptr to avoid copying
    const auto* cached_field = field_data_cache_.get_ptr(id);
    if (!cached_field) {
        // Load the entire field and cache it
        try {
            auto field_data = tbc_reader_->read_field(id);
            field_data_cache_.put(id, std::move(field_data));
            cached_field = field_data_cache_.get_ptr(id);
            if (!cached_field) {
                return nullptr;
            }
        } catch (const std::exception&) {
            return nullptr;
        }
    }
    
    // Return pointer to requested line
    size_t line_length = tbc_reader_->get_line_length();
    if (line_length == 0) {
        line_length = video_params_.field_width;
    }
    
    size_t offset = line * line_length;
    if (offset + line_length > cached_field->size()) {
        return nullptr;
    }
    
    return &(*cached_field)[offset];
}

std::vector<TBCVideoFieldRepresentation::sample_type> 
TBCVideoFieldRepresentation::get_field(FieldID id) const {
    if (!has_field(id) || !tbc_reader_) {
        return {};
    }
    
    // Get standards-compliant field height (may be less than TBC padded height)
    auto descriptor = get_descriptor(id);
    if (!descriptor) {
        return {};
    }
    
    try {
        // Read full field from TBC (may contain padding)
        auto field_data = tbc_reader_->read_field(id);
        
        // Calculate how many samples we should return (actual lines only, no padding)
        size_t line_length = tbc_reader_->get_line_length();
        if (line_length == 0) {
            line_length = video_params_.field_width;
        }
        
        size_t actual_samples = descriptor->height * line_length;
        
        // Truncate to actual field height (remove TBC padding)
        if (field_data.size() > actual_samples) {
            field_data.resize(actual_samples);
            ORC_LOG_DEBUG("TBCVideoFieldRepresentation: Truncated field {} from {} to {} samples (removed padding)",
                         id.value(), field_data.size() + (field_data.capacity() - field_data.size()), 
                         actual_samples);
        }
        
        return field_data;
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
    const std::string& metadata_filename,
    const std::string& pcm_filename,
    const std::string& efm_filename
) {
    // Create readers
    auto tbc_reader = std::make_shared<TBCReader>();
    auto metadata_reader = std::make_shared<TBCMetadataReader>();
    
    // Open metadata first to get parameters
    if (!metadata_reader->open(metadata_filename)) {
        ORC_LOG_ERROR("Failed to open TBC metadata: {}", metadata_filename);
        return nullptr;
    }
    
    // Preload metadata cache (field metadata and dropouts) to avoid lazy loading during analysis
    metadata_reader->preload_cache();
    
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
    
    // Calculate padded field length used in TBC files (parity-aware via video system)
    const size_t padded_field_height = calculate_padded_field_height(params.system);
    if (padded_field_height == 0) {
        ORC_LOG_ERROR("Unsupported or unknown video system in metadata: {}", metadata_filename);
        return nullptr;
    }
    size_t field_length = static_cast<size_t>(params.field_width) * padded_field_height;
    
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
    
    auto representation = std::make_shared<TBCVideoFieldRepresentation>(
        tbc_reader,
        metadata_reader,
        artifact_id,
        provenance
    );
    
    // Set audio file if provided
    if (!pcm_filename.empty()) {
        provenance.parameters["pcm_file"] = pcm_filename;
        if (!representation->set_audio_file(pcm_filename)) {
            ORC_LOG_WARN("Failed to set PCM audio file, continuing without audio");
        }
    }
    
    // Set EFM file if provided
    if (!efm_filename.empty()) {
        provenance.parameters["efm_file"] = efm_filename;
        if (!representation->set_efm_file(efm_filename)) {
            ORC_LOG_WARN("Failed to set EFM data file, continuing without EFM");
        }
    }
    
    return representation;
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

std::optional<FieldPhaseHint> TBCVideoFieldRepresentation::get_field_phase_hint(FieldID id) const {
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
    FieldPhaseHint hint;
    hint.field_phase_id = metadata.field_phase_id.value();
    hint.source = HintSource::METADATA;
    hint.confidence_pct = HintTraits::METADATA_CONFIDENCE;
    
    return hint;
}

std::optional<ActiveLineHint> TBCVideoFieldRepresentation::get_active_line_hint() const {
    // Active line ranges are constant for the video source (not per-field)
    // They come from video parameters
    if (!video_params_.is_valid()) {
        return std::nullopt;
    }
    
    // Provide both frame-based and field-based active line hints
    // Source stage sets frame-based values from metadata, calculates field-based from them
    if (video_params_.first_active_frame_line >= 0 && 
        video_params_.last_active_frame_line >= 0) {
        ActiveLineHint hint;
        // Frame-based values from metadata
        hint.first_active_frame_line = video_params_.first_active_frame_line;
        hint.last_active_frame_line = video_params_.last_active_frame_line;
        // Field-based values calculated from frame-based (divide by 2)
        hint.first_active_field_line = video_params_.first_active_field_line;
        hint.last_active_field_line = video_params_.last_active_field_line;
        hint.source = HintSource::METADATA;
        hint.confidence_pct = HintTraits::METADATA_CONFIDENCE;
        return hint;
    }
    
    return std::nullopt;
}

// ============================================================================
// Audio interface implementation
// ============================================================================

uint32_t TBCVideoFieldRepresentation::get_audio_sample_count(FieldID id) const {
    return audio_efm_handler_->get_audio_sample_count(id);
}

std::vector<int16_t> TBCVideoFieldRepresentation::get_audio_samples(FieldID id) const {
    return audio_efm_handler_->get_audio_samples(id);
}

bool TBCVideoFieldRepresentation::has_audio() const {
    return audio_efm_handler_->has_audio();
}

bool TBCVideoFieldRepresentation::set_audio_file(const std::string& pcm_path) {
    // Ensure metadata is loaded before setting audio file
    ensure_field_metadata();
    return audio_efm_handler_->set_audio_file(pcm_path);
}

// ============================================================================
// EFM interface implementation
// ============================================================================

uint32_t TBCVideoFieldRepresentation::get_efm_sample_count(FieldID id) const {
    return audio_efm_handler_->get_efm_sample_count(id);
}

std::vector<uint8_t> TBCVideoFieldRepresentation::get_efm_samples(FieldID id) const {
    return audio_efm_handler_->get_efm_samples(id);
}

bool TBCVideoFieldRepresentation::has_efm() const {
    return audio_efm_handler_->has_efm();
}

bool TBCVideoFieldRepresentation::set_efm_file(const std::string& efm_path) {
    // Ensure metadata is loaded before setting EFM file
    ensure_field_metadata();
    return audio_efm_handler_->set_efm_file(efm_path);
}

} // namespace orc
