/******************************************************************************
 * tbc_video_field_representation.h
 *
 * Concrete implementation of VideoFieldRepresentation backed by TBC files
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#pragma once

#include "video_field_representation.h"
#include "tbc_reader.h"
#include "tbc_metadata.h"
#include <memory>

namespace orc {

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
    
    // VideoFieldRepresentation interface
    FieldIDRange field_range() const override;
    size_t field_count() const override;
    bool has_field(FieldID id) const override;
    
    std::optional<FieldDescriptor> get_descriptor(FieldID id) const override;
    
    const sample_type* get_line(FieldID id, size_t line) const override;
    std::vector<sample_type> get_field(FieldID id) const override;
    
    std::string type_name() const override { return "TBCVideoFieldRepresentation"; }
    
    // Additional accessors specific to TBC
    const VideoParameters& video_parameters() const { return video_params_; }
    std::optional<FieldMetadata> get_field_metadata(FieldID id) const;
    
    // Access to metadata reader for additional metadata queries
    const TBCMetadataReader* metadata_reader() const { return metadata_reader_.get(); }
    
private:
    std::shared_ptr<TBCReader> tbc_reader_;
    std::shared_ptr<TBCMetadataReader> metadata_reader_;
    
    VideoParameters video_params_;
    std::map<FieldID, FieldMetadata> field_metadata_cache_;
    
    // Line data cache (for get_line calls)
    mutable std::map<FieldID, std::vector<sample_type>> field_data_cache_;
    mutable FieldID last_cached_field_;
    
    void ensure_video_parameters();
    void ensure_field_metadata();
};

/**
 * @brief Factory function to create TBCVideoFieldRepresentation from files
 * 
 * @param tbc_filename Path to .tbc file
 * @param metadata_filename Path to .tbc.json.db or .db file
 * @return Shared pointer to representation, or nullptr on failure
 */
std::shared_ptr<TBCVideoFieldRepresentation> create_tbc_representation(
    const std::string& tbc_filename,
    const std::string& metadata_filename
);

} // namespace orc
