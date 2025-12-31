#ifndef ORC_CORE_ANALYSIS_FIELD_CORRUPTION_ANALYSIS_H
#define ORC_CORE_ANALYSIS_FIELD_CORRUPTION_ANALYSIS_H

#include "../analysis_tool.h"

namespace orc {

/**
 * @brief Field corruption analysis tool
 * 
 * Generates field mapping corruption patterns for testing disc mapper
 * and field correction algorithms. This tool creates range specifications
 * that simulate laserdisc player issues (skips, repeats, gaps) which can
 * be applied to FieldMapStage.
 */
class FieldCorruptionAnalysisTool : public AnalysisTool {
public:
    std::string id() const override;
    std::string name() const override;
    std::string description() const override;
    std::string category() const override;
    
    std::vector<ParameterDescriptor> parameters() const override;
    std::vector<ParameterDescriptor> parametersForContext(const AnalysisContext& ctx) const override;
    bool canAnalyze(AnalysisSourceType source_type) const override;
    bool isApplicableToStage(const std::string& stage_name) const override;
    int priority() const override { return 1; }  // Stage-specific tool
    
    AnalysisResult analyze(const AnalysisContext& ctx,
                          AnalysisProgress* progress) override;
    
    bool canApplyToGraph() const override;
    bool applyToGraph(const AnalysisResult& result,
                     Project& project,
                     NodeID node_id) override;
    
    int estimateDurationSeconds(const AnalysisContext& ctx) const override;
};

} // namespace orc

#endif // ORC_CORE_ANALYSIS_FIELD_CORRUPTION_ANALYSIS_H
