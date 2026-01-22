/*
 * File:        tbc_yc_video_field_representation.cpp
 * Module:      orc-core
 * Purpose:     TBC YC (separate Y and C) video field representation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "tbc_yc_video_field_representation.h"
#include "dropout_decision.h"
#include "logging.h"
// TODO: Observer system refactored - old observers removed
#include <sstream>
#include <chrono>

namespace orc {

TBCYCVideoFieldRepresentation::TBCYCVideoFieldRepresentation(
    std::shared_ptr<TBCReader> y_reader,
    std::shared_ptr<TBCReader> c_reader,
    std::shared_ptr<TBCMetadataReader> metadata_reader,
    ArtifactID artifact_id,
    Provenance provenance
) : VideoFieldRepresentation(std::move(artifact_id), std::move(provenance)),
    y_reader_(std::move(y_reader)),
    c_reader_(std::move(c_reader)),
    metadata_reader_(std::move(metadata_reader)),
    has_audio_(false),
    has_efm_(false),
    y_field_data_cache_(MAX_CACHED_TBC_FIELDS),
    c_field_data_cache_(MAX_CACHED_TBC_FIELDS)
{
    ensure_video_parameters();
    // Metadata loaded lazily on first access
}

void TBCYCVideoFieldRepresentation::ensure_video_parameters() {
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
                ORC_LOG_DEBUG("TBCYCVideoFieldRepresentation: Applied format-default FSC = {} Hz", 
                             video_params_.fsc);
            }
        }
    }
}

void TBCYCVideoFieldRepresentation::ensure_field_metadata() {
    if (field_metadata_cache_.empty() && metadata_reader_ && metadata_reader_->is_open()) {
        field_metadata_cache_ = metadata_reader_->read_all_field_metadata();
    }
}

FieldIDRange TBCYCVideoFieldRepresentation::field_range() const {
    if (!y_reader_ || !y_reader_->is_open()) {
        return FieldIDRange();
    }
    
    size_t count = y_reader_->get_field_count();
    return FieldIDRange(FieldID(0), FieldID(count));
}

size_t TBCYCVideoFieldRepresentation::field_count() const {
    if (!y_reader_ || !y_reader_->is_open()) {
        return 0;
    }
    return y_reader_->get_field_count();
}

bool TBCYCVideoFieldRepresentation::has_field(FieldID id) const {
    if (!y_reader_ || !y_reader_->is_open() || !id.is_valid()) {
        return false;
    }
    
    size_t count = y_reader_->get_field_count();
    return id.value() < count;
}

std::optional<FieldDescriptor> TBCYCVideoFieldRepresentation::get_descriptor(FieldID id) const {
    if (!has_field(id)) {
        return std::nullopt;
    }
    
    FieldDescriptor desc;
    desc.field_id = id;
    
    // Determine parity from field ID (alternating)
    desc.parity = (id.value() % 2 == 0) ? FieldParity::Top : FieldParity::Bottom;
    
    // Get format from video parameters
    desc.format = VideoFormat::Unknown;
    if (video_params_.is_valid()) {
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
    }
    
    return desc;
}

// Composite access - not supported for YC sources
const TBCYCVideoFieldRepresentation::sample_type* 
TBCYCVideoFieldRepresentation::get_line(FieldID /*id*/, size_t /*line*/) const {
    // YC sources don't provide composite (Y+C modulated) data
    // Downstream stages should use get_line_luma() and get_line_chroma() instead
    return nullptr;
}

std::vector<TBCYCVideoFieldRepresentation::sample_type> 
TBCYCVideoFieldRepresentation::get_field(FieldID /*id*/) const {
    // YC sources don't provide composite (Y+C modulated) data
    // Downstream stages should use get_field_luma() and get_field_chroma() instead
    return {};
}

