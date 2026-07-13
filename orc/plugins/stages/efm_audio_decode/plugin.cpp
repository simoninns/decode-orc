/*
 * File:        plugin.cpp
 * Module:      orc-stage-plugin-efm_audio_decode
 * Purpose:     Runtime plugin bundle for EFMAudioDecodeStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <orc/plugin/orc_plugin_sdk.h>

#include "efm_audio_decode_stage.h"

#ifndef ORC_STAGE_PLUGIN_VERSION
#define ORC_STAGE_PLUGIN_VERSION "dev"
#endif

namespace {

constexpr orc::StagePluginDescriptor kPluginDescriptor =
    ORC_STAGE_PLUGIN_DESCRIPTOR("decode-orc.stage.efm_audio_decode",
                                ORC_STAGE_PLUGIN_VERSION, "GPL-3.0-or-later",
                                true);

}  // namespace

ORC_DEFINE_STAGE_PLUGIN(kPluginDescriptor, orc::EFMAudioDecodeStage)
