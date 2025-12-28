/*
 * File:        guiproject.h
 * Module:      orc-gui
 * Purpose:     GUI project management
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_GUI_PROJECT_H
#define ORC_GUI_PROJECT_H

#include <QString>
#include <memory>
#include "project.h"
#include "video_field_representation.h"

namespace orc {
    class DAG;
}

/**
 * GUI wrapper around orc::Project
 * 
 * Provides Qt-friendly interface - all data and caching handled by core
 */
class GUIProject {
public:
    GUIProject();
    ~GUIProject();
    
    // Project metadata
    void setProjectPath(const QString& path) { project_path_ = path; }
    QString projectPath() const { return project_path_; }
    QString projectName() const;
    bool isModified() const { return core_project_.has_unsaved_changes(); }
    void setModified(bool modified) { 
        if (modified) {
            // We can't directly set is_modified - this is handled by project_io functions
            // Just rely on the fact that modification operations will set it
        } else {
            core_project_.clear_modified_flag();
        }
    }
    
    // Project operations
    /**
     * Create a new empty project
     * @param project_name Name for the project
     * @param video_format Video format (NTSC or PAL)
     * @param error Optional error message output
     * @return true if successful, false otherwise
     */
    bool newEmptyProject(const QString& project_name, orc::VideoSystem video_format = orc::VideoSystem::Unknown, QString* error = nullptr);
    bool saveToFile(const QString& path, QString* error = nullptr);
    bool loadFromFile(const QString& path, QString* error = nullptr);
    void clear();
    
    // Source access (single source for now)
    bool hasSource() const;
    QString getSourceName() const;
    
    // Core project access
    orc::Project& coreProject() { return core_project_; }
    const orc::Project& coreProject() const { return core_project_; }
    
    // DAG access - single owned instance
    std::shared_ptr<orc::DAG> getDAG() const { return dag_; }
    
    // Rebuild DAG from current project structure
    // Call this whenever the DAG structure changes (nodes/edges added/removed)
    void rebuildDAG();
    
private:
    
    QString project_path_;                                      // Path to .orcprj file
    orc::Project core_project_;                                 // Core project structure
    mutable std::shared_ptr<orc::DAG> dag_;                     // Built DAG (single instance)
};

#endif // ORC_GUI_PROJECT_H
