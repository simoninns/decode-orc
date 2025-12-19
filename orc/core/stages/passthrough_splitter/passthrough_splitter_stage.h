/*
 * File:        passthrough_splitter_stage.h
 * Module:      orc-core
 * Purpose:     Passthrough splitter stage
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
 * @brief Passthrough splitter stage - duplicates input to multiple outputs
 * 
 * This is a test stage that demonstrates SPLITTER node type (1 input, N outputs).
 * It returns the same input field representation as multiple outputs.
 * 
 * Use cases:
 * - GUI testing of splitter node rendering
 * - Testing parallel processing paths in DAG
 * - Demonstrating fanout patterns
 */
class PassthroughSplitterStage : public DAGStage, public ParameterizedStage {
public:
    PassthroughSplitterStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::SPLITTER,
            "passthrough_splitter",
            "Pass-through Splitter",
            "Duplicate input to multiple outputs (test stage for fanout patterns)",
            1, 1,  // Exactly one input
            3, 3,  // Exactly three outputs
            true   // User can add
        };
    }    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, std::string>& parameters) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 3; }  // Fixed at 3 outputs
    
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
};

} // namespace orc
