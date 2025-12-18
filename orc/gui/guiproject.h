// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

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
    bool isModified() const { return core_project_.has_unsaved_changes(); }
    void setModified(bool modified) { 
        if (modified) {
            core_project_.is_modified = true;
        } else {
            core_project_.clear_modified_flag();
        }
    }
    
    // Project operations
    bool newEmptyProject(const QString& project_name, QString* error = nullptr);
    bool addSource(const QString& tbc_path, QString* error = nullptr);
    bool removeSource(const QString& node_id, QString* error = nullptr);
    bool saveToFile(const QString& path, QString* error = nullptr);
    bool loadFromFile(const QString& path, QString* error = nullptr);
    void clear();
    
    // Deprecated: use newEmptyProject + addSource
    bool newProject(const QString& tbc_path, QString* error = nullptr);
    
    // Source access (single source for now)
    bool hasSource() const;
    QString getSourceNodeId() const;  // Returns first SOURCE node ID, empty if none
    int getSourceId() const;  // Legacy compatibility - returns 0 or -1
    QString getSourcePath() const;
    QString getSourceName() const;
    std::shared_ptr<const orc::VideoFieldRepresentation> getSourceRepresentation() const;
    
    // Core project access
    orc::Project& coreProject() { return core_project_; }
    const orc::Project& coreProject() const { return core_project_; }
    
    // DAG access - single owned instance
    std::shared_ptr<orc::DAG> getDAG() const { return dag_; }
    
    // Rebuild DAG from current project structure
    // Call this whenever the DAG structure changes (nodes/edges added/removed)
    void rebuildDAG();
    
private:
    void loadSourceRepresentations();
    
    QString project_path_;                                      // Path to .orc-project file
    orc::Project core_project_;                                 // Core project structure
    std::shared_ptr<const orc::VideoFieldRepresentation> source_representation_;  // Loaded TBC
    mutable std::shared_ptr<orc::DAG> dag_;                     // Built DAG (single instance)
};

#endif // ORC_GUI_PROJECT_H
