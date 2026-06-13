/*
 * File:        plugin.cpp
 * Module:      orc-stage-plugin-frame-phase-corrector
 * Purpose:     Runtime plugin bundle for FramePhaseCorrectorStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "plugin.h"

#include "frame_phase_corrector_stage.h"

namespace {

orc::DAGStagePtr create_stage() {
  return std::make_shared<orc::FramePhaseCorrectorStage>();
}

}  // namespace

ORC_STAGE_PLUGIN_EXPORT const orc::StagePluginDescriptor*
orc_get_stage_plugin_descriptor() {
  return &orc::plugins::frame_phase_corrector::kPluginDescriptor;
}

ORC_STAGE_PLUGIN_EXPORT bool orc_register_stage_plugin(
    const orc::OrcPluginServices* services, void* context,
    bool (*register_stage)(void* context, const char* stage_name,
                           orc::OrcStageFactoryFn factory),
    const char** error_message) {
  orc::plugin::set_services(services);

  if (!register_stage) {
    if (error_message) {
      *error_message = "Missing stage registration callback";
    }
    return false;
  }

  const auto node_type_info = create_stage()->get_node_type_info();
  if (node_type_info.display_name !=
          orc::plugins::frame_phase_corrector::kStageDisplayName ||
      node_type_info.menu_category !=
          orc::plugins::frame_phase_corrector::kStageMenuCategory ||
      node_type_info.type !=
          orc::plugins::frame_phase_corrector::kStageNodeType ||
      node_type_info.min_inputs !=
          orc::plugins::frame_phase_corrector::kStageMinInputs ||
      node_type_info.max_inputs !=
          orc::plugins::frame_phase_corrector::kStageMaxInputs ||
      node_type_info.min_outputs !=
          orc::plugins::frame_phase_corrector::kStageMinOutputs ||
      node_type_info.max_outputs !=
          orc::plugins::frame_phase_corrector::kStageMaxOutputs ||
      node_type_info.compatible_formats !=
          orc::plugins::frame_phase_corrector::kStageCompatibleFormats ||
      node_type_info.sink_category !=
          orc::plugins::frame_phase_corrector::kStageSinkCategory) {
    if (error_message) {
      *error_message =
          "Stage metadata mismatch between plugin.h and NodeTypeInfo";
    }
    return false;
  }

  if (!register_stage(context,
                      orc::plugins::frame_phase_corrector::kStageName,
                      &create_stage)) {
    if (error_message) {
      *error_message = "Failed to register stage from plugin metadata";
    }
    return false;
  }

  return true;
}
