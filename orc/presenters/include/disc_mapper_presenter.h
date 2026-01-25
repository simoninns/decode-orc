/*
 * File:        disc_mapper_presenter.h
 * Module:      orc-presenters
 * Purpose:     Presenter for Disc Mapper analysis tool
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_PRESENTERS_DISC_MAPPER_PRESENTER_H
#define ORC_PRESENTERS_DISC_MAPPER_PRESENTER_H

#include "analysis_tool_presenter.h"
#include <functional>

namespace orc::presenters {

/**
 * @brief Presenter for Disc Mapper analysis tool
 *
 * Prepares DAG/project context and maps progress/results for the GUI.
 */
class DiscMapperPresenter : public AnalysisToolPresenter {
public:
    explicit DiscMapperPresenter(void* project_handle);

    orc::public_api::AnalysisResult runAnalysis(
        NodeID node_id,
        const std::map<std::string, orc::ParameterValue>& parameters,
        std::function<void(int, const std::string&)> progress_callback = nullptr
    );

protected:
    std::string toolId() const override;
    std::string toolName() const override;
};

} // namespace orc::presenters

#endif // ORC_PRESENTERS_DISC_MAPPER_PRESENTER_H
