/*
 * File:        dropout_map_stage.h
 * Module:      orc-core
 * Purpose:     Dropout map stage - override dropout hints on per-field basis
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "dropout_decision.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include "previewable_stage.h"
#include <memory>
#include <vector>
#include <map>
#include <string>

namespace orc {

// Forward declaration
class DropoutMapStage;

/**
 * @brief Per-field dropout override specification
 * 
 * Each entry specifies dropouts to add or remove for a specific field.
 */
struct FieldDropoutMap {
    FieldID field_id;
    std::vector<DropoutRegion> additions;    ///< Dropouts to add
    std::vector<DropoutRegion> removals;     ///< Dropouts to remove
    
    FieldDropoutMap() : field_id(0) {}
    FieldDropoutMap(FieldID id) : field_id(id) {}
};

/**
 * @brief Video field representation with overridden dropout hints
 * 
 * This wrapper modifies dropout hints based on per-field specifications,
 * allowing users to add, remove, or modify dropout regions.
 */
class DropoutMappedRepresentation : public VideoFieldRepresentationWrapper {
public:
    DropoutMappedRepresentation(
        std::shared_ptr<const VideoFieldRepresentation> source,
        const std::map<uint64_t, FieldDropoutMap>& dropout_map);
    
    ~DropoutMappedRepresentation() = default;
    
    /// Override dropout hints to apply the field-specific modifications
    std::vector<DropoutRegion> get_dropout_hints(FieldID id) const override;
    
    // Required virtual methods (forward to source)
    const sample_type* get_line(FieldID id, size_t line) const override {
        return source_ ? source_->get_line(id, line) : nullptr;
    }
    
    std::vector<sample_type> get_field(FieldID id) const override {
        return source_ ? source_->get_field(id) : std::vector<sample_type>{};
    }
    
private:
    std::map<uint64_t, FieldDropoutMap> dropout_map_;
};

/**
 * @brief Dropout map stage - override dropout hints on per-field basis
 * 
 * This stage allows manual override of dropout hints from the source(s).
 * Users can add new dropouts, remove false positives, or modify existing
 * dropout boundaries on a per-field basis.
 * 
 * The stage does NOT modify the actual video data - it only modifies the
 * dropout hints that downstream stages (like dropout_correct) will see.
 * 
 * Multiple inputs: When multiple inputs are provided, the same dropout map
 * is applied to all inputs, producing one output per input.
 * 
 * Parameters:
 * - dropout_map: String encoding of per-field dropout modifications
 *   Format: JSON-like structure with field-specific dropout lists
 *   Example: "[{field:0,add:[{line:10,start:100,end:200}],remove:[{line:15,start:50,end:75}]}]"
 * 
 * Use cases:
 * - Manually marking dropouts that were not detected
 * - Removing false positive dropout detections
 * - Adjusting boundaries of detected dropouts
 * - Creating custom dropout patterns for testing
 * - Applying same manual corrections to multiple sources (e.g., stacked inputs)
 */
class DropoutMapStage : public DAGStage, public ParameterizedStage, public PreviewableStage {
public:
    DropoutMapStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::TRANSFORM,
            "dropout_map",
            "Dropout Map",
            "Override dropout hints on per-field basis - add, remove, or modify dropout regions",
            1, UINT32_MAX,  // One to many inputs
            1, UINT32_MAX,  // One output per input
            VideoFormatCompatibility::ALL
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 0; }  // Variable: one per input
    
    // PreviewableStage interface
    bool supports_preview() const override { return true; }
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(const std::string& option_id, uint64_t index,
                               PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
    /// Apply additions and removals to a list of dropout regions (public for DropoutMappedRepresentation)
    static std::vector<DropoutRegion> apply_modifications(
        const std::vector<DropoutRegion>& source_dropouts,
        const FieldDropoutMap& modifications);

    /// Parse dropout map string into structured data (public for GUI editor)
    /// Format: "[{field:0,add:[{line:10,start:100,end:200}],remove:[...]},{field:1,...}]"
    static std::map<uint64_t, FieldDropoutMap> parse_dropout_map(const std::string& map_str);
    
    /// Encode dropout map to string format (public for GUI editor)
    static std::string encode_dropout_map(const std::map<uint64_t, FieldDropoutMap>& map);

private:
    // Current parameters
    std::string dropout_map_str_;
    
    // Cached parsed dropout map (updated when dropout_map_str_ changes)
    std::map<uint64_t, FieldDropoutMap> cached_dropout_map_;
    
    // Cached output for preview rendering
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_output_;
};

} // namespace orc
