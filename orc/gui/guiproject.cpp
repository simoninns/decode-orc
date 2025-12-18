// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#include "guiproject.h"
#include "tbc_video_field_representation.h"
#include <QFileInfo>
#include <algorithm>

GUIProject::GUIProject()
    : modified_(false)
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
        modified_ = true;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to create project: %1").arg(e.what());
        }
        return false;
    }
}

bool GUIProject::addSource(const QString& tbc_path, QString* error)
{
    try {
        // Use core function to add source (creates START node automatically)
        orc::project_io::add_source_to_project(core_project_, tbc_path.toStdString());
        
        // Load TBC representation
        loadSourceRepresentations();
        modified_ = true;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to add source: %1").arg(e.what());
        }
        return false;
    }
}

bool GUIProject::removeSource(int source_id, QString* error)
{
    try {
        // Use core function to remove source (removes START node and edges automatically)
        orc::project_io::remove_source_from_project(core_project_, source_id);
        
        // Reload representations
        loadSourceRepresentations();
        modified_ = true;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to remove source: %1").arg(e.what());
        }
        return false;
    }
}

// Deprecated: kept for backward compatibility
bool GUIProject::newProject(const QString& tbc_path, QString* error)
{
    if (!newEmptyProject("Untitled Project", error)) {
        return false;
    }
    return addSource(tbc_path, error);
}

bool GUIProject::saveToFile(const QString& path, QString* error)
{
    try {
        orc::project_io::save_project(core_project_, path.toStdString());
        project_path_ = path;
        modified_ = false;
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
        core_project_ = orc::project_io::load_project(path.toStdString());
        project_path_ = path;
        loadSourceRepresentations();
        modified_ = false;
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = QString("Failed to load project: %1").arg(e.what());
        }
        return false;
    }
}

void GUIProject::clear()
{
    core_project_ = orc::Project();
    source_representation_.reset();
    project_path_.clear();
    modified_ = false;
}

bool GUIProject::hasSource() const
{
    return !core_project_.sources.empty();
}

int GUIProject::getSourceId() const
{
    if (core_project_.sources.empty()) {
        return -1;
    }
    return core_project_.sources[0].source_id;
}

QString GUIProject::getSourcePath() const
{
    if (core_project_.sources.empty()) {
        return QString();
    }
    return QString::fromStdString(core_project_.sources[0].tbc_path);
}

QString GUIProject::getSourceName() const
{
    if (core_project_.sources.empty()) {
        return QString();
    }
    return QString::fromStdString(core_project_.sources[0].display_name);
}

std::shared_ptr<const orc::VideoFieldRepresentation> GUIProject::getSourceRepresentation() const
{
    return source_representation_;
}

void GUIProject::loadSourceRepresentations()
{
    source_representation_.reset();
    
    if (core_project_.sources.empty()) {
        return;
    }
    
    // Load the single source (for now)
    const auto& source = core_project_.sources[0];
    source_representation_ = orc::create_tbc_representation(
        source.tbc_path,
        source.tbc_path + ".db"
    );
}
