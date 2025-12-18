// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#ifndef ORC_GUI_PROJECT_H
#define ORC_GUI_PROJECT_H

#include <QString>
#include <memory>
#include "project.h"
#include "video_field_representation.h"

/**
 * GUI wrapper around orc::Project
 * 
 * Provides Qt-friendly interface and manages VideoFieldRepresentation loading
 */
class GUIProject {
public:
    GUIProject();
    ~GUIProject();
    
    // Project metadata
    void setProjectPath(const QString& path) { project_path_ = path; }
    QString projectPath() const { return project_path_; }
    QString projectName() const;
    bool isModified() const { return modified_; }
    void setModified(bool modified) { modified_ = modified; }
    
    // Project operations
    bool newEmptyProject(const QString& project_name, QString* error = nullptr);
    bool addSource(const QString& tbc_path, QString* error = nullptr);
    bool removeSource(int source_id, QString* error = nullptr);
    bool saveToFile(const QString& path, QString* error = nullptr);
    bool loadFromFile(const QString& path, QString* error = nullptr);
    void clear();
    
    // Deprecated: use newEmptyProject + addSource
    bool newProject(const QString& tbc_path, QString* error = nullptr);
    
    // Source access (single source for now)
    bool hasSource() const;
    int getSourceId() const;  // Returns 0 for single source, -1 if none
    QString getSourcePath() const;
    QString getSourceName() const;
    std::shared_ptr<const orc::VideoFieldRepresentation> getSourceRepresentation() const;
    
    // Core project access
    orc::Project& coreProject() { return core_project_; }
    const orc::Project& coreProject() const { return core_project_; }
    
private:
    void loadSourceRepresentations();
    
    QString project_path_;                                      // Path to .orc-project file
    orc::Project core_project_;                                 // Core project structure
    std::shared_ptr<const orc::VideoFieldRepresentation> source_representation_;  // Loaded TBC
    bool modified_;                                             // Has project been modified since last save?
};

#endif // ORC_GUI_PROJECT_H
