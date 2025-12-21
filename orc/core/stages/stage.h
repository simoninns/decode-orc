/*
 * File:        stage.h
 * Module:      orc-core/stages
 * Purpose:     Base interface for all stage types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include "../include/artifact.h"
#include "../include/node_type.h"
#include "../include/stage_parameter.h"
#include <memory>
#include <vector>
#include <map>
#include <string>

namespace orc {

/**
 * @brief Base interface for all processing stages
 * 
 * Stages transform input artifacts into output artifacts.
 * They are pure functions of their inputs and parameters.
 * 
 * All stage implementations should inherit from this interface
 * and implement the required methods.
 * 
 * Design Philosophy:
 * - Stages are stateless transformations
 * - All state is in artifacts (inputs/outputs)
 * - Parameters are declarative configuration
 * - Execution is deterministic and repeatable
 */
class DAGStage {
public:
    virtual ~DAGStage() = default;
    
    /**
     * @brief Get stage version string
     * 
     * Used for provenance tracking and compatibility checking.
     * Should follow semantic versioning (e.g., "1.2.3").
     */
    virtual std::string version() const = 0;
    
    /**
     * @brief Get node type information for GUI and validation
     * 
     * Describes the stage's capabilities, inputs, outputs, and parameters
     * for use in the visual DAG editor and runtime validation.
     */
    virtual NodeTypeInfo get_node_type_info() const = 0;
    
    /**
     * @brief Execute the stage transformation
     * 
     * @param inputs Input artifacts (must match required_input_count)
     * @param parameters Configuration parameters (validated against NodeTypeInfo)
     * @return Output artifacts (count matches output_count)
     * 
     * This method should be pure - same inputs and parameters always
     * produce the same outputs. No side effects except through returned artifacts.
     */
    virtual std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters
    ) = 0;
    
    /**
     * @brief Number of required input artifacts
     * 
     * The DAG executor validates that this many inputs are provided.
     * Return 0 for source stages (no inputs required).
     */
    virtual size_t required_input_count() const = 0;
    
    /**
     * @brief Number of output artifacts produced
     * 
     * The DAG executor validates that execute() returns this many outputs.
     * Most stages return 1, but splitters may return multiple outputs.
     */
    virtual size_t output_count() const = 0;
};

/**
 * @brief Shared pointer to a stage
 * 
 * Stages are shared across the DAG and should be managed via shared_ptr.
 */
using DAGStagePtr = std::shared_ptr<DAGStage>;

} // namespace orc
