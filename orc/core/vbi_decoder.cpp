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

namespace orc {

VBIDecoder::VBIDecoder(std::shared_ptr<const DAG> dag)
    : dag_(dag)
    , dag_version_(0)
    , renderer_(dag ? std::make_unique<DAGFieldRenderer>(dag) : nullptr)
{
}

void VBIDecoder::update_dag(std::shared_ptr<const DAG> dag) {
    dag_ = dag;
    dag_version_++;
    vbi_cache_.clear();
    renderer_ = dag ? std::make_unique<DAGFieldRenderer>(dag) : nullptr;
}

std::optional<VBIFieldInfo> VBIDecoder::get_vbi_for_field(
    const NodeID& node_id,
    FieldID field_id)
{
    if (!dag_) {
        ORC_LOG_WARN("VBIDecoder: No DAG available");
        return std::nullopt;
    }
    
    if (!renderer_) {
        ORC_LOG_WARN("VBIDecoder: No renderer available");
        return std::nullopt;
    }
    
    // Check cache first (efficiency improvement #1: persistent caching)
    VBICacheKey cache_key{node_id, field_id.value(), dag_version_};
    if (auto cached = vbi_cache_.get(cache_key)) {
        ORC_LOG_DEBUG("VBIDecoder: Cache hit for field {} at node {}", 
                      field_id.value(), node_id);
        return cached;
    }
    
    // Render the field at the specified node
    auto render_result = renderer_->render_field_at_node(node_id, field_id);
    
    if (!render_result.is_valid) {
        ORC_LOG_WARN("VBIDecoder: Failed to render field {} at node {}: {}",
                       field_id.value(), node_id.to_string(), render_result.error_message);
        return std::nullopt;
    }
    
    if (!render_result.representation) {
        ORC_LOG_WARN("VBIDecoder: No representation returned for field {} at node {}",
                       field_id.value(), node_id.to_string());
        return std::nullopt;
    }
    
    // Extract VBI from the representation
    auto result = extract_vbi_from_representation(render_result.representation.get(), field_id);
    
    // Cache the result for future queries
    if (result.has_value()) {
        vbi_cache_.put(cache_key, *result);
    }
    
    return result;
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
    
    if (observations.empty()) {
        ORC_LOG_DEBUG("VBIDecoder: No observations found for field {}", field_id.value());
        VBIFieldInfo info;
        info.field_id = field_id;
        info.has_vbi_data = false;
        info.vbi_data = {0, 0, 0};
        info.error_message = "No VBI data available";
        return info;
    }
    
    // Find BiphaseObservation (efficiency improvement #2: check type before casting)
    std::shared_ptr<BiphaseObservation> biphase_obs;
    for (const auto& obs : observations) {
        if (obs && obs->observation_type() == "Biphase") {
            biphase_obs = std::dynamic_pointer_cast<BiphaseObservation>(obs);
            if (biphase_obs) break;
        }
    }
    
    if (!biphase_obs) {
        ORC_LOG_DEBUG("VBIDecoder: No BiphaseObservation found for field {}", field_id.value());
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
    
    ORC_LOG_DEBUG("VBIDecoder: Successfully extracted VBI for field {}", field_id.value());
    
    return info;
}

} // namespace orc
