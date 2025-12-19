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
        return QString::fromStdString(core_project_.name);
    }
    return QFileInfo(project_path_).completeBaseName();
}

bool GUIProject::newEmptyProject(const QString& project_name, QString* error)
{
    try {
        // Use core function to create empty project
        core_project_ = orc::project_io::create_empty_project(project_name.toStdString());
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

QString GUIProject::getSourceNodeId() const
{
    // Return the first SOURCE node ID, empty if none
    for (const auto& node : core_project_.nodes) {
        if (node.node_type == orc::NodeType::SOURCE) {
            return QString::fromStdString(node.node_id);
        }
    }
    return QString();
}

QString GUIProject::getSourceType() const
{
    // Return the first SOURCE node's stage name, empty if none
    for (const auto& node : core_project_.nodes) {
        if (node.node_type == orc::NodeType::SOURCE) {
            return QString::fromStdString(node.stage_name);
        }
    }
    return QString();
}

int GUIProject::getSourceId() const
{
    // Legacy compatibility - return 0 if we have a source, -1 if not
    return hasSource() ? 0 : -1;
}

QString GUIProject::getSourcePath() const
{
    // Find first SOURCE node and get tbc_path parameter
    for (const auto& node : core_project_.nodes) {
        if (node.node_type == orc::NodeType::SOURCE) {
            auto it = node.parameters.find("tbc_path");
            if (it != node.parameters.end() && std::holds_alternative<std::string>(it->second)) {
                return QString::fromStdString(std::get<std::string>(it->second));
            }
        }
    }
    return QString();
}

QString GUIProject::getSourceName() const
{
    // Find first SOURCE node and get display name
    for (const auto& node : core_project_.nodes) {
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

