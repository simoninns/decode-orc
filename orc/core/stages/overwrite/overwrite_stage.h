/******************************************************************************
 * overwrite_stage.h
 *
 * Overwrite stage - replaces all field data with a constant IRE value
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
#include "dag_executor.h"
#include <memory>

namespace orc {

/**
 * @brief Overwrite stage that replaces all field data with a constant IRE value
 * 
 * This stage is useful for testing visualization and parameter systems.
 * It takes an input VideoFieldRepresentation and produces a new one where
 * all sample values are set to the specified IRE level (converted to 16-bit).
 * 
 * Use cases:
 * - Testing parameter editing in GUI
 * - Verifying visualization pipeline
 * - Creating test patterns with known values
 */
class OverwriteStage : public DAGStage, public ParameterizedStage {
public:
    OverwriteStage();
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::TRANSFORM,
            "overwrite",
            "Overwrite",
            "Replace all field data with constant IRE value (for testing)",
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
     * @brief Process a field by overwriting all samples with constant value
     * 
     * @param source Input field representation
     * @return New field representation with all samples set to ire_value_
     */
    std::shared_ptr<const VideoFieldRepresentation> process(
        std::shared_ptr<const VideoFieldRepresentation> source) const;
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

private:
    double ire_value_ = 50.0;  // Default to mid-gray (50 IRE)
};

} // namespace orc
