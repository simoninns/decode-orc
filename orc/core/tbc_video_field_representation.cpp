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
#include "observers/biphase_observer.h"
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
    has_audio_(false),
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
    const std::string& metadata_filename,
    const std::string& pcm_filename
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
    
    // Check if active line information is available
    if (video_params_.first_active_frame_line < 0 || 
        video_params_.last_active_frame_line < 0) {
        return std::nullopt;
    }
    
    // Create hint from video parameters
    ActiveLineHint hint;
    hint.first_active_frame_line = video_params_.first_active_frame_line;
    hint.last_active_frame_line = video_params_.last_active_frame_line;
    hint.source = HintSource::METADATA;
    hint.confidence_pct = HintTraits::METADATA_CONFIDENCE;
    
    return hint;
}

std::vector<std::shared_ptr<Observation>> TBCVideoFieldRepresentation::get_observations(FieldID id) const {
    std::vector<std::shared_ptr<Observation>> observations;
    
    if (!metadata_reader_ || !metadata_reader_->is_open()) {
        return observations;
    }
    
    // Read VBI data from the database
    auto vbi_data = metadata_reader_->read_vbi(id);
    if (vbi_data && vbi_data->in_use) {
        // Create a BiphaseObservation from the VBI data
        auto biphase_obs = std::make_shared<BiphaseObservation>();
        biphase_obs->vbi_data = vbi_data->vbi_data;
        
        // Decode the VBI data to populate fields like picture_number, chapter_number, etc.
        // This is based on the BiphaseObserver::interpret_vbi_data() logic
        
        int32_t vbi17 = vbi_data->vbi_data[1];
        int32_t vbi18 = vbi_data->vbi_data[2];
        
        // Helper to decode BCD
        auto decode_bcd = [](uint32_t bcd, int32_t& output) -> bool {
            output = 0;
            int32_t multiplier = 1;
            while (bcd > 0) {
                uint32_t digit = bcd & 0x0F;
                if (digit > 9) return false;
                output += digit * multiplier;
                multiplier *= 10;
                bcd >>= 4;
            }
            return true;
        };
        
        // Check for CAV picture number on lines 17 and 18
        if ((vbi17 & 0xF00000) == 0xF00000) {
            int32_t pic_no;
            if (decode_bcd(vbi17 & 0x07FFFF, pic_no)) {
                biphase_obs->picture_number = pic_no;
            }
        }
        if ((vbi18 & 0xF00000) == 0xF00000) {
            int32_t pic_no;
            if (decode_bcd(vbi18 & 0x07FFFF, pic_no)) {
                biphase_obs->picture_number = pic_no;
            }
        }
        
        // Check for chapter number on lines 17 and 18
        if ((vbi17 & 0xF00FFF) == 0x800DDD) {
            int32_t chapter;
            if (decode_bcd((vbi17 & 0x07F000) >> 12, chapter)) {
                biphase_obs->chapter_number = chapter;
            }
        }
        if ((vbi18 & 0xF00FFF) == 0x800DDD) {
            int32_t chapter;
            if (decode_bcd((vbi18 & 0x07F000) >> 12, chapter)) {
                biphase_obs->chapter_number = chapter;
            }
        }
        
        // Check for CLV time code (simplified - not including full CLV decoding)
        // Full CLV decoding would require more complex logic from BiphaseObserver
        
        // Check for special codes
        if (vbi17 == 0x82CFFF || vbi18 == 0x82CFFF) {
            biphase_obs->stop_code_present = true;
        }
        if (vbi17 == 0x88FFFF || vbi18 == 0x88FFFF) {
            biphase_obs->lead_in = true;
        }
        if (vbi17 == 0x80EEEE || vbi18 == 0x80EEEE) {
            biphase_obs->lead_out = true;
        }
        
        observations.push_back(biphase_obs);
    }
    
    // Future: Add other observation types
    // - Read field_record for is_first_field → FieldParityObservation
    // - Read vitc table → VitcObservation
    // - Read closed_caption → ClosedCaptionObservation
    // - Read vits_metrics → VITSQualityObservation
    
    return observations;
}

// ============================================================================
// Audio interface implementation
// ============================================================================

uint32_t TBCVideoFieldRepresentation::get_audio_sample_count(FieldID id) const {
    if (!has_audio_ || !has_field(id)) {
        return 0;
    }
    
    // Get audio sample count from field metadata
    auto metadata = get_field_metadata(id);
    if (!metadata || !metadata->audio_samples) {
        return 0;
    }
    
    return static_cast<uint32_t>(metadata->audio_samples.value());
}

