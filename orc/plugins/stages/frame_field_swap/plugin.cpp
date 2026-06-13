/*
 * File:        plugin.cpp
 * Module:      orc-stage-plugin-frame-field-swap
 * Purpose:     Runtime plugin bundle for FrameFieldSwapStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "plugin.h"

#include "frame_field_swap_stage.h"

namespace {

orc::DAGStagePtr create_stage() {
  return std::make_shared<orc::FrameFieldSwapStage>();
}

}  // namespace

ORC_STAGE_PLUGIN_EXPORT const orc::StagePluginDescriptor*
orc_get_stage_plugin_descriptor() {
  return &orc::plugins::frame_field_swap::kPluginDescriptor;
}

ORC_STAGE_PLUGIN_EXPORT bool orc_register_stage_plugin(
    const orc::OrcPluginServices* services, void* context,
    bool (*register_stage)(void* context, const char* stage_name,
                           orc::OrcStageFactoryFn factory),
    const char** error_message) {
  orc::plugin::set_services(services);

  if (!register_stage) {
    if (error_message) *error_message = "Missing stage registration callback";
    return false;
  }

  const auto info = create_stage()->get_node_type_info();
  if (info.display_name != orc::plugins::frame_field_swap::kStageDisplayName ||
      info.menu_category != orc::plugins::frame_field_swap::kStageMenuCategory ||
      info.type != orc::plugins::frame_field_swap::kStageNodeType ||
      info.min_inputs != orc::plugins::frame_field_swap::kStageMinInputs ||
      info.max_inputs != orc::plugins::frame_field_swap::kStageMaxInputs ||
      info.min_outputs != orc::plugins::frame_field_swap::kStageMinOutputs ||
      info.max_outputs != orc::plugins::frame_field_swap::kStageMaxOutputs ||
      info.compatible_formats !=
          orc::plugins::frame_field_swap::kStageCompatibleFormats ||
      info.sink_category != orc::plugins::frame_field_swap::kStageSinkCategory) {
    if (error_message) {
      *error_message = "Stage metadata mismatch between plugin.h and NodeTypeInfo";
    }
    return false;
  }

  if (!register_stage(context, orc::plugins::frame_field_swap::kStageName,
                      &create_stage)) {
    if (error_message) {
      *error_message = "Failed to register stage from plugin metadata";
    }
    return false;
  }

  return true;
}
