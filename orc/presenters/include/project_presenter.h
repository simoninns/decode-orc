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
#include <optional>
#include <node_id.h>
#include <node_type.h>
#include <field_id.h>
#include <parameter_types.h>
#include <orc_video_metadata.h>  // For public_api::VideoParameters
#include "stage_inspection_view_models.h"

// Forward declare core Project type
namespace orc {
    class Project;
}

namespace orc::presenters {

// === Application Initialization ===

/**
 * @brief Initialize core logging system
 * @param level Log level (trace, debug, info, warn, error, critical, off)
 * @param pattern Optional custom pattern (default: "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v")
 * @param log_file Optional file path to write logs to (in addition to console)
 * 
 * This function provides access to core's logging initialization through the
 * presenters layer, maintaining MVP architecture compliance.
 */
void initCoreLogging(const std::string& level = "info",
                     const std::string& pattern = "[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v",
                     const std::string& log_file = "");

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
     * @brief Construct presenter wrapping an existing project
     * @param project_handle Opaque handle to existing project
     */
    explicit ProjectPresenter(void* project_handle);
    
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
    
    // === Utility Methods (Static) ===
    
    /**
     * @brief Read video parameters from a TBC metadata file
     * @param metadata_path Path to .tbc.db metadata file
     * @return VideoParameters if successful, nullopt if file doesn't exist or can't be read
     * 
     * This is a utility method for reading metadata before creating a project,
     * allowing the GUI to determine video format and other parameters from
     * existing TBC files.
     */
    static std::optional<orc::VideoParameters> readVideoParameters(
        const std::string& metadata_path);
    
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
     * @brief Get source format
     */
    SourceType getSourceFormat() const;
    
    /**
     * @brief Set source format
     */
    void setSourceFormat(SourceType source);
    
    /**
     * @brief Create a snapshot copy of the project
     * @return Shared pointer to immutable project copy (opaque handle)
     */
    std::shared_ptr<const void> createSnapshot() const;
    
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
     * @brief Get the first node in the DAG (if any)
     * @return NodeID of first node, or invalid NodeID if no nodes
     */
    NodeID getFirstNode() const;
    
    /**
     * @brief Check if a node exists in the project
     * @param node_id Node to check
     * @return true if node exists
     */
    bool hasNode(NodeID node_id) const;
    
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
    
    /**
     * @brief Check if a stage type exists in the registry
     * @param stage_name Stage type name
     * @return true if stage exists
     */
    static bool hasStage(const std::string& stage_name);
    
    /**
     * @brief Get stage instance for inspection (from DAG if available, else fresh)
     * @param node_id Node to get stage for
     * @return Stage instance or nullptr if not found
     */
    std::shared_ptr<void> getStageForInspection(NodeID node_id) const;
    
    /**
     * @brief Get stage instance for parameter editing
     * @param stage_name Stage type name
     * @return Fresh stage instance or nullptr if not found
     */
    static std::shared_ptr<void> createStageInstance(const std::string& stage_name);
    
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
    
        /**
         * @brief Trigger all sink nodes in the project
         * @param progress_callback Optional progress callback
         * @return true if all sinks succeeded
         * 
         * Finds all triggerable sink nodes in the project and executes them sequentially.
         * Progress callback is invoked for each sink node being processed.
         */
        bool triggerAllSinks(ProgressCallback progress_callback = nullptr);
    
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
    
    // === Stage Inspection ===
    
    /**
     * @brief Get inspection report for a node
     * @param node_id Node to inspect
     * @return Inspection report, or nullopt if not available
     * 
     * This creates a stage instance (from DAG if available, otherwise fresh),
     * and generates an inspection report. The report contains human-readable
     * information about the stage's current state and configuration.
     */
    std::optional<StageInspectionView> getNodeInspection(NodeID node_id) const;
    
    // === Project Snapshots ===
    
    
    /**
     * @brief Get the current DAG for the project
     * @return Shared pointer to DAG (as void* for encapsulation)
     */
    std::shared_ptr<void> getDAG() const;
    
    // === DAG Operations ===
    
    /**
     * @brief Build DAG from current project structure
     * @return Opaque DAG handle (void*) or nullptr on failure
     * 
     * Rebuilds the executable DAG from the project graph.
     * Call this whenever the DAG structure changes (nodes/edges added/removed).
     */
    std::shared_ptr<void> buildDAG();
    
    /**
     * @brief Validate DAG structure
     * @return true if DAG is valid and can be executed
     * 
     * Checks for cycles, disconnected subgraphs, missing parameters, etc.
     */
    bool validateDAG();
    
    // === Parameter Operations ===
    
    /**
     * @brief Get parameter descriptors for a stage type
     * @param stage_name Internal stage name
     * @return Vector of parameter descriptors
     * 
     * Returns all parameters that can be configured for this stage type.
     */
    std::vector<ParameterDescriptor> getStageParameters(const std::string& stage_name);
    
    /**
     * @brief Get current parameters for a specific node
     * @param node_id Node to query
     * @return Map of parameter name -> current value
     */
    std::map<std::string, ParameterValue> getNodeParameters(NodeID node_id);
    
    /**
     * @brief Set parameters for a specific node
     * @param node_id Node to configure
     * @param params Map of parameter name -> new value
     * @return true on success
     * 
     * Updates the node's parameter values and marks project as modified.
     */
    bool setNodeParameters(NodeID node_id, const std::map<std::string, ParameterValue>& params);

    /**
     * @brief Get raw project pointer for low-level access
     * @return Non-owning pointer to core Project
     * 
     * This is needed for components that require direct Project access (e.g., RenderPresenter).
     * The presenter retains ownership of the Project.
     */
    /**
     * @brief Get opaque handle to core project
     * @return Opaque handle to project
     * 
     * @note This method provides direct Project access for components like
     * RenderPresenter that manage DAG lifecycle. The presenter retains ownership.
     * New GUI code should use presenter methods instead.
     */
    void* getCoreProjectHandle() { 
        return external_project_ ? external_project_ : project_.get(); 
    }

private:
    // Helper to get project pointer (owned or external)
    orc::Project* getProject() { 
        return external_project_ ? external_project_ : project_.get(); 
    }
    const orc::Project* getProject() const { 
        return external_project_ ? external_project_ : project_.get(); 
    }

    std::unique_ptr<orc::Project> project_;  ///< Owned project (if constructed without existing project)
    orc::Project* external_project_ = nullptr;  ///< Non-owned external project (if constructed with existing project)
    std::string project_path_;
    bool is_modified_;
    mutable std::shared_ptr<void> dag_;      ///< Cached DAG instance
};

} // namespace orc::presenters
