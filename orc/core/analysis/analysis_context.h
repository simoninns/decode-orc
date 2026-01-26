#ifndef ORC_CORE_ANALYSIS_CONTEXT_H
#define ORC_CORE_ANALYSIS_CONTEXT_H

#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/analysis/analysis_context.h. Use AnalysisPresenter instead."
#endif

#include "../include/stage_parameter.h"
#include <node_id.h>
#include <orc_analysis.h>  // For AnalysisSourceType
#include <string>
#include <map>
#include <memory>

namespace orc {

// Forward declarations
class DAG;
class Project;

/**
 * @brief Input context for running an analysis
 */
struct AnalysisContext {
    AnalysisSourceType source_type = AnalysisSourceType::LaserDisc;
    std::string source_file;      // Path to TBC or video file (legacy - prefer using dag/project)
    NodeID node_id;               // ID of node being analyzed
    std::map<std::string, ParameterValue> parameters;  // User-configured parameters
    
    // DAG execution context - preferred over source_file
    std::shared_ptr<DAG> dag;           // DAG to execute
    std::shared_ptr<Project> project;   // Project for node metadata
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_CONTEXT_H
