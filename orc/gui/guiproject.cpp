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
        source_representation_.reset();
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to create project: %1").arg(e.what());
        }
        return false;
    }
}

bool GUIProject::addSource(const QString& stage_name, const QString& tbc_path, QString* error)
{
    try {
        // Use core function to add source with specified stage type
        orc::project_io::add_source_to_project(core_project_, stage_name.toStdString(), tbc_path.toStdString());
        
        // Load TBC representation
        loadSourceRepresentations();
        rebuildDAG();  // Rebuild DAG after adding source
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to add source: %1").arg(e.what());
        }
        return false;
    }
}

bool GUIProject::removeSource(const QString& node_id, QString* error)
{
    try {
        // Use core function to remove source node (removes node and edges automatically)
        orc::project_io::remove_source_node(core_project_, node_id.toStdString());
        
        // Reload representations
        loadSourceRepresentations();
        rebuildDAG();  // Rebuild DAG after removing source
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to remove source: %1").arg(e.what());
        }
        return false;
    }
}

// Deprecated: kept for backward compatibility
// Attempts to auto-detect format from TBC file
bool GUIProject::newProject(const QString& tbc_path, QString* error)
{
    if (!newEmptyProject("Untitled Project", error)) {
        return false;
    }
    // Try PAL first, then NTSC - let the stages validate
    if (addSource("LDPALSource", tbc_path, error)) {
        return true;
    }
    // If PAL failed, try NTSC
    return addSource("LDNTSCSource", tbc_path, error);
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
        ORC_LOG_DEBUG("Project loaded, loading source representations");
        loadSourceRepresentations();
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
    source_representation_.reset();
    dag_.reset();
    project_path_.clear();
}

bool GUIProject::hasSource() const
{
    // Check if project has any SOURCE nodes
    return std::any_of(core_project_.nodes.begin(), core_project_.nodes.end(),
        [](const orc::ProjectDAGNode& node) {
            return node.node_type == orc::NodeType::SOURCE;
        });
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

std::shared_ptr<const orc::VideoFieldRepresentation> GUIProject::getSourceRepresentation() const
{
    return source_representation_;
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

void GUIProject::loadSourceRepresentations()
{
    source_representation_.reset();
    
    // Find first SOURCE node
    for (const auto& node : core_project_.nodes) {
        if (node.node_type == orc::NodeType::SOURCE) {
            // Get tbc_path from parameters
            auto it = node.parameters.find("tbc_path");
            if (it != node.parameters.end() && std::holds_alternative<std::string>(it->second)) {
                std::string tbc_path = std::get<std::string>(it->second);
                std::string db_path = tbc_path + ".db";
                
                // Check for db_path parameter
                auto db_it = node.parameters.find("db_path");
                if (db_it != node.parameters.end() && std::holds_alternative<std::string>(db_it->second)) {
                    db_path = std::get<std::string>(db_it->second);
                }
                
                ORC_LOG_DEBUG("Loading TBC representation: {}", tbc_path);
                // Load representation
                source_representation_ = orc::create_tbc_representation(tbc_path, db_path);
                if (source_representation_) {
                    auto range = source_representation_->field_range();
                    ORC_LOG_INFO("TBC representation loaded: {} fields", range.size());
                } else {
                    ORC_LOG_ERROR("Failed to create TBC representation");
                }
                break;  // Only load first source for now
            }
        }
    }
}
