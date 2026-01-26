#ifndef ORC_CORE_ANALYSIS_REGISTRY_H
#define ORC_CORE_ANALYSIS_REGISTRY_H

// =============================================================================
// MVP Architecture Enforcement
// =============================================================================
// This header is part of the CORE internal implementation.
// GUI code must NOT include this header directly.
// Use AnalysisPresenter from orc/presenters instead.
// =============================================================================
#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/analysis/analysis_registry.h. Use AnalysisPresenter instead."
#endif

#include "analysis_tool.h"
#include "analysis_context.h"
#include <vector>
#include <memory>
#include <string>

namespace orc {

/**
 * @brief Registry for all available analysis tools
 * 
 * Tools are registered at startup and can be queried by the GUI.
 */
class AnalysisRegistry {
public:
    /**
     * @brief Get singleton instance
     */
    static AnalysisRegistry& instance();

    /**
     * @brief Register an analysis tool
     */
    void registerTool(std::unique_ptr<AnalysisTool> tool);

    /**
     * @brief Get all registered tools
     */
    std::vector<AnalysisTool*> tools() const;

    /**
     * @brief Find tool by ID
     */
    AnalysisTool* findById(const std::string& id) const;

    /**
     * @brief Get tools that can analyze the given source type
     */
    std::vector<AnalysisTool*> toolsForSource(AnalysisSourceType source_type) const;

private:
    AnalysisRegistry() = default;
    ~AnalysisRegistry() = default;
    AnalysisRegistry(const AnalysisRegistry&) = delete;
    AnalysisRegistry& operator=(const AnalysisRegistry&) = delete;

    std::vector<std::unique_ptr<AnalysisTool>> tools_;
};

} // namespace orc

// Macro for easy tool registration
#define REGISTER_ANALYSIS_TOOL(ToolClass) \
    namespace { \
    static bool registered_##ToolClass = []() { \
        orc::AnalysisRegistry::instance().registerTool( \
            std::make_unique<ToolClass>()); \
        return true; \
    }(); \
    }

#endif // ORC_CORE_ANALYSIS_REGISTRY_H