// YC dual-channel access - the primary interface for YC sources
const TBCYCVideoFieldRepresentation::sample_type* 
TBCYCVideoFieldRepresentation::get_line_luma(FieldID id, size_t line) const {
    if (!has_field(id)) {
        return nullptr;
    }
    
    // Check if we have the full field cached
    const auto* cached_field = y_field_data_cache_.get_ptr(id);
    if (cached_field) {
        // Return pointer to the line within the cached field
        size_t width = static_cast<size_t>(video_params_.field_width);
        if (line >= static_cast<size_t>(video_params_.field_height)) {
            return nullptr;
        }
        return cached_field->data() + (line * width);
    }
    
    // Field not cached - load it
    auto field_data = get_field_luma(id);
    if (field_data.empty()) {
        return nullptr;
    }
    
    // Cache it for future access
    y_field_data_cache_.put(id, std::move(field_data));
    
    // Return pointer to the requested line
    size_t width = static_cast<size_t>(video_params_.field_width);
    if (line >= static_cast<size_t>(video_params_.field_height)) {
        return nullptr;
    }
    
    // Get from cache (we just put it there)
    cached_field = y_field_data_cache_.get_ptr(id);
    if (!cached_field) {
        return nullptr;
    }
    
    return cached_field->data() + (line * width);
}

const TBCYCVideoFieldRepresentation::sample_type* 
TBCYCVideoFieldRepresentation::get_line_chroma(FieldID id, size_t line) const {
    if (!has_field(id)) {
        return nullptr;
    }
    
    // Check if we have the full field cached
    const auto* cached_field = c_field_data_cache_.get_ptr(id);
    if (cached_field) {
        // Return pointer to the line within the cached field
        size_t width = static_cast<size_t>(video_params_.field_width);
        if (line >= static_cast<size_t>(video_params_.field_height)) {
            return nullptr;
        }
        return cached_field->data() + (line * width);
    }
    
    // Field not cached - load it
    auto field_data = get_field_chroma(id);
    if (field_data.empty()) {
        return nullptr;
    }
    
    // Cache it for future access
    c_field_data_cache_.put(id, std::move(field_data));
    
    // Return pointer to the requested line
    size_t width = static_cast<size_t>(video_params_.field_width);
    if (line >= static_cast<size_t>(video_params_.field_height)) {
        return nullptr;
    }
    
    // Get from cache (we just put it there)
    cached_field = c_field_data_cache_.get_ptr(id);
    if (!cached_field) {
        return nullptr;
    }
    
    return cached_field->data() + (line * width);
}

std::vector<TBCYCVideoFieldRepresentation::sample_type> 
TBCYCVideoFieldRepresentation::get_field_luma(FieldID id) const {
    if (!has_field(id) || !y_reader_->is_open()) {
        return {};
    }
    
    // Check cache first
    auto cached_field = y_field_data_cache_.get(id);
    if (cached_field) {
        return *cached_field;
    }
    
    // Read from Y file
    ORC_LOG_DEBUG("TBCYCVideoFieldRepresentation: Reading luma field {} from Y reader", id.value());
    auto field_data = y_reader_->read_field(id);
    
    if (field_data.empty()) {
        ORC_LOG_ERROR("Failed to read luma field {} from Y file", id.value());
        return {};
    }
    
    // Debug: Log first few samples
    ORC_LOG_DEBUG("Luma field {} first samples: {} {} {} {} {}", 
                  id.value(), field_data[0], field_data[1], field_data[2], field_data[3], field_data[4]);
    
    // Cache and return
    y_field_data_cache_.put(id, field_data);
    return field_data;
}

