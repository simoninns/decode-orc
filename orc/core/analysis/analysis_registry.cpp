#include "analysis_registry.h"
#include "logging.h"

namespace orc {

// Forward declaration of force linking function
void force_analysis_tool_linking();

AnalysisRegistry& AnalysisRegistry::instance() {
    static AnalysisRegistry instance;
    static bool initialized = false;
    if (!initialized) {
        ORC_LOG_DEBUG("Initializing AnalysisRegistry...");
        force_analysis_tool_linking();
        initialized = true;
        ORC_LOG_DEBUG("AnalysisRegistry initialized with {} tools", instance.tools_.size());
    }
    return instance;
}

void AnalysisRegistry::registerTool(std::unique_ptr<AnalysisTool> tool) {
    ORC_LOG_DEBUG("Registering analysis tool: {}", tool->name());
    tools_.push_back(std::move(tool));
}

std::vector<AnalysisTool*> AnalysisRegistry::tools() const {
    std::vector<AnalysisTool*> result;
    result.reserve(tools_.size());
    for (const auto& tool : tools_) {
        result.push_back(tool.get());
    }
    return result;
}

AnalysisTool* AnalysisRegistry::findById(const std::string& id) const {
    for (const auto& tool : tools_) {
        if (tool->id() == id) {
            return tool.get();
        }
    }
    return nullptr;
}

std::vector<AnalysisTool*> AnalysisRegistry::toolsForSource(AnalysisSourceType source_type) const {
    std::vector<AnalysisTool*> result;
    for (const auto& tool : tools_) {
        if (tool->canAnalyze(source_type)) {
            result.push_back(tool.get());
        }
    }
    return result;
}

} // namespace orc
