/*
 * File:        project.h
 * Module:      orc-core
 * Purpose:     Project
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include "stage_parameter.h"
#include "node_type.h"

namespace orc {

// Forward declaration
class VideoFieldRepresentation;

/**
 * Node in a project DAG
 * All nodes are uniform - SOURCE nodes just use TBCSourceStage with tbc_path parameter
 */
struct ProjectDAGNode {
    std::string node_id;
    std::string stage_name;     // e.g., "TBCSource", "DropoutCorrect", etc.
    NodeType node_type;         // Node type (SOURCE, SINK, TRANSFORM, etc.)
    std::string display_name;   // Display name for GUI (e.g., "Source: video.tbc", "Noise Filter")
    std::string user_label;     // User-editable label (initially same as display_name)
    double x_position;          // Position for GUI layout
    double y_position;
    std::map<std::string, ParameterValue> parameters;  // Stage parameters (e.g., tbc_path/db_path for sources)
};

/**
 * Edge in a project DAG
 */
struct ProjectDAGEdge {
    std::string source_node_id;
    std::string target_node_id;
};

/**
 * Project - encapsulates processing DAG
 * 
 * A project file (.orc-project) is a YAML file containing:
 * - Project metadata (name, description, version)
 * - DAG structure (nodes, edges, parameters)
 * - SOURCE nodes use TBCSourceStage with tbc_path in parameters
 * 
 * The project file format is shared between orc-gui and orc-process.
 * Both tools can load and save projects in the same format.
 * 
 * The Project class owns and caches the source TBC representation,
 * ensuring a single source of truth for all consumers.
 */
class Project {
public:
    std::string name;                       // Project name
    std::string description;                // Project description (optional)
    std::string version;                    // Project format version (e.g., "1.0")
    std::vector<ProjectDAGNode> nodes;      // DAG nodes (including SOURCE nodes)
    std::vector<ProjectDAGEdge> edges;      // DAG edges
    
    // Modification tracking (not persisted to file)
    mutable bool is_modified = false;       // True if project has been modified since load/save
    
    // Helper to clear modification flag (called after successful load/save)
    void clear_modified_flag() const { is_modified = false; }
    
    // Helper to check if modified
    bool has_unsaved_changes() const { return is_modified; }
    
    /**
     * Check if project has a source node
     */
    bool has_source() const;
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
     * Create a new empty project with no sources
     * @param project_name Name for the project
     * @return Empty project structure
     */
    Project create_empty_project(const std::string& project_name);
    
    /**
     * Update project DAG nodes and edges
     * Replaces all nodes and edges with new ones
     * @param project Project to modify
     * @param nodes New DAG nodes
     * @param edges New DAG edges
     */
    void update_project_dag(
        Project& project,
        const std::vector<ProjectDAGNode>& nodes,
        const std::vector<ProjectDAGEdge>& edges
    );
    
    /**
     * Generate a unique node ID for a project
     * Finds the next available ID by examining existing nodes
     * @param project Project to check for existing IDs
     * @return Unique node ID (e.g., "node_1", "node_2", etc.)
     */
    std::string generate_unique_node_id(const Project& project);
    
    /**
     * Add a new node to the project DAG
     * @param project Project to modify
     * @param stage_name Stage type name (e.g., "Passthrough", "DropoutCorrect")
     * @param x_position X position for GUI layout
     * @param y_position Y position for GUI layout
     * @return ID of the newly created node
     * @throws std::runtime_error if stage_name is invalid
     */
    std::string add_node(Project& project, const std::string& stage_name, double x_position, double y_position);
    
    /**
     * Remove a node from the project DAG
     * Also removes all edges connected to this node
     * @param project Project to modify
     * @param node_id ID of node to remove
     * @throws std::runtime_error if node_id not found
     */
    void remove_node(Project& project, const std::string& node_id);
    
    /**
     * Change a node's stage type
     * @param project Project to modify
     * @param node_id ID of node to modify
     * @param new_stage_name New stage type name
     * @throws std::runtime_error if node_id not found or new_stage_name invalid
     */
    void change_node_type(Project& project, const std::string& node_id, const std::string& new_stage_name);
    
    /**
     * Check if a node's type can be changed
     * @param project Project to check
     * @param node_id ID of node to check
     * @param reason Optional output parameter for why node type cannot be changed
     * @return true if node type can be changed, false otherwise
     */
    bool can_change_node_type(const Project& project, const std::string& node_id, std::string* reason = nullptr);
    
    /**
     * Update a node's parameters
     * @param project Project to modify
     * @param node_id ID of node to modify
     * @param parameters New parameter map
     * @throws std::runtime_error if node_id not found
     */
    void set_node_parameters(Project& project, const std::string& node_id, 
                            const std::map<std::string, ParameterValue>& parameters);
    
    /**
     * Update a node's position
     * @param project Project to modify
     * @param node_id ID of node to modify
     * @param x_position New X position
     * @param y_position New Y position
     * @throws std::runtime_error if node_id not found
     */
    void set_node_position(Project& project, const std::string& node_id, double x_position, double y_position);
    
    /**
     * Update a node's user-defined label
     * @param project Project to modify
     * @param node_id ID of node to modify
     * @param label New user-defined label
     * @throws std::runtime_error if node_id not found
     */
    void set_node_label(Project& project, const std::string& node_id, const std::string& label);
    
    /**
     * Add an edge to the project DAG
     * @param project Project to modify
     * @param source_node_id Source node ID
     * @param target_node_id Target node ID
     * @throws std::runtime_error if nodes not found or connection invalid
     */
    void add_edge(Project& project, const std::string& source_node_id, const std::string& target_node_id);
    
    /**
     * Remove an edge from the project DAG
     * @param project Project to modify
     * @param source_node_id Source node ID
     * @param target_node_id Target node ID
     * @throws std::runtime_error if edge not found
     */
    void remove_edge(Project& project, const std::string& source_node_id, const std::string& target_node_id);
    
    /**
     * Clear all project data, resetting to empty state
     * Clears name, sources, nodes, edges, and resets modification flag
     * @param project Project to clear
     */
    void clear_project(Project& project);
}

} // namespace orc
