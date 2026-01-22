/*
 * File:        project_presenter.h
 * Module:      orc-presenters
 * Purpose:     Project management presenter - MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <node_id.h>
#include <node_type.h>
#include <field_id.h>

// Forward declare core Project type
namespace orc {
    class Project;
}

namespace orc::presenters {

/**
 * @brief Video format enumeration for GUI use
 */
enum class VideoFormat {
    NTSC,
    PAL,
    Unknown
};

/**
 * @brief Source type enumeration for GUI use
 */
enum class SourceType {
    Composite,
    YC,
    Unknown
};

/**
 * @brief Information about a stage available in the registry
 */
struct StageInfo {
    std::string name;           ///< Internal stage name
    std::string display_name;   ///< User-friendly display name
    std::string description;    ///< Stage description
    NodeType node_type;         ///< Type of node
    bool is_source;             ///< True if this is a source stage
    bool is_sink;               ///< True if this is a sink stage
};

/**
 * @brief Information about a node in the project
 */
struct NodeInfo {
    NodeID node_id;             ///< Node identifier
    std::string stage_name;     ///< Stage type name
    std::string label;          ///< User-assigned label
    double x_position;          ///< X position in graph
    double y_position;          ///< Y position in graph
    bool can_remove;            ///< Whether node can be removed
    bool can_trigger;           ///< Whether node can be triggered
    bool can_inspect;           ///< Whether node can be inspected
    std::string remove_reason;  ///< Reason if cannot remove
    std::string trigger_reason; ///< Reason if cannot trigger
    std::string inspect_reason; ///< Reason if cannot inspect
};

/**
 * @brief Edge between two nodes
 */
struct EdgeInfo {
    NodeID source_node;         ///< Source node ID
    NodeID target_node;         ///< Target node ID
};

/**
 * @brief Progress callback for batch operations
 */
using ProgressCallback = std::function<void(size_t current, size_t total, const std::string& message)>;

/**
 * @brief ProjectPresenter - Manages project creation, loading, and modification
 * 
 * This presenter extracts all project-related business logic from the GUI layer.
 * It provides a clean interface for:
 * - Creating quick/template projects
 * - Loading and saving projects
 * - Managing nodes and edges in the project DAG
 * - Querying project metadata
 * - Triggering batch operations
 * 
 * The presenter owns the core Project object and coordinates all operations.
 */
class ProjectPresenter {
public:
    /**
     * @brief Construct presenter with new empty project
     */
    ProjectPresenter();
    
    /**
     * @brief Construct presenter by loading existing project
     * @param project_path Path to .orcprj file
     */
    explicit ProjectPresenter(const std::string& project_path);
    
    /**
     * @brief Destructor
     */
    ~ProjectPresenter();
    
    // Disable copy, enable move
    ProjectPresenter(const ProjectPresenter&) = delete;
    ProjectPresenter& operator=(const ProjectPresenter&) = delete;
    ProjectPresenter(ProjectPresenter&&) noexcept;
    ProjectPresenter& operator=(ProjectPresenter&&) noexcept;
    
    // === Project Lifecycle ===
    
    /**
     * @brief Create a quick project from template
     * @param format Video format (NTSC/PAL)
     * @param source Source type (Composite/SVideo)
     * @param input_files List of input TBC files
     * @return true on success
     */
    bool createQuickProject(VideoFormat format, SourceType source, const std::vector<std::string>& input_files);
    
    /**
     * @brief Load project from file
     * @param project_path Path to .orcprj file
     * @return true on success
     */
    bool loadProject(const std::string& project_path);
    
    /**
     * @brief Save project to file
     * @param project_path Path to save to
     * @return true on success
     */
    bool saveProject(const std::string& project_path);
    
    /**
     * @brief Clear the current project
     */
    void clearProject();
    
    /**
     * @brief Check if project has been modified since last save
     */
    bool isModified() const;
    
    /**
     * @brief Get project file path
     */
    std::string getProjectPath() const;
    
    // === Project Metadata ===
    
