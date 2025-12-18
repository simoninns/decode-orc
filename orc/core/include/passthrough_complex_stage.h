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
class PassthroughComplexStage : public ParameterizedStage {
public:
    PassthroughComplexStage() = default;
    
    /**
     * @brief Process multiple fields (returns each input as separate output)
     * 
     * @param sources Vector of input field representations
     * @return Vector of outputs (same as inputs)
     */
    std::vector<std::shared_ptr<const VideoFieldRepresentation>> process(
        const std::vector<std::shared_ptr<const VideoFieldRepresentation>>& sources) const;
    
    /**
     * @brief Get stage name
     */
    static const char* name() { return "PassthroughComplex"; }
    
    /**
     * @brief Get stage version
     */
    static const char* version() { return "1.0"; }
    
    /**
     * @brief Get minimum number of inputs required
     */
    static size_t min_input_count() { return 2; }
    
    /**
     * @brief Get maximum number of inputs allowed
     */
    static size_t max_input_count() { return 4; }
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
};

} // namespace orc
