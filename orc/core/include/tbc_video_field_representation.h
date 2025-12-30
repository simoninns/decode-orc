/*
 * File:        tbc_video_field_representation.h
 * Module:      orc-core
 * Purpose:     TBC video field representation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include "video_field_representation.h"
#include "tbc_reader.h"
#include "tbc_metadata.h"
#include "lru_cache.h"
#include <memory>
#include <fstream>
#include <mutex>

namespace orc {

// Forward declarations for observer friends
class BiphaseObserver;
class VitcObserver;
class ClosedCaptionObserver;
class VideoIdObserver;
class FmCodeObserver;
class WhiteFlagObserver;
class VITSQualityObserver;
class BurstLevelObserver;
class SNRAnalysisObserver;

/**
 * @brief Concrete implementation of VideoFieldRepresentation backed by a TBC file
 * 
 * This class provides the bridge between raw TBC files and the abstract
 * VideoFieldRepresentation interface, implementing Phase 1 requirements.
 */
class TBCVideoFieldRepresentation : public VideoFieldRepresentation {
public:
    /**
     * @brief Create from an open TBC file and metadata
     * 
     * @param tbc_reader Shared reader for TBC file data
     * @param metadata_reader Shared reader for metadata database
     * @param artifact_id Unique identifier for this artifact
     * @param provenance Provenance information
     */
    TBCVideoFieldRepresentation(
        std::shared_ptr<TBCReader> tbc_reader,
        std::shared_ptr<TBCMetadataReader> metadata_reader,
        ArtifactID artifact_id,
        Provenance provenance
    );
    
    ~TBCVideoFieldRepresentation() override = default;
    
    // Prevent copying - represents large video data, share via shared_ptr instead
    TBCVideoFieldRepresentation(const TBCVideoFieldRepresentation&) = delete;
    TBCVideoFieldRepresentation& operator=(const TBCVideoFieldRepresentation&) = delete;
    
    // Prevent moving - instances should be managed via shared_ptr
    TBCVideoFieldRepresentation(TBCVideoFieldRepresentation&&) = delete;
    TBCVideoFieldRepresentation& operator=(TBCVideoFieldRepresentation&&) = delete;
    
    // VideoFieldRepresentation interface
    FieldIDRange field_range() const override;
    size_t field_count() const override;
    bool has_field(FieldID id) const override;
    
    std::optional<FieldDescriptor> get_descriptor(FieldID id) const override;
    
    const sample_type* get_line(FieldID id, size_t line) const override;
    std::vector<sample_type> get_field(FieldID id) const override;
    
    std::vector<DropoutRegion> get_dropout_hints(FieldID id) const override;
    std::optional<FieldParityHint> get_field_parity_hint(FieldID id) const override;
    std::optional<FieldPhaseHint> get_field_phase_hint(FieldID id) const override;
    std::optional<ActiveLineHint> get_active_line_hint() const override;
    
    std::optional<VideoParameters> get_video_parameters() const override {
        return video_params_;
    }
    
    std::vector<std::shared_ptr<Observation>> get_observations(FieldID id) const override;
    
    // Audio interface
    uint32_t get_audio_sample_count(FieldID id) const override;
    std::vector<int16_t> get_audio_samples(FieldID id) const override;
    bool has_audio() const override;
    
    /**
     * @brief Set the PCM audio file path
     * @param pcm_path Path to .pcm audio file
     * @return true if file opened successfully, false otherwise
     */
    bool set_audio_file(const std::string& pcm_path);
    
    std::string type_name() const override { return "TBCVideoFieldRepresentation"; }
    
private:
    // TBC-specific accessors - private to enforce architectural boundaries
    // Only observers and the source stage itself should access TBC internals
    // Other stages must use the standard VideoFieldRepresentation interface
    const VideoParameters& video_parameters() const { return video_params_; }
    std::shared_ptr<TBCMetadataReader> get_metadata_reader() const { return metadata_reader_; }
    std::optional<FieldMetadata> get_field_metadata(FieldID id) const;
    
    // Allow observers to access TBC-specific data
    friend class BiphaseObserver;
    friend class VitcObserver;
    friend class ClosedCaptionObserver;
    friend class VideoIdObserver;
    friend class FmCodeObserver;
    friend class WhiteFlagObserver;
    friend class VITSQualityObserver;
    friend class BurstLevelObserver;
    friend class SNRAnalysisObserver;
    
    std::shared_ptr<TBCReader> tbc_reader_;
    std::shared_ptr<TBCMetadataReader> metadata_reader_;
    
    VideoParameters video_params_;
    std::map<FieldID, FieldMetadata> field_metadata_cache_;
    
    // PCM audio file handle and path
    std::string pcm_audio_path_;
    mutable std::ifstream pcm_audio_file_;
    mutable std::mutex audio_mutex_;  // Protect audio file access
    bool has_audio_;
    
    // Access to metadata reader for internal use only
    const TBCMetadataReader* metadata_reader() const { return metadata_reader_.get(); }
    
    // Line data cache (for get_line calls)
    // Cache size: 500 fields × ~1.4MB/field = ~700MB max for preview navigation
    mutable LRUCache<FieldID, std::vector<sample_type>> field_data_cache_;
    static constexpr size_t MAX_CACHED_TBC_FIELDS = 500;
    
    void ensure_video_parameters();
    void ensure_field_metadata();
};

/**
 * @brief Factory function to create TBCVideoFieldRepresentation from files
 * 
 * @param tbc_filename Path to .tbc file
 * @param metadata_filename Path to .tbc.json.db or .db file
 * @param pcm_filename Optional path to .pcm audio file
 * @return Shared pointer to representation, or nullptr on failure
 */
std::shared_ptr<TBCVideoFieldRepresentation> create_tbc_representation(
    const std::string& tbc_filename,
    const std::string& metadata_filename,
    const std::string& pcm_filename = ""
);

} // namespace orc
