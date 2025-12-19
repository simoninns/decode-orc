/*
 * File:        field_map_stage.h
 * Module:      orc-core
 * Purpose:     Field mapping/reordering stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include <memory>
#include <vector>
#include <string>

namespace orc {

/**
 * @brief Field mapping stage that reorders fields based on range specifications
 * 
 * This stage allows reordering of input fields by specifying ranges.
 * For example, given input fields 0-30, the parameter "0-10,20-30,11-19"
 * would output fields in that reordered sequence:
 * - Fields 0-10 (first 11 fields)
 * - Fields 20-30 (next 11 fields)  
 * - Fields 11-19 (final 9 fields)
 * 
 * The output is a virtual representation that remaps field IDs according
 * to the specified ranges, without copying the actual field data.
 * 
 * Use cases:
 * - Reordering fields from misaligned captures
 * - Skipping bad field ranges
 * - Rearranging field sequences for processing
 */
class FieldMapStage : public DAGStage, public ParameterizedStage {
public:
    FieldMapStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::TRANSFORM,
            "field_map",
            "Field Map",
            "Reorder fields based on range specifications (e.g., 0-10,20-30,11-19)",
            1, 1,  // Exactly one input
            1, 1   // Exactly one output
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 1; }
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

private:
    /**
     * @brief Parse range specification string (e.g., "0-10,20-30,11-19")
     * @return Vector of (start, end) pairs, or empty on parse error
     */
    static std::vector<std::pair<uint64_t, uint64_t>> parse_ranges(const std::string& range_spec);
    
    /**
     * @brief Build mapping from output field index to input FieldID
     */
    static std::vector<FieldID> build_field_mapping(
        const std::vector<std::pair<uint64_t, uint64_t>>& ranges,
        const VideoFieldRepresentation& source);
    
    // Current parameters
    std::string range_spec_;
    
    // Cached parsed ranges (updated when range_spec_ changes)
    std::vector<std::pair<uint64_t, uint64_t>> cached_ranges_;
};

} // namespace orc
