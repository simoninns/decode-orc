#ifndef ORC_CORE_ANALYSIS_RESULT_H
#define ORC_CORE_ANALYSIS_RESULT_H

#include <string>
#include <vector>
#include <map>
#include <variant>

namespace orc {

using StatisticValue = std::variant<bool, int, long long, double, std::string>;

/**
 * @brief Generic result from an analysis tool
 */
class AnalysisResult {
public:
    enum Status {
        Success,
        Failed,
        Cancelled
    };

    /**
     * @brief Individual result item (issue, warning, metric, etc.)
     */
    struct ResultItem {
        std::string type;           // "skip", "repeat", "gap", "warning", etc.
        std::string message;        // Human-readable description
        int startFrame = -1;    // Start frame (-1 if not applicable)
        int endFrame = -1;      // End frame (-1 if not applicable)
        std::map<std::string, StatisticValue> metadata;   // Tool-specific data
    };

    AnalysisResult() : status(Success) {}

    Status status;
    std::string summary;                    // Human-readable summary
    std::vector<ResultItem> items;          // Structured results
    std::map<std::string, StatisticValue> statistics;  // Statistics for display
    std::map<std::string, std::string> graphData;      // Data for graph application (opaque to GUI)
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_RESULT_H
