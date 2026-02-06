#ifndef ORC_CORE_ANALYSIS_FIELD_MAP_RANGE_ANALYSIS_H
#define ORC_CORE_ANALYSIS_FIELD_MAP_RANGE_ANALYSIS_H

#include "../analysis_tool.h"

namespace orc {

/**
 * @brief Field map range analysis tool
 * 
 * Locates a field range based on user-specified start/end addresses
 * (picture number or CLV timecode) and prepares a FieldMapStage
 * range specification.
 */
class FieldMapRangeAnalysisTool : public AnalysisTool {
public:
    std::string id() const override;
    std::string name() const override;
    std::string description() const override;
    std::string category() const override;
    
    std::vector<ParameterDescriptor> parameters() const override;
    bool canAnalyze(AnalysisSourceType source_type) const override;
    bool isApplicableToStage(const std::string& stage_name) const override;
    int priority() const override { return 1; }
    
    AnalysisResult analyze(const AnalysisContext& ctx,
                          AnalysisProgress* progress) override;
    
    bool canApplyToGraph() const override;
    bool applyToGraph(AnalysisResult& result,
                     const Project& project,
                     NodeID node_id) override;
    
    int estimateDurationSeconds(const AnalysisContext& ctx) const override;
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_FIELD_MAP_RANGE_ANALYSIS_H
