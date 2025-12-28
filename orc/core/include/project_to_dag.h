/*
 * File:        project_to_dag.h
 * Module:      orc-core
 * Purpose:     Project to DAG conversion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#pragma once

#include "project.h"
#include "dag_executor.h"
#include "video_field_representation.h"
#include <memory>
#include <map>
#include <stdexcept>

namespace orc {

/**
 * @brief Exception thrown during Project-to-DAG conversion
 */
class ProjectConversionError : public std::runtime_error {
public:
    explicit ProjectConversionError(const std::string& msg) : std::runtime_error(msg) {}
};

/**
 * @brief Convert a Project to an executable DAG
 * 
 * This function bridges the gap between serializable Projects (strings, data)
 * and executable DAGs (C++ objects, stages).
 * 
 * Conversion process:
 * 1. Create DAG nodes by instantiating stages from the stage registry
 * 2. SOURCE nodes use TBCSourceStage which loads TBC files from parameters
 * 3. Set up edges and dependencies
 * 4. Validate the resulting DAG
 * 
 * @param project The project to convert
 * @return Executable DAG ready for rendering or execution
 * @throws ProjectConversionError if conversion fails
 * 
 * Example:
 * ```cpp
 * Project project = load_project("example.orc-project");
 * 
 * // Convert to executable DAG (SOURCE nodes load TBC files automatically)
 * auto dag = project_to_dag(project);
 * 
 * // Now can render fields
 * DAGFieldRenderer renderer(dag);
 * auto result = renderer.render_field_at_node("transform_1", FieldID(42));
 * ```
 */
std::shared_ptr<DAG> project_to_dag(const Project& project);

/**
 * @brief Validate that all source nodes in a DAG can be accessed
 * 
 * This function attempts to execute each source node in the DAG to verify
 * that they can produce output. This is useful for validating that source
 * files (e.g., TBC files) exist and can be loaded.
 * 
 * @param dag The DAG to validate
 * @throws ProjectConversionError if any source node fails validation
 */
void validate_source_nodes(const std::shared_ptr<DAG>& dag);

} // namespace orc
