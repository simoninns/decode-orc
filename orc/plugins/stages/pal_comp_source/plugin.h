/*
 * File:        plugin.h
 * Module:      orc-stage-plugin-pal_comp_source
 * Purpose:     Plugin entrypoint metadata for PALCompSourceStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <orc/plugin/orc_plugin_sdk.h>

#ifndef ORC_STAGE_PLUGIN_VERSION
#define ORC_STAGE_PLUGIN_VERSION "dev"
#endif

namespace orc::plugins::pal_comp_source {

inline constexpr const char* kStageName = "PAL_Comp_Source";

inline constexpr const char* kStageDisplayName = "PAL Composite Source";
inline constexpr const char* kStageMenuCategory = "Source";

inline constexpr orc::NodeType kStageNodeType = NodeType::SOURCE;
inline constexpr uint32_t kStageMinInputs = 0;
inline constexpr uint32_t kStageMaxInputs = 0;
inline constexpr uint32_t kStageMinOutputs = 1;
inline constexpr uint32_t kStageMaxOutputs = UINT32_MAX;
inline constexpr orc::VideoFormatCompatibility kStageCompatibleFormats = VideoFormatCompatibility::PAL_ONLY;
inline constexpr orc::SinkCategory kStageSinkCategory = SinkCategory::CORE;

static_assert(kStageName[0] != '\0', "kStageName must not be empty");
static_assert(kStageDisplayName[0] != '\0', "kStageDisplayName must not be empty");
static_assert(kStageMenuCategory[0] != '\0', "kStageMenuCategory must not be empty");

static_assert(kStageMaxInputs >= kStageMinInputs, "kStageMaxInputs must be >= kStageMinInputs");
static_assert(kStageMaxOutputs >= kStageMinOutputs, "kStageMaxOutputs must be >= kStageMinOutputs");

inline constexpr orc::StagePluginDescriptor kPluginDescriptor{
    "decode-orc.stage.pal_comp_source",
    ORC_STAGE_PLUGIN_VERSION,
    orc::kStagePluginHostAbiVersion,
    orc::kStagePluginApiVersion,
    "GPL-3.0-or-later",
    true,
};

} // namespace orc::plugins::pal_comp_source
