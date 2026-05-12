/*
 * File:        plugin.cpp
 * Module:      orc-stage-plugin-daphne_vbi_sink
 * Purpose:     Runtime plugin bundle for DaphneVBISinkStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "daphne_vbi_sink_stage.h"
#include "plugin.h"

namespace {

orc::DAGStagePtr create_stage()
{
    return std::make_shared<orc::DaphneVBISinkStage>(orc::plugin::get_stage_services());
}

} // namespace

ORC_STAGE_PLUGIN_EXPORT const orc::StagePluginDescriptor* orc_get_stage_plugin_descriptor()
{
    return &orc::plugins::daphne_vbi_sink::kPluginDescriptor;
}

ORC_STAGE_PLUGIN_EXPORT bool orc_register_stage_plugin(
    const orc::OrcPluginServices* services,
    void* context,
    bool (*register_stage)(void* context, const char* stage_name, orc::OrcStageFactoryFn factory),
    const char** error_message)
{
    orc::plugin::set_services(services);

    if (!register_stage) {
        if (error_message) {
            *error_message = "Missing stage registration callback";
        }
        return false;
    }

    const auto node_type_info = create_stage()->get_node_type_info();
    if (node_type_info.display_name != orc::plugins::daphne_vbi_sink::kStageDisplayName ||
        node_type_info.menu_category != orc::plugins::daphne_vbi_sink::kStageMenuCategory ||
        node_type_info.type != orc::plugins::daphne_vbi_sink::kStageNodeType ||
        node_type_info.min_inputs != orc::plugins::daphne_vbi_sink::kStageMinInputs ||
        node_type_info.max_inputs != orc::plugins::daphne_vbi_sink::kStageMaxInputs ||
        node_type_info.min_outputs != orc::plugins::daphne_vbi_sink::kStageMinOutputs ||
        node_type_info.max_outputs != orc::plugins::daphne_vbi_sink::kStageMaxOutputs ||
        node_type_info.compatible_formats != orc::plugins::daphne_vbi_sink::kStageCompatibleFormats ||
        node_type_info.sink_category != orc::plugins::daphne_vbi_sink::kStageSinkCategory) {
        if (error_message) {
            *error_message = "Stage metadata mismatch between plugin.h and NodeTypeInfo";
        }
        return false;
    }

    if (!register_stage(context, orc::plugins::daphne_vbi_sink::kStageName, &create_stage)) {
        if (error_message) {
            *error_message = "Failed to register stage from plugin metadata";
        }
        return false;
    }

    return true;
}
