/******************************************************************************
 * passthrough_complex_stage.h
 *
 * Passthrough complex stage - multiple inputs, multiple outputs (for testing)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include <memory>
#include <vector>

namespace orc {

/**
 * @brief Passthrough complex stage - multiple inputs to multiple outputs
 * 
 * This is a test stage that demonstrates COMPLEX node type (N inputs, M outputs).
 * It returns each input as a separate output (identity mapping).
 * 
 * Use cases:
 * - GUI testing of complex node rendering
 * - Testing advanced DAG patterns
 * - Demonstrating multi-input/multi-output processing
 */
class PassthroughComplexStage : public DAGStage, public ParameterizedStage {
public:
    PassthroughComplexStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::COMPLEX,
            "passthrough_complex",
            "Pass-through Complex",
            "Multiple inputs to multiple outputs (test stage for complex patterns)",
            2, 4,  // 2 to 4 inputs
            2, 4,  // 2 to 4 outputs
            true   // User can add
        };
    }    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, std::string>& parameters) override;
    
    size_t required_input_count() const override { return 3; }  // 3 inputs
    size_t output_count() const override { return 2; }  // 2 outputs
    
    /**
     * @brief Process multiple fields (returns each input as separate output)
     * 
     * @param sources Vector of input field representations
     * @return Vector of outputs (same as inputs)
     */
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> process(
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const;
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
};

} // namespace orc
