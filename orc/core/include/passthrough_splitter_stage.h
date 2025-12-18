/******************************************************************************
 * passthrough_splitter_stage.h
 *
 * Passthrough splitter stage - one input, multiple outputs (for testing)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#pragma once

#include "video_field_representation.h"
#include "stage_parameter.h"
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
class PassthroughSplitterStage : public ParameterizedStage {
public:
    PassthroughSplitterStage() = default;
    
    /**
     * @brief Process a field (returns input duplicated to multiple outputs)
     * 
     * @param source Input field representation
     * @return Vector of N copies of the input (shared_ptr aliasing)
     */
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> process(
        std::shared_ptr<const VideoFieldRepresentation> source) const;
    
    /**
     * @brief Get stage name
     */
    static const char* name() { return "PassthroughSplitter"; }
    
    /**
     * @brief Get stage version
     */
    static const char* version() { return "1.0"; }
    
    /**
     * @brief Get number of outputs this stage produces
     */
    static size_t output_count() { return 3; }  // Fixed at 3 outputs for testing
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
};

} // namespace orc
