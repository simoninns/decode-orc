#ifndef ORC_CORE_ANALYSIS_FIELD_MAPPING_ANALYSIS_H
#define ORC_CORE_ANALYSIS_FIELD_MAPPING_ANALYSIS_H

#include "../analysis_tool.h"

namespace orc {

/**
 * @brief Field mapping analysis tool
 * 
 * Analyzes TBC files to detect skipped, repeated, and missing fields
 * that indicate laserdisc player tracking problems.
 */
class FieldMappingAnalysisTool : public AnalysisTool {
public:
    std::string id() const override;
    std::string name() const override;
    std::string description() const override;
    std::string category() const override;
    
    std::vector<ParameterDescriptor> parameters() const override;
    bool canAnalyze(AnalysisSourceType source_type) const override;
    bool isApplicableToStage(const std::string& stage_name) const override;
    
    AnalysisResult analyze(const AnalysisContext& ctx,
                          AnalysisProgress* progress) override;
    
    bool canApplyToGraph() const override;
    bool applyToGraph(const AnalysisResult& result,
                     Project& project,
                     NodeID node_id) override;
    
    int estimateDurationSeconds(const AnalysisContext& ctx) const override;
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_FIELD_MAPPING_ANALYSIS_H