    /**
     * @brief Get project name
     */
    std::string getProjectName() const;
    
    /**
     * @brief Set project name
     */
    void setProjectName(const std::string& name);
    
    /**
     * @brief Get project description
     */
    std::string getProjectDescription() const;
    
    /**
     * @brief Set project description
     */
    void setProjectDescription(const std::string& description);
    
    /**
     * @brief Get video format
     */
    VideoFormat getVideoFormat() const;
    
    /**
     * @brief Set video format
     */
    void setVideoFormat(VideoFormat format);
    
    /**
     * @brief Get source type
     */
    SourceType getSourceType() const;
    
    /**
     * @brief Set source type
     */
    void setSourceType(SourceType source);
    
    // === DAG Management ===
    
    /**
     * @brief Add a node to the project
     * @param stage_name Internal stage name
     * @param x_position X coordinate
     * @param y_position Y coordinate
     * @return NodeID of created node
     */
    NodeID addNode(const std::string& stage_name, double x_position, double y_position);
    
    /**
     * @brief Remove a node from the project
     * @param node_id Node to remove
     * @return true on success
     */
    bool removeNode(NodeID node_id);
    
    /**
     * @brief Check if a node can be removed
     * @param node_id Node to check
     * @param reason Output parameter for reason if cannot remove
     * @return true if can remove
     */
    bool canRemoveNode(NodeID node_id, std::string* reason = nullptr) const;
    
    /**
     * @brief Set node position
     */
    void setNodePosition(NodeID node_id, double x, double y);
    
    /**
     * @brief Set node label
     */
    void setNodeLabel(NodeID node_id, const std::string& label);
    
    /**
     * @brief Set node parameters
     * @param node_id Node to configure
     * @param parameters Map of parameter name -> value
     */
    void setNodeParameters(NodeID node_id, const std::map<std::string, std::string>& parameters);
    
    /**
     * @brief Add an edge between two nodes
     * @param source_node Source node
     * @param target_node Target node
     */
    void addEdge(NodeID source_node, NodeID target_node);
    
    /**
     * @brief Remove an edge
     */
    void removeEdge(NodeID source_node, NodeID target_node);
    
    /**
     * @brief Get all nodes in the project
     */
    std::vector<NodeInfo> getNodes() const;
    
    /**
     * @brief Get all edges in the project
     */
    std::vector<EdgeInfo> getEdges() const;
    
    /**
     * @brief Get information about a specific node
     */
    NodeInfo getNodeInfo(NodeID node_id) const;
    
    // === Stage Registry ===
    
    /**
     * @brief Get all available stages for a video format
     * @param format Video format to filter by
     * @return List of available stages
     */
    static std::vector<StageInfo> getAvailableStages(VideoFormat format);
    
    /**
     * @brief Get all available stages (no filtering)
     */
    static std::vector<StageInfo> getAllStages();
    
    // === Batch Operations ===
    
    /**
     * @brief Check if a node can be triggered
     * @param node_id Node to check
     * @param reason Output parameter for reason if cannot trigger
     * @return true if can trigger
     */
    bool canTriggerNode(NodeID node_id, std::string* reason = nullptr) const;
    
    /**
     * @brief Trigger batch processing for a node
     * @param node_id Node to trigger
     * @param progress_callback Optional progress callback
     * @return true on success
     */
    bool triggerNode(NodeID node_id, ProgressCallback progress_callback = nullptr);
    
    // === Validation ===
    
    /**
     * @brief Validate the project for errors
     * @return true if project is valid
     */
    bool validateProject() const;
    
    /**
     * @brief Get validation errors
     */
    std::vector<std::string> getValidationErrors() const;
    
    // === Internal Access (for other presenters) ===
    
    /**
     * @brief Get raw project pointer (for other presenters only)
     * @internal
     */
    orc::Project* getProject() const { return project_.get(); }

private:
    std::unique_ptr<orc::Project> project_;
    std::string project_path_;
    bool is_modified_;
};

} // namespace orc::presenters
