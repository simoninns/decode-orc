#ifndef ORC_CORE_ANALYSIS_PROGRESS_H
#define ORC_CORE_ANALYSIS_PROGRESS_H

#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/analysis/analysis_progress.h. Use AnalysisPresenter instead."
#endif

#include "analysis_result.h"
#include <string>
#include <atomic>

namespace orc {

/**
 * @brief Abstract interface for progress reporting during analysis
 */
class AnalysisProgress {
public:
    virtual ~AnalysisProgress() = default;

    /**
     * @brief Report progress percentage (0-100)
     */
    virtual void setProgress(int percentage) = 0;

    /**
     * @brief Update current status message
     */
    virtual void setStatus(const std::string& message) = 0;

    /**
     * @brief Report substep (e.g., "Processing field 1000/2000")
     */
    virtual void setSubStatus(const std::string& message) = 0;

    /**
     * @brief Check if user requested cancellation
     */
    virtual bool isCancelled() const = 0;

    /**
     * @brief Report intermediate result (for live updates)
     */
    virtual void reportPartialResult(const AnalysisResultItem& item) = 0;
};

/**
 * @brief Null progress implementation (does nothing)
 */
class NullProgress : public AnalysisProgress {
public:
    void setProgress(int) override {}
    void setStatus(const std::string&) override {}
    void setSubStatus(const std::string&) override {}
    bool isCancelled() const override { return false; }
    void reportPartialResult(const AnalysisResultItem&) override {}
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_PROGRESS_H
