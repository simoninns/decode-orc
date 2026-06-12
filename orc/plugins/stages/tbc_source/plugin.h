/*
 * File:        plugin.h
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     Plugin entrypoint metadata for TBCSourceStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <orc/plugin/orc_plugin_sdk.h>

#ifndef ORC_STAGE_PLUGIN_VERSION
#define ORC_STAGE_PLUGIN_VERSION "dev"
#endif

namespace orc::plugins::tbc_source {

// Single descriptor: the TBC source stage auto-detects video system (PAL,
// NTSC, PAL_M) and signal type (composite or YC) from its metadata at
// execute() time, so it declares ALL compatibility rather than per-system
// variants.
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

inline constexpr StageRegistrationMetadata kTBCStage{
    "tbc_source",
    "TBC Source",
    "Source",
    NodeType::SOURCE,
    0,
    0,
    1,
    UINT32_MAX,
    VideoFormatCompatibility::ALL,
    SinkCategory::CORE,
};

static_assert(kTBCStage.stage_name[0] != '\0',
              "TBC stage name must not be empty");
static_assert(kTBCStage.stage_display_name[0] != '\0',
              "TBC stage display name must not be empty");
static_assert(kTBCStage.stage_menu_category[0] != '\0',
              "TBC stage menu category must not be empty");
static_assert(kTBCStage.stage_max_inputs >= kTBCStage.stage_min_inputs,
              "TBC stage input bounds invalid");
static_assert(kTBCStage.stage_max_outputs >= kTBCStage.stage_min_outputs,
              "TBC stage output bounds invalid");

inline constexpr orc::StagePluginDescriptor kPluginDescriptor{
    "decode-orc.stage.tbc_source",
    ORC_STAGE_PLUGIN_VERSION,
    orc::kStagePluginHostAbiVersion,
    orc::kStagePluginApiVersion,
    "GPL-3.0-or-later",
    true,
};

}  // namespace orc::plugins::tbc_source
