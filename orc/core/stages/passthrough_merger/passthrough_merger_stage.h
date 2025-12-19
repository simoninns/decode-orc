/*
 * File:        passthrough_merger_stage.h
 * Module:      orc-core
 * Purpose:     Passthrough merger stage
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

namespace orc {

/**
 * @brief Passthrough merger stage - selects first input from multiple inputs
 * 
 * This is a test stage that demonstrates MERGER node type (N inputs, 1 output).
 * It simply returns the first input unchanged (ignores other inputs).
 * 
 * Use cases:
 * - GUI testing of merger node rendering
 * - Testing multi-source DAG patterns
 * - Demonstrating stacking/blending node structure
 */
class PassthroughMergerStage : public DAGStage, public ParameterizedStage {
public:
    PassthroughMergerStage() = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::MERGER,
            "passthrough_merger",
            "Pass-through Merger",
            "Select first input from multiple inputs (test stage for merge patterns)",
            2, 8,  // 2 to 8 inputs
            1, 1   // Exactly one output
        };
    }    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;
    
    size_t required_input_count() const override { return 2; }  // At least 2 inputs
    size_t output_count() const override { return 1; }
    
    /**
     * @brief Process multiple fields (returns first input unchanged)
     * 
     * @param sources Vector of input field representations
     * @return First input unchanged (or nullptr if inputs empty)
     */
    std::shared_ptr<const VideoFieldRepresentation> process(
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const;
    

    /**
     * @brief Get minimum number of inputs required
     */
    static size_t min_input_count() { return 2; }
    
    /**
     * @brief Get maximum number of inputs allowed
     */
    static size_t max_input_count() { return 8; }  // Reasonable limit for testing
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
};

} // namespace orc
