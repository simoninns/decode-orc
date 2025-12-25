/*
 * File:        splitter_stage.h
 * Module:      orc-core
 * Purpose:     Splitter stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include <memory>

namespace orc {

/**
 * @brief Splitter stage - duplicates input to multiple outputs
 * 
 * This stage demonstrates SPLITTER node type (1 input, N outputs).
 * It returns the same input field representation as multiple outputs.
 * 
 * Use cases:
 * - Connecting input to multiple sink types in a DAG chain
 * - GUI testing of splitter node rendering
 * - Testing parallel processing paths in DAG
 * - Demonstrating fanout patterns
 */
class SplitterStage : public DAGStage, public ParameterizedStage {
public:
    SplitterStage();
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::SPLITTER,
            "splitter",
            "Splitter",
            "Duplicate input to multiple outputs for parallel processing",
            1, 1,  // Exactly one input
            2, 8   // 2 to 8 outputs
        };
    }    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return num_outputs_; }
    
    /**
     * @brief Process a field (returns input duplicated to multiple outputs)
     * 
     * @param source Input field representation
     * @return Vector of N copies of the input (shared_ptr aliasing)
     */
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> process(
        std::shared_ptr<const VideoFieldRepresentation> source) const;
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

private:
    size_t num_outputs_ = 2;  // Default to 2 outputs
};

} // namespace orc
