/*
 * File:        plugin.h
 * Module:      orc-stage-plugin-audio_sink
 * Purpose:     Plugin entrypoint metadata for AudioSinkStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <orc/plugin/orc_plugin_sdk.h>

#ifndef ORC_STAGE_PLUGIN_VERSION
#define ORC_STAGE_PLUGIN_VERSION "dev"
#endif

namespace orc::plugins::audio_sink {

inline constexpr const char* kStageName = "AudioSink";

inline constexpr const char* kStageDisplayName = "Analogue Audio Sink";
inline constexpr const char* kStageMenuCategory = "Sink (Core)";

inline constexpr orc::NodeType kStageNodeType = NodeType::SINK;
inline constexpr uint32_t kStageMinInputs = 1;
inline constexpr uint32_t kStageMaxInputs = 1;
inline constexpr uint32_t kStageMinOutputs = 0;
inline constexpr uint32_t kStageMaxOutputs = 0;
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
    "decode-orc.stage.audio_sink",
    ORC_STAGE_PLUGIN_VERSION,
    orc::kStagePluginHostAbiVersion,
    orc::kStagePluginApiVersion,
    "GPL-3.0-or-later",
    true,
};

}  // namespace orc::plugins::audio_sink
