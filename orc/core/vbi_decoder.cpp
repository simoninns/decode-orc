/*
 * File:        vbi_decoder.cpp
 * Module:      orc-core
 * Purpose:     VBI decoding API implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "vbi_decoder.h"
#include "dag_field_renderer.h"
#include "video_field_representation.h"
#include "biphase_observer.h"
#include "logging.h"
#include <sstream>
#include <iomanip>

namespace orc {

VBIDecoder::VBIDecoder(std::shared_ptr<const DAG> dag)
    : dag_(dag)
{
}

void VBIDecoder::update_dag(std::shared_ptr<const DAG> dag) {
    dag_ = dag;
}

std::optional<VBIFieldInfo> VBIDecoder::get_vbi_for_field(
    const std::string& node_id,
    FieldID field_id)
{
    if (!dag_) {
        ORC_LOG_WARN("VBIDecoder: No DAG available");
        return std::nullopt;
    }
    
    // Render the field at the specified node
    DAGFieldRenderer renderer(dag_);
    auto render_result = renderer.render_field_at_node(node_id, field_id);
    
    if (!render_result.is_valid) {
        ORC_LOG_WARN("VBIDecoder: Failed to render field {} at node {}: {}",
                       field_id.value(), node_id, render_result.error_message);
        return std::nullopt;
    }
    
    if (!render_result.representation) {
        ORC_LOG_WARN("VBIDecoder: No representation returned for field {} at node {}",
                       field_id.value(), node_id);
        return std::nullopt;
    }
    
    // Extract VBI from the representation
    return extract_vbi_from_representation(render_result.representation.get(), field_id);
}

std::optional<VBIFieldInfo> VBIDecoder::extract_vbi_from_representation(
    const VideoFieldRepresentation* representation,
    FieldID field_id)
{
    if (!representation) {
        ORC_LOG_WARN("VBIDecoder: Null representation provided");
        return std::nullopt;
    }
    
    // Get observations for this field (now provided by DAGFieldRenderer)
    auto observations = representation->get_observations(field_id);
    
    ORC_LOG_INFO("VBIDecoder: Found {} observations for field {}", 
                  observations.size(), field_id.value());
    
    // Find BiphaseObservation
    std::shared_ptr<BiphaseObservation> biphase_obs;
    for (const auto& obs : observations) {
        if (obs) {
            ORC_LOG_DEBUG("VBIDecoder: Observation type: {}", obs->observation_type());
            if (auto bp = std::dynamic_pointer_cast<BiphaseObservation>(obs)) {
                biphase_obs = bp;
                break;
            }
        }
    }
    
    if (!biphase_obs) {
        ORC_LOG_WARN("VBIDecoder: No BiphaseObservation found for field {} (found {} total observations)", 
                     field_id.value(), observations.size());
        
        // Return empty VBI info indicating no data
        VBIFieldInfo info;
        info.field_id = field_id;
        info.has_vbi_data = false;
        info.vbi_data = {0, 0, 0};
        info.error_message = "No VBI data available";
        return info;
    }
    
    // Extract VBI data from observation
    VBIFieldInfo info;
    info.field_id = field_id;
    info.has_vbi_data = true;
    info.vbi_data = biphase_obs->vbi_data;
    info.picture_number = biphase_obs->picture_number;
    info.clv_timecode = biphase_obs->clv_timecode;
    info.chapter_number = biphase_obs->chapter_number;
    info.stop_code_present = biphase_obs->stop_code_present;
    info.lead_in = biphase_obs->lead_in;
    info.lead_out = biphase_obs->lead_out;
    info.user_code = biphase_obs->user_code;
    info.programme_status = biphase_obs->programme_status;
    info.amendment2_status = biphase_obs->amendment2_status;
    
    ORC_LOG_INFO("VBIDecoder: Successfully extracted VBI for field {}", field_id.value());
    ORC_LOG_INFO("VBIDecoder: VBI data: [0x{:06X}, 0x{:06X}, 0x{:06X}]", 
                 biphase_obs->vbi_data[0] > 0 ? biphase_obs->vbi_data[0] : 0,
                 biphase_obs->vbi_data[1] > 0 ? biphase_obs->vbi_data[1] : 0,
                 biphase_obs->vbi_data[2] > 0 ? biphase_obs->vbi_data[2] : 0);
    if (info.picture_number.has_value()) {
        ORC_LOG_INFO("VBIDecoder: Picture number: {}", info.picture_number.value());
    }
    
    return info;
}

} // namespace orc
