/******************************************************************************
 * passthrough_stage.h
 *
 * Passthrough (dummy) stage - passes input to output unchanged
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
class PassthroughStage : public ParameterizedStage {
public:
    PassthroughStage() = default;
    
    /**
     * @brief Process a field (returns input unchanged)
     * 
     * @param source Input field representation
     * @return The same field representation (shared_ptr aliasing)
     */
    std::shared_ptr<const VideoFieldRepresentation> process(
        std::shared_ptr<const VideoFieldRepresentation> source) const;
    
    /**
     * @brief Get stage name
     */
    static const char* name() { return "Passthrough"; }
    
    /**
     * @brief Get stage version
     */
    static const char* version() { return "1.0"; }
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
};

} // namespace orc
