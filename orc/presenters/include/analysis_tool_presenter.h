/*
 * File:        analysis_tool_presenter.h
 * Module:      orc-presenters
 * Purpose:     Base class for analysis tool presenters
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_PRESENTERS_ANALYSIS_TOOL_PRESENTER_H
#define ORC_PRESENTERS_ANALYSIS_TOOL_PRESENTER_H

#include <string>
#include <memory>
#include <vector>
#include <functional>
#include <map>
#include <node_id.h>
#include <orc_analysis.h>
#include <parameter_types.h>

namespace orc {
    // Forward declarations from core
    class Project;
    class DAG;
    struct DAGNode;
    class Artifact;
}

namespace orc::presenters {

/**
 * @brief Base class for all analysis tool presenters
 * 
 * This class provides common functionality needed by all analysis tool presenters:
 * - DAG building and caching
 * - Node input/output detection
 * - DAG execution up to a specific node
 * - Progress reporting
 * - Result application to graph
 * 
 * Each specialized presenter (e.g., FieldCorruptionPresenter) extends this base
 * class and implements tool-specific logic in its runAnalysis() method.
 * 
 * **MVP Architecture:**
 * - GUI calls specialized presenter methods
 * - Presenter prepares context and data for core tool
 * - Core tool performs algorithm/analysis
 * - Presenter formats results and handles apply-to-graph
 * 
 * **Example Usage:**
 * ```cpp
 * class FieldCorruptionPresenter : public AnalysisToolPresenter {
 * public:
 *     explicit FieldCorruptionPresenter(orc::Project* project)
 *         : AnalysisToolPresenter(project) {}
 *     
 *     orc::public_api::AnalysisResult runAnalysis(
 *         NodeID node_id,
 *         const std::map<std::string, orc::ParameterValue>& parameters,
 *         ProgressCallback callback = nullptr);
 * };
 * ```
 */
class AnalysisToolPresenter {
public:
    /// Progress callback type: (percentage, status_message)
    using ProgressCallback = std::function<void(int, const std::string&)>;

    /**
     * @brief Apply analysis result to graph
     * @param result Analysis result containing data to apply
     * @param node_id Node to apply result to
     * @return true if successfully applied, false otherwise
     * 
     * This delegates to the core tool's applyToGraph() method.
     * Call this from GUI after successful analysis to update the stage parameters.
     */
    bool applyResultToGraph(
        const orc::public_api::AnalysisResult& result,
        orc::NodeID node_id);

protected:
    /**
     * @brief Construct base presenter
     * @param project Project to analyze (not owned)
     * @throws std::invalid_argument if project is null
     */
    explicit AnalysisToolPresenter(orc::Project* project);

    /**
     * @brief Virtual destructor
     */
    virtual ~AnalysisToolPresenter();

    // Prevent copying
    AnalysisToolPresenter(const AnalysisToolPresenter&) = delete;
    AnalysisToolPresenter& operator=(const AnalysisToolPresenter&) = delete;

    // Allow moving
    AnalysisToolPresenter(AnalysisToolPresenter&&) noexcept;
    AnalysisToolPresenter& operator=(AnalysisToolPresenter&&) noexcept;

    // =========================================================================
    // Subclass Interface
    // =========================================================================

    /**
     * @brief Get the unique identifier for this tool
     * @return Tool ID (e.g., "field_corruption")
     */
    virtual std::string toolId() const = 0;

    /**
     * @brief Get the human-readable name for this tool
     * @return Tool name (e.g., "Field Corruption Generator")
     */
    virtual std::string toolName() const = 0;

    // =========================================================================
    // Common Utilities for Subclasses
    // =========================================================================

    /**
     * @brief Build or retrieve cached DAG from project
     * @return Shared pointer to DAG
     * @throws std::runtime_error if DAG cannot be built
     * 
     * The DAG is cached on first call. Subsequent calls return the cached DAG.
     * Call invalidateDAG() if the project structure changes.
     */
    std::shared_ptr<orc::DAG> getOrBuildDAG();

    /**
     * @brief Get the project nodes
     * @return Reference to project DAG nodes
     */
    const std::vector<orc::DAGNode>& getProjectNodes() const;

    /**
     * @brief Check if a node has at least one input connection
     * @param node_id Node to check
     * @return true if node has inputs, false otherwise
     */
    bool hasNodeInput(orc::NodeID node_id) const;

    /**
     * @brief Get the first input node ID for a given node
     * @param node_id Node to query
     * @return Input node ID if exists, invalid NodeID otherwise
     */
    orc::NodeID getFirstInputNodeId(orc::NodeID node_id) const;

    /**
     * @brief Execute DAG up to specified node and get its output artifacts
     * @param node_id Node to execute to
     * @return Vector of output artifacts from that node
     * @throws std::runtime_error if execution fails
     * 
     * This executes the DAG incrementally up to the specified node,
     * caching intermediate results. Subsequent calls with the same or
     * later nodes will reuse cached data.
     */
    std::vector<std::shared_ptr<const orc::Artifact>> executeToNode(orc::NodeID node_id);

    /**
     * @brief Invalidate cached DAG
     * 
     * Call this if the project structure changes (nodes added/removed)
     * to force DAG rebuild on next getOrBuildDAG() call.
     */
    void invalidateDAG();

    /**
     * @brief Report progress to callback if set
     * @param percentage Progress percentage (0-100)
     * @param status Status message
     */
    void reportProgress(int percentage, const std::string& status);

    /**
     * @brief Get the project pointer
     * @return Project pointer (not owned)
     */
    orc::Project* getProject() const { return project_; }

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
    
    orc::Project* project_;  // Not owned
    std::shared_ptr<orc::DAG> cached_dag_;
    ProgressCallback progress_callback_;
};

} // namespace orc::presenters

#endif // ORC_PRESENTERS_ANALYSIS_TOOL_PRESENTER_H
