// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025

#pragma once

#include <string>
#include <vector>
#include <map>
#include "stage_parameter.h"

namespace orc {

/**
 * Represents a source (TBC file) within a project
 */
struct ProjectSource {
    int source_id;              // Unique ID within project (0, 1, 2, ...)
    std::string tbc_path;       // Absolute or relative path to TBC file
    std::string display_name;   // Display name (extracted from filename or user-provided)
};

/**
 * Node in a project DAG
 * Similar to GUIDAGNode but includes source_id for START nodes
 */
struct ProjectDAGNode {
    std::string node_id;
    std::string stage_name;
    std::string display_name;  // Display name for GUI (e.g., "Source: video.tbc", "Noise Filter")
    double x_position;  // Position for GUI layout
    double y_position;
    std::map<std::string, ParameterValue> parameters;
    int source_id;      // For START nodes: which source this represents (-1 for non-START nodes)
};

/**
 * Edge in a project DAG
 */
struct ProjectDAGEdge {
    std::string source_node_id;
    std::string target_node_id;
};

/**
 * Project - encapsulates sources and their processing DAG
 * 
 * A project file (.orc-project) is a YAML file containing:
 * - Project metadata (name, version)
 * - List of source TBC files with unique source IDs
 * - DAG structure (nodes, edges, parameters)
 * - Each START node references a source by source_id
 * 
 * The project file format is shared between orc-gui and orc-process.
 * Both tools can load and save projects in the same format.
 */
struct Project {
    std::string name;                       // Project name
    std::string version;                    // Project format version (e.g., "1.0")
    std::vector<ProjectSource> sources;     // All sources in the project
    std::vector<ProjectDAGNode> nodes;      // DAG nodes (including START nodes)
    std::vector<ProjectDAGEdge> edges;      // DAG edges
};

/**
 * Project file I/O
 */
namespace project_io {
    /**
     * Load a project from YAML file
     * @param filename Path to .orc-project file
     * @return Project structure
     * @throws std::runtime_error on parse or I/O errors
     */
    Project load_project(const std::string& filename);
    
    /**
     * Save a project to YAML file
     * @param project Project structure to save
     * @param filename Path to .orc-project file
     * @throws std::runtime_error on I/O errors
     */
    void save_project(const Project& project, const std::string& filename);
    
    /**
     * Create a new empty project with a single source
     * @param tbc_path Path to TBC file
     * @param project_name Name for the project (optional, derived from TBC if empty)
     * @return Project with one source and a START node
     */
    Project create_single_source_project(const std::string& tbc_path, const std::string& project_name = "");
    
    /**
     * Create a new empty project with no sources
     * @param project_name Name for the project
     * @return Empty project structure
     */
    Project create_empty_project(const std::string& project_name);
    
    /**
     * Extract display name from TBC file path
     * @param tbc_path Path to TBC file
     * @return Display name (filename without extension)
     */
    std::string extract_display_name(const std::string& tbc_path);
    
    /**
     * Add a source to an existing project
     * Automatically assigns source ID, creates START node, and positions it
     * @param project Project to modify
     * @param tbc_path Path to TBC file to add
     * @throws std::runtime_error if source path already exists in project
     */
    void add_source_to_project(Project& project, const std::string& tbc_path);
    
    /**
     * Remove a source from a project
     * Removes source, START node, and connected edges
     * @param project Project to modify
     * @param source_id ID of source to remove
     * @throws std::runtime_error if source ID not found
     */
    void remove_source_from_project(Project& project, int source_id);
    
    /**
     * Update project DAG nodes and edges
     * Replaces all non-START nodes and edges with new ones
     * Preserves START nodes from sources
     * @param project Project to modify
     * @param nodes New DAG nodes (non-START)
     * @param edges New DAG edges
     */
    void update_project_dag(
        Project& project,
        const std::vector<ProjectDAGNode>& nodes,
        const std::vector<ProjectDAGEdge>& edges
    );
}

} // namespace orc
