// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#pragma once

#include "video_field_representation.h"
#include "dropout_decision.h"
#include "stage_parameter.h"
#include <memory>
#include <vector>
#include <cstdint>

namespace orc {

/// Configuration for dropout correction stage
struct DropoutCorrectionConfig {
    /// Overcorrect mode: extend dropout regions by this many samples
    /// Useful for heavily damaged sources (default: 0, overcorrect: 24)
    uint32_t overcorrect_extension = 0;
    
    /// Force intrafield correction only (default: use interfield when possible)
    bool intrafield_only = false;
    
    /// Reverse field order (use second/first instead of first/second)
    bool reverse_field_order = false;
    
    /// Maximum distance to search for replacement lines (in lines)
    uint32_t max_replacement_distance = 10;
    
    /// Whether to match chroma phase when selecting replacement lines
    bool match_chroma_phase = true;
};

/// Corrected video field representation
/// 
/// This wraps the original field data with corrections applied
class CorrectedVideoFieldRepresentation : public VideoFieldRepresentation {
public:
    CorrectedVideoFieldRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source,
        const std::vector<DropoutRegion>& corrections_applied);
    
    ~CorrectedVideoFieldRepresentation() override = default;
    
    // VideoFieldRepresentation interface
    FieldIDRange field_range() const override;
    size_t field_count() const override;
    bool has_field(FieldID id) const override;
    std::optional<FieldDescriptor> get_descriptor(FieldID id) const override;
    const uint16_t* get_line(FieldID id, size_t line) const override;
    std::vector<uint16_t> get_field(FieldID id) const override;
    
    // Access to corrected data
    const std::vector<uint16_t>& get_corrected_line(FieldID id, uint32_t line) const;
    
    // Allow stage to access private members
    friend class DropoutCorrectStage;
    
private:
    std::shared_ptr<const VideoFieldRepresentation> source_;
    
    // Corrected line data (sparse - only lines with corrections)
    mutable std::map<std::pair<FieldID, uint32_t>, std::vector<uint16_t>> corrected_lines_;
    
    // List of corrections applied
    std::vector<DropoutRegion> corrections_;
};

/// Dropout correction stage
/// 
/// Signal-transforming stage that corrects dropouts by replacing
/// corrupted samples with data from other lines/fields.
class DropoutCorrectStage : public ParameterizedStage {
public:
    explicit DropoutCorrectStage(const DropoutCorrectionConfig& config = DropoutCorrectionConfig())
        : config_(config) {}
    
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
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
private:
    DropoutCorrectionConfig config_;
    
    /// Location type for dropout classification
    enum class DropoutLocation {
        COLOUR_BURST,
        VISIBLE_LINE,
        UNKNOWN
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
    };
    
    ReplacementLine find_replacement_line(
        const VideoFieldRepresentation& source,
        FieldID field_id,
        uint32_t line,
        const DropoutRegion& dropout,
        bool intrafield) const;
    
    /// Apply a single dropout correction
    void apply_correction(
        std::vector<uint16_t>& line_data,
        const DropoutRegion& dropout,
        const uint16_t* replacement_data) const;
    
    /// Calculate quality metric for a potential replacement line
    double calculate_line_quality(
        const uint16_t* line_data,
        size_t width,
        const DropoutRegion& dropout) const;
};

} // namespace orc
