/*
 * File:        guiproject.h
 * Module:      orc-gui
 * Purpose:     GUI project management
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
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
 * @brief GUI wrapper around orc::Project
 * 
 * Provides Qt-friendly interface for managing ORC projects in the GUI.
 * Handles project file I/O, DAG construction, and project state management.
 * All core data and caching is handled by orc::Project; this class just
 * provides Qt integration.
 */
class GUIProject {
public:
    GUIProject();
    ~GUIProject();
    
    /// @name Project Metadata
    /// @{
    void setProjectPath(const QString& path) { project_path_ = path; }  ///< Set project file path
    QString projectPath() const { return project_path_; }  ///< Get project file path
    QString projectName() const;  ///< Get project name
    bool isModified() const { return core_project_.has_unsaved_changes(); }  ///< Check if project has unsaved changes
    void setModified(bool modified) { 
        if (modified) {
            // We can't directly set is_modified - this is handled by project_io functions
            // Just rely on the fact that modification operations will set it
        } else {
            core_project_.clear_modified_flag();
        }
    }
    /// @}
    
    /// @name Project Operations
    /// @{
    
    /**
     * @brief Create a new empty project
     * @param project_name Name for the project
     * @param video_format Video format (NTSC or PAL)
     * @param error Optional error message output
     * @return true if successful, false otherwise
     */
    bool newEmptyProject(const QString& project_name, orc::VideoSystem video_format = orc::VideoSystem::Unknown, QString* error = nullptr);
    
    /**
     * @brief Save project to file
     * @param path Path to save .orcprj file
     * @param error Optional error message output
     * @return true if successful, false otherwise
     */
    bool saveToFile(const QString& path, QString* error = nullptr);
    
    /**
     * @brief Load project from file
     * @param path Path to .orcprj file
     * @param error Optional error message output
     * @return true if successful, false otherwise
     */
    bool loadFromFile(const QString& path, QString* error = nullptr);
    
    void clear();  ///< Clear project data
    /// @}
    
    /// @name Source Access
    /// @{
    bool hasSource() const;  ///< Check if project has a video source
    QString getSourceName() const;  ///< Get name of the first video source
    /// @}
    
    /// @name Core Project Access
    /// @{
    orc::Project& coreProject() { return core_project_; }  ///< Get mutable reference to core project
    const orc::Project& coreProject() const { return core_project_; }  ///< Get const reference to core project
    std::shared_ptr<orc::DAG> getDAG() const { return dag_; }  ///< Get the current DAG
    
    /**
     * @brief Rebuild DAG from current project structure
     * 
     * Call this whenever the DAG structure changes (nodes/edges added/removed)
     * to regenerate the executable DAG from the project.
     */
    void rebuildDAG();
    /// @}
    
private:
    
    QString project_path_;                                      // Path to .orcprj file
    orc::Project core_project_;                                 // Core project structure
    mutable std::shared_ptr<orc::DAG> dag_;                     // Built DAG (single instance)
};

#endif // ORC_GUI_PROJECT_H
