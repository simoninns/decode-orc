/*
 * File:        plugin.cpp
 * Module:      orc-stage-plugin-cvbs_source
 * Purpose:     Runtime plugin bundle for CVBSSourceStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "plugin.h"

#include "cvbs_source_stage.h"

namespace {

orc::DAGStagePtr create_pal_stage() {
  return std::make_shared<orc::PALCVBSSourceStage>();
}

orc::DAGStagePtr create_ntsc_stage() {
  return std::make_shared<orc::NTSCCVBSSourceStage>();
}

bool register_stage_from_metadata(
    void* context,
    bool (*register_stage)(void* context, const char* stage_name,
                           orc::OrcStageFactoryFn factory),
    const orc::plugins::cvbs_source::StageRegistrationMetadata& metadata,
    orc::DAGStagePtr (*factory)(), const char** error_message) {
  const auto node_type_info = factory()->get_node_type_info();
  if (node_type_info.display_name != metadata.stage_display_name ||
      node_type_info.menu_category != metadata.stage_menu_category ||
      node_type_info.type != metadata.stage_node_type ||
      node_type_info.min_inputs != metadata.stage_min_inputs ||
      node_type_info.max_inputs != metadata.stage_max_inputs ||
      node_type_info.min_outputs != metadata.stage_min_outputs ||
      node_type_info.max_outputs != metadata.stage_max_outputs ||
      node_type_info.compatible_formats != metadata.stage_compatible_formats ||
      node_type_info.sink_category != metadata.stage_sink_category) {
    if (error_message) {
      *error_message =
          "Stage metadata mismatch between plugin.h and NodeTypeInfo";
    }
    return false;
  }

  if (!register_stage(context, metadata.stage_name, factory)) {
    if (error_message) {
      *error_message = "Failed to register stage from plugin metadata";
    }
    return false;
  }

  return true;
}

}  // namespace

ORC_STAGE_PLUGIN_EXPORT const orc::StagePluginDescriptor*
orc_get_stage_plugin_descriptor() {
  return &orc::plugins::cvbs_source::kPluginDescriptor;
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

  if (!register_stage_from_metadata(context, register_stage,
                                    orc::plugins::cvbs_source::kPALStage,
                                    &create_pal_stage, error_message)) {
    return false;
  }

  if (!register_stage_from_metadata(context, register_stage,
                                    orc::plugins::cvbs_source::kNTSCStage,
                                    &create_ntsc_stage, error_message)) {
    return false;
  }

  return true;
}
