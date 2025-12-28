/*
 * File:        guiproject.cpp
 * Module:      orc-gui
 * Purpose:     GUI project management
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "guiproject.h"
#include "tbc_video_field_representation.h"
#include "logging.h"
#include "../core/include/project_to_dag.h"
#include <QFileInfo>
#include <algorithm>

GUIProject::GUIProject()
{
}

GUIProject::~GUIProject() = default;

QString GUIProject::projectName() const
{
    if (project_path_.isEmpty()) {
        return QString::fromStdString(core_project_.get_name());
    }
    return QFileInfo(project_path_).completeBaseName();
}

bool GUIProject::newEmptyProject(const QString& project_name, orc::VideoSystem video_format, QString* error)
{
    try {
        // Use core function to create empty project with video format
        core_project_ = orc::project_io::create_empty_project(project_name.toStdString(), video_format);
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to create project: %1").arg(e.what());
        }
        return false;
    }
}

bool GUIProject::saveToFile(const QString& path, QString* error)
{
    try {
        orc::project_io::save_project(core_project_, path.toStdString());
        project_path_ = path;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to save project: %1").arg(e.what());
        }
        return false;
    }
}

bool GUIProject::loadFromFile(const QString& path, QString* error)
{
    try {
        ORC_LOG_DEBUG("Loading project from: {}", path.toStdString());
        core_project_ = orc::project_io::load_project(path.toStdString());
        project_path_ = path;
        ORC_LOG_DEBUG("Building DAG from project");
        rebuildDAG();  // Build DAG after loading project
        
        // Validate DAG was built successfully
        if (hasSource() && !dag_) {
            // Project has source nodes but DAG build failed - this is an error
            throw std::runtime_error("Failed to build DAG from project - check that all source files are valid");
        }
        
        // Attempt to validate source nodes by trying to access them
        if (dag_ && hasSource()) {
            ORC_LOG_DEBUG("Validating source nodes in DAG");
            validateDAGSources();
        }
        
        return true;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Failed to load project: {}", e.what());
        if (error) {
            *error = QString("Failed to load project: %1").arg(e.what());
        }
        return false;
    }
}

void GUIProject::clear()
{
    orc::project_io::clear_project(core_project_);
    dag_.reset();
    project_path_.clear();
}

bool GUIProject::hasSource() const
{
    return core_project_.has_source();
}



QString GUIProject::getSourceName() const
{
    // Find first SOURCE node and get display name
    for (const auto& node : core_project_.get_nodes()) {
        if (node.node_type == orc::NodeType::SOURCE) {
            return QString::fromStdString(node.display_name);
        }
    }
    return QString();
}

void GUIProject::rebuildDAG()
{
    dag_.reset();
    
    if (!hasSource()) {
        ORC_LOG_DEBUG("No source in project, skipping DAG build");
        return;
    }
    
    // Project-to-DAG conversion
    // SOURCE nodes use TBCSourceStage which loads TBC files directly
    try {
        ORC_LOG_DEBUG("Converting project to executable DAG");
        dag_ = orc::project_to_dag(core_project_);
        ORC_LOG_INFO("DAG built successfully from project");
    } catch (const std::exception& e) {
        // Conversion failed - leave null
        // GUI will handle the error
        ORC_LOG_ERROR("Failed to build DAG from project: {}", e.what());
        dag_.reset();
    }
}

void GUIProject::validateDAGSources()
{
    if (!dag_) {
        return;
    }
    
    // Try to execute each source node to validate they can be accessed
    // Source nodes should produce output when executed with empty inputs
    ORC_LOG_DEBUG("Validating {} DAG nodes", dag_->nodes().size());
    
    for (const auto& node : dag_->nodes()) {
        // Check if this is a source node by checking if it has no inputs
        if (node.input_node_ids.empty()) {
            ORC_LOG_DEBUG("Validating source node: {}", node.node_id);
            try {
                // Execute the stage with empty inputs to validate
                // This will trigger TBC loading and validation
                auto outputs = node.stage->execute({}, node.parameters);
                if (outputs.empty()) {
                    throw std::runtime_error("Source node '" + node.node_id.to_string() + "' produced no output");
                }
                ORC_LOG_DEBUG("Source node validation passed: {}", node.node_id);
            } catch (const std::exception& e) {
                // Source validation failed - re-throw with more context
                std::string error_msg = "Source validation failed for node '" + node.node_id.to_string() + "': " + e.what();
                throw std::runtime_error(error_msg);
            }
        }
    }
}

