/*
 * File:        plugin.h
 * Module:      orc-stage-plugin-stacker
 * Purpose:     Plugin entrypoint metadata for StackerStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <orc/plugin/orc_plugin_sdk.h>

#ifndef ORC_STAGE_PLUGIN_VERSION
#define ORC_STAGE_PLUGIN_VERSION "dev"
#endif

namespace orc::plugins::stacker {

inline constexpr const char* kStageName = "stacker";

inline constexpr const char* kStageDisplayName = "Stacker";
inline constexpr const char* kStageMenuCategory = "Transform";

inline constexpr orc::NodeType kStageNodeType = NodeType::MERGER;
inline constexpr uint32_t kStageMinInputs = 1;
inline constexpr uint32_t kStageMaxInputs = 16;
inline constexpr uint32_t kStageMinOutputs = 1;
inline constexpr uint32_t kStageMaxOutputs = UINT32_MAX;
inline constexpr orc::VideoFormatCompatibility kStageCompatibleFormats = VideoFormatCompatibility::ALL;
inline constexpr orc::SinkCategory kStageSinkCategory = SinkCategory::CORE;

static_assert(kStageName[0] != '\0', "kStageName must not be empty");
static_assert(kStageDisplayName[0] != '\0', "kStageDisplayName must not be empty");
static_assert(kStageMenuCategory[0] != '\0', "kStageMenuCategory must not be empty");

static_assert(kStageMaxInputs >= kStageMinInputs, "kStageMaxInputs must be >= kStageMinInputs");
static_assert(kStageMaxOutputs >= kStageMinOutputs, "kStageMaxOutputs must be >= kStageMinOutputs");

inline constexpr orc::StagePluginDescriptor kPluginDescriptor{
    "decode-orc.stage.stacker",
    ORC_STAGE_PLUGIN_VERSION,
    orc::kStagePluginHostAbiVersion,
    orc::kStagePluginApiVersion,
    "GPL-3.0-or-later",
    true,
};

} // namespace orc::plugins::stacker
