/*
 * File:        plugin.h
 * Module:      orc-stage-plugin-source_align
 * Purpose:     Plugin entrypoint metadata for SourceAlignStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <orc/plugin/orc_plugin_sdk.h>

#ifndef ORC_STAGE_PLUGIN_VERSION
#define ORC_STAGE_PLUGIN_VERSION "dev"
#endif

namespace orc::plugins::source_align {

inline constexpr const char* kStageName = "source_align";

inline constexpr const char* kStageDisplayName = "Source Align";
inline constexpr const char* kStageMenuCategory = "Transform";

inline constexpr orc::NodeType kStageNodeType = NodeType::COMPLEX;
inline constexpr uint32_t kStageMinInputs = 1;
inline constexpr uint32_t kStageMaxInputs = 16;
inline constexpr uint32_t kStageMinOutputs = 2;
inline constexpr uint32_t kStageMaxOutputs = UINT32_MAX;
inline constexpr orc::VideoFormatCompatibility kStageCompatibleFormats =
    VideoFormatCompatibility::ALL;
inline constexpr orc::SinkCategory kStageSinkCategory = SinkCategory::CORE;

static_assert(kStageName[0] != '\0', "kStageName must not be empty");
static_assert(kStageDisplayName[0] != '\0',
              "kStageDisplayName must not be empty");
static_assert(kStageMenuCategory[0] != '\0',
              "kStageMenuCategory must not be empty");

static_assert(kStageMaxInputs >= kStageMinInputs,
              "kStageMaxInputs must be >= kStageMinInputs");
static_assert(kStageMaxOutputs >= kStageMinOutputs,
              "kStageMaxOutputs must be >= kStageMinOutputs");

inline constexpr orc::StagePluginDescriptor kPluginDescriptor{
    "decode-orc.stage.source_align",
    ORC_STAGE_PLUGIN_VERSION,
    orc::kStagePluginHostAbiVersion,
    orc::kStagePluginApiVersion,
    "GPL-3.0-or-later",
    true,
};

}  // namespace orc::plugins::source_align
