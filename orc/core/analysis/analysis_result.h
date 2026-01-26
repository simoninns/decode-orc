#ifndef ORC_CORE_ANALYSIS_RESULT_H
#define ORC_CORE_ANALYSIS_RESULT_H

#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/analysis/analysis_result.h. Use AnalysisPresenter instead."
#endif

// Use view-type definitions for analysis results
#include <orc_analysis.h>

// Core headers simply include the view-type and use it directly

#endif // ORC_CORE_ANALYSIS_RESULT_H
