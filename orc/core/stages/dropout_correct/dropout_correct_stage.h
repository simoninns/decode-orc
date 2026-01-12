/*
 * File:        dropout_correct_stage.h
 * Module:      orc-core
 * Purpose:     Dropout correction stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "tbc_metadata.h"
#include "dropout_decision.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include "previewable_stage.h"
#include "lru_cache.h"
#include <memory>
#include <vector>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <set>

namespace orc {

// Hash function for std::pair<FieldID, uint32_t> to use in unordered_map
struct FieldLineHash {
    std::size_t operator()(const std::pair<FieldID, uint32_t>& key) const {
        // Combine field ID and line number into a single hash
        return std::hash<uint64_t>{}(key.first.value()) ^ (std::hash<uint32_t>{}(key.second) << 1);
    }
};

// Forward declarations
class DropoutCorrectStage;

/// Configuration for dropout correction stage
struct DropoutCorrectionConfig {
    /// Overcorrect mode: extend dropout regions by this many samples
    /// Useful for heavily damaged sources (default: 0, overcorrect: 24)
    uint32_t overcorrect_extension = 0;
    
    /// Force intrafield correction only (default: use interfield when possible)
    bool intrafield_only = false;
    
    /// Maximum distance to search for replacement lines (in lines)
    uint32_t max_replacement_distance = 10;
    
    /// Whether to match chroma phase when selecting replacement lines
    bool match_chroma_phase = true;
    
    /// Highlight corrections by filling with white IRE level instead of replacement data
    bool highlight_corrections = false;
};

/// Corrected video field representation
/// 
/// This wraps the original field data with corrections applied on-demand
class CorrectedVideoFieldRepresentation : public VideoFieldRepresentationWrapper {
public:
    CorrectedVideoFieldRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source,
        DropoutCorrectStage* stage,
        bool highlight_corrections);
    
    ~CorrectedVideoFieldRepresentation() = default;
    
    // Only override methods that are actually modified by this stage
    const uint16_t* get_line(FieldID id, size_t line) const override;
    std::vector<uint16_t> get_field(FieldID id) const override;
    
    // Dual-channel access methods for YC sources
    const uint16_t* get_line_luma(FieldID id, size_t line) const override;
    const uint16_t* get_line_chroma(FieldID id, size_t line) const override;
    std::vector<uint16_t> get_field_luma(FieldID id) const override;
    std::vector<uint16_t> get_field_chroma(FieldID id) const override;
    
    // Override dropout hints - after correction, there are no dropouts
    // (the output of this stage has corrected data, so hints describe the output)
    std::vector<DropoutRegion> get_dropout_hints(FieldID /*id*/) const override {
        // All dropouts have been corrected, so return empty
        // Future: could return uncorrectable dropouts if correction failed
        return {};
    }
    
    // Get the original dropout regions that were corrected (for visualization/debugging)
    std::vector<DropoutRegion> get_corrected_regions(FieldID id) const {
        return source_ ? source_->get_dropout_hints(id) : std::vector<DropoutRegion>{};
    }
    
    // Allow stage to access private members
    friend class DropoutCorrectStage;
    
private:
    DropoutCorrectStage* stage_;  // Non-owning pointer to stage for lazy correction
    bool highlight_corrections_;
    
    // Corrected field data - LRU cache of whole fields for fast access
    // For composite sources: single cache
    // For YC sources: dual caches (luma and chroma)
    // Cache size: 300 fields × ~1.4MB/field = ~420MB max (composite)
    //           or 300 fields × 2 × ~1.4MB/field = ~840MB max (YC sources)
    // For 200,000 field videos, this caches ~0.15% of total, focused on preview navigation area
    // Cache also serves as the "processed" indicator - if field is in cache, it's been processed
    mutable LRUCache<FieldID, std::vector<uint16_t>> corrected_fields_;
    
    // Dual caches for YC sources (separate Y and C corrections)
    mutable LRUCache<FieldID, std::vector<uint16_t>> corrected_luma_fields_;
    mutable LRUCache<FieldID, std::vector<uint16_t>> corrected_chroma_fields_;
    
    static constexpr size_t MAX_CACHED_FIELDS = 300;
    
    // Ensure field is corrected (lazy)
    void ensure_field_corrected(FieldID field_id) const;
};

