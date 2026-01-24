#ifndef ORC_CORE_ANALYSIS_TOOL_H
#define ORC_CORE_ANALYSIS_TOOL_H

#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/analysis/analysis_tool.h. Use AnalysisPresenter instead."
#endif

#include "analysis_context.h"
#include "analysis_result.h"
#include "analysis_progress.h"
#include "../include/project.h"
#include "../include/stage_parameter.h"
#include <string>
#include <vector>

namespace orc {

/**
 * @brief Abstract base class for all analysis tools
 * 
 * Analysis tools inspect TBC data and report issues, metrics, or other
 * diagnostic information without modifying the source data.
 */
class AnalysisTool {
public:
    virtual ~AnalysisTool() = default;

    /**
     * @brief Unique identifier for this tool
     */
    virtual std::string id() const = 0;

    /**
     * @brief Human-readable name
     */
    virtual std::string name() const = 0;

    /**
     * @brief Description of what this tool does
     */
    virtual std::string description() const = 0;

    /**
     * @brief Category for menu organization
     */
    virtual std::string category() const = 0;

    /**
     * @brief Get parameter definitions for this tool
     */
    virtual std::vector<ParameterDescriptor> parameters() const = 0;

    /**
     * @brief Get parameter definitions for this tool with context
     * @param ctx Analysis context (can be used to show/hide parameters)
     * @return Parameter descriptors (default: calls parameters())
     */
    virtual std::vector<ParameterDescriptor> parametersForContext(const AnalysisContext& ctx) const {
        (void)ctx;
        return parameters();
    }

    /**
     * @brief Check if this tool can analyze the given source type
     */
    virtual bool canAnalyze(AnalysisSourceType source_type) const = 0;

    /**
     * @brief Check if this tool is applicable to the given stage type
     * @param stage_name Name of the stage type (e.g., "field_map", "PAL_Comp_Source")
     * @return true if this tool can be used with this stage type
     */
    virtual bool isApplicableToStage(const std::string& stage_name) const = 0;

    /**
     * @brief Get priority for menu ordering
     * @return Priority level (1 = stage-specific tools, 2 = common tools)
     * 
     * Lower numbers appear first in menus. Priority 1 is for tools that are
     * specific to a particular stage type. Priority 2 is for common batch
     * analysis tools that work across multiple stage types.
     */
    virtual int priority() const { return 2; }

    /**
     * @brief Run the analysis
     * @param ctx Analysis context with source and parameters
     * @param progress Progress reporter (can be null)
     * @return Analysis result
     */
    virtual AnalysisResult analyze(const AnalysisContext& ctx,
                                   AnalysisProgress* progress) = 0;

    /**
     * @brief Can this analysis be applied to the graph?
     */
    virtual bool canApplyToGraph() const = 0;

    /**
     * @brief Determine parameter changes to apply to the graph
     * @param result Analysis result (should populate result.parameterChanges)
     * @param project Read-only project for querying node structure
     * @param node_id Target node ID to prepare changes for
     * @return true if changes were successfully determined
     * 
     * This method should populate result.parameterChanges with the parameters
     * to modify. The caller (presenter) will apply these changes through
     * proper channels. Tools should NOT modify the project directly.
     */
    virtual bool applyToGraph(AnalysisResult& result,
                             const Project& project,
                             NodeID node_id) = 0;

    /**
     * @brief Estimate analysis duration in seconds (-1 if unknown)
     */
    virtual int estimateDurationSeconds(const AnalysisContext& ctx) const {
        (void)ctx;
        return -1;
    }
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_TOOL_H
