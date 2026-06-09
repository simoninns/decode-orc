/*
 * File:        plugin.h
 * Module:      orc-stage-plugin-cvbs_source
 * Purpose:     Plugin entrypoint metadata for CVBSSourceStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <orc/plugin/orc_plugin_sdk.h>

#ifndef ORC_STAGE_PLUGIN_VERSION
#define ORC_STAGE_PLUGIN_VERSION "dev"
#endif

namespace orc::plugins::cvbs_source {

struct StageRegistrationMetadata {
  const char* stage_name;
  const char* stage_display_name;
  const char* stage_menu_category;
  orc::NodeType stage_node_type;
  uint32_t stage_min_inputs;
  uint32_t stage_max_inputs;
  uint32_t stage_min_outputs;
  uint32_t stage_max_outputs;
  orc::VideoFormatCompatibility stage_compatible_formats;
  orc::SinkCategory stage_sink_category;
};

inline constexpr StageRegistrationMetadata kPALStage{
    "PAL_CVBS_Source",
    "PAL CVBS Source",
    "Source",
    NodeType::SOURCE,
    0,
    0,
    1,
    UINT32_MAX,
    VideoFormatCompatibility::PAL_ONLY,
    SinkCategory::CORE,
};

inline constexpr StageRegistrationMetadata kNTSCStage{
    "NTSC_CVBS_Source",
    "NTSC CVBS Source",
    "Source",
    NodeType::SOURCE,
    0,
    0,
    1,
    UINT32_MAX,
    VideoFormatCompatibility::NTSC_ONLY,
    SinkCategory::CORE,
};

static_assert(kPALStage.stage_name[0] != '\0',
              "PAL stage name must not be empty");
static_assert(kNTSCStage.stage_name[0] != '\0',
              "NTSC stage name must not be empty");
static_assert(kPALStage.stage_display_name[0] != '\0',
              "PAL display name must not be empty");
static_assert(kNTSCStage.stage_display_name[0] != '\0',
              "NTSC display name must not be empty");
static_assert(kPALStage.stage_menu_category[0] != '\0',
              "PAL menu category must not be empty");
static_assert(kNTSCStage.stage_menu_category[0] != '\0',
              "NTSC menu category must not be empty");

static_assert(kPALStage.stage_max_inputs >= kPALStage.stage_min_inputs,
              "PAL inputs invalid");
static_assert(kPALStage.stage_max_outputs >= kPALStage.stage_min_outputs,
              "PAL outputs invalid");
static_assert(kNTSCStage.stage_max_inputs >= kNTSCStage.stage_min_inputs,
              "NTSC inputs invalid");
static_assert(kNTSCStage.stage_max_outputs >= kNTSCStage.stage_min_outputs,
              "NTSC outputs invalid");

inline constexpr orc::StagePluginDescriptor kPluginDescriptor{
    "decode-orc.stage.cvbs_source",
    ORC_STAGE_PLUGIN_VERSION,
    orc::kStagePluginHostAbiVersion,
    orc::kStagePluginApiVersion,
    "GPL-3.0-or-later",
    true,
};

}  // namespace orc::plugins::cvbs_source