/// Dropout correction stage
/// 
/// Signal-transforming stage that corrects dropouts by replacing
/// corrupted samples with data from other lines/fields.
class DropoutCorrectStage : public DAGStage, public ParameterizedStage, public PreviewableStage {
public:
    explicit DropoutCorrectStage(const DropoutCorrectionConfig& config = DropoutCorrectionConfig())
        : config_(config) {}
    
    // DAGStage interface

    std::string version() const override { return "1.0"; }    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::TRANSFORM,
            "dropout_correct",
            "Dropout Correction",
            "Correct dropouts by replacing corrupted samples with data from other lines/fields",
            1, 1,  // Exactly one input
            1, UINT32_MAX,  // Many outputs
            VideoFormatCompatibility::ALL
        };
    }    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 1; }
    
    // PreviewableStage interface
    bool supports_preview() const override { return true; }
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(const std::string& option_id, uint64_t index,
                               PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;
    
    /// Process a single field and apply dropout corrections
    /// 
    /// @param source Source field representation
    /// @param field_id Field to process
    /// @param dropouts Detected dropout regions (from observer)
    /// @param decisions User decisions to apply
    /// @return Corrected field representation
    std::shared_ptr<CorrectedVideoFieldRepresentation> correct_field(
        std::shared_ptr<const VideoFieldRepresentation> source,
        FieldID field_id,
        const std::vector<DropoutRegion>& dropouts,
        const DropoutDecisions& decisions = DropoutDecisions());
    
    /// Process multiple fields (for multi-source correction)
    /// 
    /// @param sources Multiple source representations
    /// @param field_id Field to process
    /// @param all_dropouts Dropout regions for each source
    /// @param decisions User decisions
    /// @return Corrected field representation using best available sources
    std::shared_ptr<CorrectedVideoFieldRepresentation> correct_field_multisource(
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources,
        FieldID field_id,
        const std::vector<std::vector<DropoutRegion>>& all_dropouts,
        const DropoutDecisions& decisions = DropoutDecisions());
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
    // Public for lazy correction from CorrectedVideoFieldRepresentation
    void correct_single_field(
        CorrectedVideoFieldRepresentation* corrected,
        std::shared_ptr<const VideoFieldRepresentation> source,
        FieldID field_id) const;
    
private:
    DropoutCorrectionConfig config_;
    
    /// Location type for dropout classification
    enum class DropoutLocation {
        COLOUR_BURST,
        VISIBLE_LINE,
        UNKNOWN
    };
    
    /// Channel type for YC source replacement line search
    enum class Channel {
        COMPOSITE,  // For composite sources (use get_line)
        LUMA,       // For YC sources, luma channel (use get_line_luma)
        CHROMA      // For YC sources, chroma channel (use get_line_chroma)
    };
    
    /// Classify a dropout region by location
    DropoutLocation classify_dropout(const DropoutRegion& dropout, const FieldDescriptor& descriptor) const;
    
    /// Split dropout regions that span multiple areas
    std::vector<DropoutRegion> split_dropout_regions(
        const std::vector<DropoutRegion>& dropouts,
        const FieldDescriptor& descriptor) const;
    
    /// Find the best replacement line for a dropout
    /// 
    /// Searches nearby lines in the same field (intrafield) or
    /// the opposite field (interfield) for the best replacement.
    struct ReplacementLine {
        bool found = false;
        FieldID source_field;
        uint32_t source_line;
        double quality = 0.0;  // Quality metric (higher is better)
        uint32_t distance = 0;  // Distance in lines from original
        const uint16_t* cached_data = nullptr;  // Cached pointer to replacement line data
    };
    
    ReplacementLine find_replacement_line(
        const VideoFieldRepresentation& source,
        FieldID field_id,
        uint32_t line,
        const DropoutRegion& dropout,
        bool intrafield,
        Channel channel = Channel::COMPOSITE) const;
    
    /// Apply a single dropout correction
    void apply_correction(
        std::vector<uint16_t>& line_data,
        const DropoutRegion& dropout,
        const uint16_t* replacement_data,
        bool highlight = false) const;
    
    /// Calculate quality metric for a potential replacement line
    double calculate_line_quality(
        const uint16_t* line_data,
        size_t width,
        const DropoutRegion& dropout) const;
    
    // Cached output for preview rendering
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_output_;
};

} // namespace orc
