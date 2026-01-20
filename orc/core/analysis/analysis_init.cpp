/*
 * File:        analysis_init.cpp
 * Module:      orc-core/analysis
 * Purpose:     Analysis tool initialization
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "field_mapping/field_mapping_analysis.h"
#include "field_mapping/field_mapping_range_analysis.h"
#include "field_corruption/field_corruption_analysis.h"
#include "vectorscope/vectorscope_analysis.h"
#include "dropout/dropout_analysis.h"
#include "dropout_editor_tool.h"
#include "snr/snr_analysis.h"
#include "burst_level/burst_level_analysis.h"
#include "source_alignment/source_alignment_analysis.h"
#include "mask_line/mask_line_analysis.h"
#include "ffmpeg_preset/ffmpeg_preset_analysis.h"
#include "logging.h"
#include <memory>

namespace orc {

/**
 * @brief Force linking of all analysis tool object files
 * 
 * This function creates dummy instances to force the linker to include
 * all analysis tool object files, which ensures their static registrations execute.
 * This must be called before any analysis tool lookups occur.
 */
void force_analysis_tool_linking() {
    ORC_LOG_WARN("Analysis tool linking skipped (analysis observers disabled)");
}

} // namespace orc
