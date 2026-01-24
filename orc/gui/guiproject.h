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
#include "presenters/include/project_presenter.h"

namespace orc {
    class Project;  // Forward declaration for legacy methods
    class DAG;      // Forward declaration for legacy methods
}

/**
 * @brief GUI wrapper around ProjectPresenter
 * 
 * Provides Qt-friendly interface for managing ORC projects in the GUI.
 * Handles project file I/O, DAG construction, and project state management.
 * All business logic is delegated to ProjectPresenter (MVP architecture).
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
    bool isModified() const;  ///< Check if project has unsaved changes
    void setModified(bool modified);  ///< Set modified flag (for compatibility)
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
    bool newEmptyProject(
        const QString& project_name, 
        orc::presenters::VideoFormat video_format = orc::presenters::VideoFormat::Unknown, 
        orc::presenters::SourceType source_format = orc::presenters::SourceType::Unknown, 
        QString* error = nullptr
    );
    
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
    
    /// @name Presenter Access
    /// @{
    orc::presenters::ProjectPresenter* presenter() { return presenter_.get(); }  ///< Get mutable presenter
    const orc::presenters::ProjectPresenter* presenter() const { return presenter_.get(); }  ///< Get const presenter
    
    /**
     * @brief Get the current DAG
     * @deprecated Use presenter()->buildDAG() instead
     */
    std::shared_ptr<orc::DAG> getDAG() const;
    
    /**
     * @brief Rebuild DAG from current project structure
     * @deprecated Use presenter()->buildDAG() instead
     * 
     * Call this whenever the DAG structure changes (nodes/edges added/removed)
     * to regenerate the executable DAG from the project.
     */
    void rebuildDAG();
    /// @}
    
private:
    QString project_path_;                                           // Path to .orcprj file
    std::unique_ptr<orc::presenters::ProjectPresenter> presenter_;   // Presenter managing core project
    mutable std::shared_ptr<orc::DAG> dag_cache_;                    // Cached DAG for getDAG()
};

#endif // ORC_GUI_PROJECT_H