std::vector<TBCYCVideoFieldRepresentation::sample_type> 
TBCYCVideoFieldRepresentation::get_field_chroma(FieldID id) const {
    if (!has_field(id) || !c_reader_->is_open()) {
        return {};
    }
    
    // Check cache first
    auto cached_field = c_field_data_cache_.get(id);
    if (cached_field) {
        return *cached_field;
    }
    
    // Read from C file
    ORC_LOG_DEBUG("TBCYCVideoFieldRepresentation: Reading chroma field {} from C reader", id.value());
    auto field_data = c_reader_->read_field(id);
    
    if (field_data.empty()) {
        ORC_LOG_ERROR("Failed to read chroma field {} from C file", id.value());
        return {};
    }
    
    // Debug: Log first few samples
    ORC_LOG_DEBUG("Chroma field {} first samples: {} {} {} {} {}", 
                  id.value(), field_data[0], field_data[1], field_data[2], field_data[3], field_data[4]);
    
    // Cache and return
    c_field_data_cache_.put(id, field_data);
    return field_data;
}

std::optional<FieldMetadata> TBCYCVideoFieldRepresentation::get_field_metadata(FieldID id) const {
    if (!metadata_reader_ || !metadata_reader_->is_open()) {
        return std::nullopt;
    }
    
    // Check cache first
    const_cast<TBCYCVideoFieldRepresentation*>(this)->ensure_field_metadata();
    
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

std::shared_ptr<TBCYCVideoFieldRepresentation> create_tbc_yc_representation(
    const std::string& y_filename,
    const std::string& c_filename,
    const std::string& metadata_filename,
    const std::string& pcm_filename,
    const std::string& efm_filename
) {
    // Create readers
    auto y_reader = std::make_shared<TBCReader>();
    auto c_reader = std::make_shared<TBCReader>();
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
        ORC_LOG_ERROR("  Y file: {}", y_filename);
        ORC_LOG_ERROR("  C file: {}", c_filename);
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
    
    // Open Y (luma) file
    ORC_LOG_DEBUG("Opening Y (luma) file: {}", y_filename);
    if (!y_reader->open(y_filename, field_length, params.field_width)) {
        ORC_LOG_ERROR("Failed to open Y (luma) TBC file: {}", y_filename);
        return nullptr;
    }
    ORC_LOG_DEBUG("Y file opened successfully, {} fields detected", y_reader->get_field_count());
    
    // Open C (chroma) file
    ORC_LOG_DEBUG("Opening C (chroma) file: {}", c_filename);
    if (!c_reader->open(c_filename, field_length, params.field_width)) {
        ORC_LOG_ERROR("Failed to open C (chroma) TBC file: {}", c_filename);
        return nullptr;
    }
    ORC_LOG_DEBUG("C file opened successfully, {} fields detected", c_reader->get_field_count());
    
    // Validate Y and C files have matching field counts
    size_t y_field_count = y_reader->get_field_count();
    size_t c_field_count = c_reader->get_field_count();
    size_t metadata_field_count = static_cast<size_t>(params.number_of_sequential_fields);
    
    if (y_field_count != c_field_count) {
        ORC_LOG_ERROR("YC field count mismatch!");
        ORC_LOG_ERROR("  Y file: {} contains {} fields", y_filename, y_field_count);
        ORC_LOG_ERROR("  C file: {} contains {} fields", c_filename, c_field_count);
        ORC_LOG_ERROR("  Y and C files must have IDENTICAL field counts.");
        return nullptr;
    }
    
    if (y_field_count != metadata_field_count) {
        size_t field_size = field_length * sizeof(uint16_t);
        size_t expected_file_size = metadata_field_count * field_size;
        size_t actual_y_file_size = y_field_count * field_size;
        size_t actual_c_file_size = c_field_count * field_size;
        
        ORC_LOG_ERROR("YC file size mismatch with metadata!");
        ORC_LOG_ERROR("  Y file: {} contains {} fields ({} bytes)", 
                     y_filename, y_field_count, actual_y_file_size);
        ORC_LOG_ERROR("  C file: {} contains {} fields ({} bytes)", 
                     c_filename, c_field_count, actual_c_file_size);
        ORC_LOG_ERROR("  Metadata specifies {} fields ({} bytes expected)", 
                     metadata_field_count, expected_file_size);
        ORC_LOG_ERROR("  The Y/C files and metadata are inconsistent.");
        ORC_LOG_ERROR("  These files may be corrupted or truncated. Please regenerate the YC files.");
        return nullptr;
    }
    
    ORC_LOG_DEBUG("YC validation passed: {} fields, {}x{} pixels", 
                 metadata_field_count, params.field_width, params.field_height);
    
    // Create artifact ID and provenance
    std::ostringstream id_stream;
    id_stream << "tbc_yc:" << y_filename << "+" << c_filename;
    ArtifactID artifact_id(id_stream.str());
    
    Provenance provenance;
    provenance.stage_name = "tbc_yc_input";
    provenance.stage_version = "1.0";
    provenance.created_at = std::chrono::system_clock::now();
    provenance.parameters["y_file"] = y_filename;
    provenance.parameters["c_file"] = c_filename;
    provenance.parameters["metadata_file"] = metadata_filename;
    
    auto representation = std::make_shared<TBCYCVideoFieldRepresentation>(
        y_reader,
        c_reader,
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

std::vector<DropoutRegion> TBCYCVideoFieldRepresentation::get_dropout_hints(FieldID id) const {
    std::vector<DropoutRegion> regions;
    
    if (!metadata_reader_ || !metadata_reader_->is_open()) {
        return regions;
    }
    
    // Read dropout info from metadata
    // NOTE: The same dropout map applies to both Y and C channels
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

std::optional<FieldParityHint> TBCYCVideoFieldRepresentation::get_field_parity_hint(FieldID id) const {
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

std::optional<FieldPhaseHint> TBCYCVideoFieldRepresentation::get_field_phase_hint(FieldID id) const {
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

std::optional<ActiveLineHint> TBCYCVideoFieldRepresentation::get_active_line_hint() const {
    // Active line ranges are constant for the video source (not per-field)
    // They come from video parameters
    if (!video_params_.is_valid()) {
        return std::nullopt;
    }
    
    // Provide both frame-based and field-based active line hints
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

// Audio interface - same as composite TBC sources
uint32_t TBCYCVideoFieldRepresentation::get_audio_sample_count(FieldID /*id*/) const {
    // Audio support to be implemented (same as TBCVideoFieldRepresentation)
    return 0;
}

std::vector<int16_t> TBCYCVideoFieldRepresentation::get_audio_samples(FieldID /*id*/) const {
    // Audio support to be implemented (same as TBCVideoFieldRepresentation)
    return {};
}

bool TBCYCVideoFieldRepresentation::has_audio() const {
    return has_audio_;
}

bool TBCYCVideoFieldRepresentation::set_audio_file(const std::string& pcm_path) {
    // Audio support to be implemented (same as TBCVideoFieldRepresentation)
    pcm_audio_path_ = pcm_path;
    has_audio_ = false;  // TODO: Implement audio loading
    return false;
}

// EFM interface - same as composite TBC sources
uint32_t TBCYCVideoFieldRepresentation::get_efm_sample_count(FieldID /*id*/) const {
    // EFM support to be implemented (same as TBCVideoFieldRepresentation)
    return 0;
}

std::vector<uint8_t> TBCYCVideoFieldRepresentation::get_efm_samples(FieldID /*id*/) const {
    // EFM support to be implemented (same as TBCVideoFieldRepresentation)
    return {};
}

bool TBCYCVideoFieldRepresentation::has_efm() const {
    return has_efm_;
}

bool TBCYCVideoFieldRepresentation::set_efm_file(const std::string& efm_path) {
    // EFM support to be implemented (same as TBCVideoFieldRepresentation)
    efm_data_path_ = efm_path;
    has_efm_ = false;  // TODO: Implement EFM loading
    return false;
}

void TBCYCVideoFieldRepresentation::compute_audio_offsets() {
    // Audio support to be implemented (same as TBCVideoFieldRepresentation)
}

void TBCYCVideoFieldRepresentation::compute_efm_offsets() {
    // EFM support to be implemented (same as TBCVideoFieldRepresentation)
}

} // namespace orc
