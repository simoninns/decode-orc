/*
 * File:        analysis_init.cpp
 * Module:      orc-core/analysis
 * Purpose:     Analysis tool initialization
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "disc_mapper/disc_mapper_analysis.h"
#include "field_corruption/field_corruption_analysis.h"
#include "vectorscope/vectorscope_analysis.h"
#include "dropout/dropout_editor_tool.h"
#include "source_alignment/source_alignment_analysis.h"
#include "mask_line/mask_line_analysis.h"
#include "ffmpeg_preset/ffmpeg_preset_analysis.h"
#include "logging.h"
#include <memory>

namespace orc {

// Forward declarations of force-link functions for analysis tools we want enabled
void force_link_FFmpegPresetAnalysisTool();
void force_link_FieldCorruptionAnalysisTool();
void force_link_DiscMapperAnalysisTool();
void force_link_SourceAlignmentAnalysisTool();
void force_link_MaskLineAnalysisTool();
void force_link_DropoutEditorTool();
void force_link_VectorscopeAnalysisTool();

/**
 * @brief Force linking of all analysis tool object files
 * 
 * This function creates dummy instances to force the linker to include
 * all analysis tool object files, which ensures their static registrations execute.
 * This must be called before any analysis tool lookups occur.
 */
void force_analysis_tool_linking() {
    // Only enable the FFmpeg preset analysis tool for now.
    // Additional tools can be added here as they are re-enabled/refactored.
    ORC_LOG_DEBUG("Forcing link of analysis tools: FFmpeg preset, field corruption, disc mapper, source alignment, mask line, dropout editor, vectorscope");
    force_link_FFmpegPresetAnalysisTool();
    force_link_FieldCorruptionAnalysisTool();
    force_link_DiscMapperAnalysisTool();
    force_link_SourceAlignmentAnalysisTool();
    force_link_MaskLineAnalysisTool();
    force_link_DropoutEditorTool();
    force_link_VectorscopeAnalysisTool();
}

} // namespace orc
