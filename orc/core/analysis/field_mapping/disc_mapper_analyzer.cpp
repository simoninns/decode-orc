/*
 * File:        disc_mapper_analyzer.cpp
 * Module:      orc-core/analysis
 * Purpose:     Field mapping analyzer stub
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "disc_mapper_analyzer.h"
#include "../analysis_progress.h"
#include "../../include/logging.h"

namespace orc {

FieldMappingDecision DiscMapperAnalyzer::analyze(
    const VideoFieldRepresentation& source,
    const Options& /*options*/,
    AnalysisProgress* progress) {
    FieldMappingDecision decision;

    if (progress) {
        progress->setStatus("Disc mapper stub: skipping analysis");
        progress->setProgress(100);
    }

    // Minimal bookkeeping so callers can report basic context
    decision.stats.total_fields = source.field_range().size();
    auto first_descriptor = source.get_descriptor(source.field_range().start);
    if (first_descriptor) {
        decision.is_pal = (first_descriptor->format == VideoFormat::PAL);
    }
    // Stub: we don't inspect CAV picture numbers here
    decision.is_cav = false;

    decision.success = false;
    decision.rationale = "Disc mapper analysis is currently stubbed out (mapping unavailable)";
    decision.warnings.push_back("Disc mapper is disabled; no field mapping was generated.");
    decision.mapping_spec.clear();
    return decision;
}

} // namespace orc