std::vector<int16_t> TBCVideoFieldRepresentation::get_audio_samples(FieldID id) const {
    if (!has_audio_ || !has_field(id)) {
        return {};
    }
    
    uint32_t sample_count = get_audio_sample_count(id);
    if (sample_count == 0) {
        return {};
    }
    
    // Calculate file offset for this field's audio data
    // Audio is stored sequentially, so we need to sum up all previous fields
    uint64_t byte_offset = 0;
    auto field_range = this->field_range();
    
    for (FieldID fid = field_range.start; fid < id; ++fid) {
        auto metadata = get_field_metadata(fid);
        if (metadata && metadata->audio_samples) {
            // Each sample is 2 channels * 2 bytes = 4 bytes
            byte_offset += metadata->audio_samples.value() * 4;
        }
    }
    
    // Read audio data from file
    std::vector<int16_t> samples(sample_count * 2);  // Stereo
    size_t bytes_to_read = sample_count * 2 * sizeof(int16_t);
    
    {
        std::lock_guard<std::mutex> lock(audio_mutex_);
        
        if (!pcm_audio_file_.is_open()) {
            ORC_LOG_WARN("TBCVideoFieldRepresentation: PCM audio file not open");
            return {};
        }
        
        pcm_audio_file_.seekg(byte_offset, std::ios::beg);
        pcm_audio_file_.read(reinterpret_cast<char*>(samples.data()), bytes_to_read);
        
        if (pcm_audio_file_.gcount() != static_cast<std::streamsize>(bytes_to_read)) {
            ORC_LOG_WARN("TBCVideoFieldRepresentation: Failed to read complete audio for field {}", id.value());
            return {};
        }
    }
    
    return samples;
}

bool TBCVideoFieldRepresentation::has_audio() const {
    return has_audio_;
}

bool TBCVideoFieldRepresentation::set_audio_file(const std::string& pcm_path) {
    if (pcm_path.empty()) {
        has_audio_ = false;
        return true;
    }
    
    std::lock_guard<std::mutex> lock(audio_mutex_);
    
    // Close any previously opened file
    if (pcm_audio_file_.is_open()) {
        pcm_audio_file_.close();
    }
    
    // Open PCM audio file
    pcm_audio_file_.open(pcm_path, std::ios::binary);
    if (!pcm_audio_file_.is_open()) {
        ORC_LOG_ERROR("TBCVideoFieldRepresentation: Failed to open PCM audio file: {}", pcm_path);
        has_audio_ = false;
        return false;
    }
    
    // Validate PCM file size matches metadata expectations
    // Get actual file size
    pcm_audio_file_.seekg(0, std::ios::end);
    uint64_t actual_file_size = pcm_audio_file_.tellg();
    pcm_audio_file_.seekg(0, std::ios::beg);
    
    // Calculate expected file size from metadata
    uint64_t expected_samples = 0;
    auto field_range = this->field_range();
    
    for (FieldID fid = field_range.start; fid < field_range.end; ++fid) {
        auto metadata = get_field_metadata(fid);
        if (metadata && metadata->audio_samples) {
            expected_samples += metadata->audio_samples.value();
        }
    }
    
    // Each sample is 2 channels * 2 bytes (16-bit signed stereo)
    uint64_t expected_file_size = expected_samples * 4;
    
    // Always log the comparison for debugging
    uint64_t actual_samples = actual_file_size / 4;
    ORC_LOG_DEBUG("  PCM file size: {} bytes ({} samples)", actual_file_size, actual_samples);
    ORC_LOG_DEBUG("  Expected from metadata: {} samples ({} bytes)", expected_samples, expected_file_size);
    
    if (actual_file_size != expected_file_size) {
        ORC_LOG_ERROR("PCM audio file size mismatch!");
        ORC_LOG_ERROR("  PCM file: {}", pcm_path);
        ORC_LOG_ERROR("  File contains {} bytes ({} samples)", actual_file_size, actual_samples);
        ORC_LOG_ERROR("  Metadata specifies {} samples ({} bytes expected)", 
                     expected_samples, expected_file_size);
        ORC_LOG_ERROR("  The PCM file and metadata are inconsistent.");
        ORC_LOG_ERROR("  This file may be corrupted, truncated, or not match the TBC metadata.");
        pcm_audio_file_.close();
        has_audio_ = false;
        return false;
    }
    
    ORC_LOG_INFO("TBCVideoFieldRepresentation: Opened PCM audio file: {}", pcm_path);
    ORC_LOG_INFO("  PCM validation passed: {} samples match metadata", expected_samples);
    
    pcm_audio_path_ = pcm_path;
    has_audio_ = true;
    
    return true;
}

} // namespace orc
