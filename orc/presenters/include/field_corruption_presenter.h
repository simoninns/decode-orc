/*
 * File:        field_corruption_presenter.h
 * Module:      orc-presenters
 * Purpose:     Presenter for Field Corruption Generator analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_PRESENTERS_FIELD_CORRUPTION_PRESENTER_H
#define ORC_PRESENTERS_FIELD_CORRUPTION_PRESENTER_H

#include "analysis_tool_presenter.h"
#include <functional>

namespace orc::presenters {

/**
 * @brief Presenter for Field Corruption Generator analysis tool
 * 
 * Handles:
 * - Preparing DAG context for the tool
 * - Executing input node to get field count
 * - Formatting results for display
 * - Applying generated pattern to field_map stage
 * 
 * This presenter encapsulates the business logic for running field corruption
 * analysis, keeping the core tool focused on the algorithm and the GUI focused
 * on UI concerns.
 */
class FieldCorruptionPresenter : public AnalysisToolPresenter {
public:
    /**
     * @brief Construct a Field Corruption presenter
     * @param project The project to operate on (not owned)
     */
    explicit FieldCorruptionPresenter(orc::Project* project);
    
    /**
     * @brief Run field corruption analysis
     * 
     * This method:
     * 1. Validates the node is a field_map stage
     * 2. Builds the DAG from the project
     * 3. Executes the input node to get field count
     * 4. Calls the core field corruption tool with prepared context
     * 5. Formats results for display
     * 
     * @param node_id The field_map node to analyze
     * @param parameters User-selected parameters (pattern type, regenerate_seed, etc.)
     * @param progress_callback Optional progress updates (percentage, status message)
     * @return Analysis result with generated pattern or error information
     */
    orc::public_api::AnalysisResult runAnalysis(
        NodeID node_id,
        const std::map<std::string, orc::ParameterValue>& parameters,
        std::function<void(int, const std::string&)> progress_callback = nullptr
    );
    
protected:
    std::string toolId() const override;
    std::string toolName() const override;

private:
    /**
     * @brief Get field count from the input node
     * @param node_id The field_map node
     * @return Field count, or 0 if unable to determine
     */
    uint64_t getInputFieldCount(NodeID node_id);
    
    /**
     * @brief Extract existing seed from node parameters
     * @param node_id The field_map node
     * @return Existing seed value, or 0 if not set
     */
    int32_t getExistingSeed(NodeID node_id);
};

} // namespace orc::presenters

#endif // ORC_PRESENTERS_FIELD_CORRUPTION_PRESENTER_H
