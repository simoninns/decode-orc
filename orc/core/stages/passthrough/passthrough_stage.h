/*
 * File:        passthrough_stage.h
 * Module:      orc-core
 * Purpose:     Passthrough processing stage
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
 * @brief Passthrough stage that returns input unchanged
 * 
 * This is a dummy/no-op stage useful for GUI prototyping and DAG building.
 * It simply passes the input VideoFieldRepresentation through to the output
 * without any modifications.
 * 
 * Use cases:
 * - GUI placeholder when user adds a node before selecting its type
 * - Testing DAG execution flow
 * - Benchmarking overhead of stage infrastructure
 */
class PassthroughStage : public DAGStage, public ParameterizedStage {
public:
    PassthroughStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::TRANSFORM,
            "passthrough",
            "Pass-through Simple",
            "Pass input to output unchanged (no-op stage for testing)",
            1, 1,  // Exactly one input
            1, 1,  // Exactly one output
            true   // User can add
        };
    }    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, std::string>& parameters) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 1; }
    
    /**
     * @brief Process a field (returns input unchanged)
     * 
     * @param source Input field representation
     * @return The same field representation (shared_ptr aliasing)
     */
    std::shared_ptr<const VideoFieldRepresentation> process(
        std::shared_ptr<const VideoFieldRepresentation> source) const;
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
};

} // namespace orc
