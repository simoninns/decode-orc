/*
 * File:        dropout_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Dropout presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "dropout_presenter.h"
#include "project_presenter.h"  // Need full definition to call methods
#include "../core/include/project.h"
#include "../core/include/logging.h"
#include "../core/include/video_field_representation.h"
#include "../core/stages/dropout_map/dropout_map_stage.h"
#include <stdexcept>
#include <algorithm>

namespace orc::presenters {

class DropoutPresenter::Impl {
public:
    explicit Impl(orc::presenters::ProjectPresenter& project_presenter)
        : project_presenter_(project_presenter)
    {
    }
    
    orc::presenters::ProjectPresenter& project_presenter_;
}; // DEBUG: Extra brace was needed here!

DropoutPresenter::DropoutPresenter(orc::presenters::ProjectPresenter& project_presenter)
    : impl_(std::make_unique<Impl>(project_presenter))
{
}

DropoutPresenter::~DropoutPresenter() = default;

DropoutPresenter::DropoutPresenter(DropoutPresenter&&) noexcept = default;
DropoutPresenter& DropoutPresenter::operator=(DropoutPresenter&&) noexcept = default;

std::vector<DetectedDropout> DropoutPresenter::detectDropouts(NodeID node_id, FieldID field_id)
{
    return {};
}

std::vector<DetectedDropout> DropoutPresenter::getDetectedDropouts(NodeID node_id, FieldID field_id) const
{
    return {};
}

void DropoutPresenter::clearDetections(NodeID node_id, FieldID field_id)
{
}

void DropoutPresenter::updateDropoutDecision(NodeID node_id, FieldID field_id, 
                                              int line, int pixel_start,
                                              DropoutDecision decision,
                                              const std::string& correction_method)
{
}

std::vector<DropoutCorrection> DropoutPresenter::getCorrections(NodeID node_id, FieldID field_id) const
{
    return {};
}

void DropoutPresenter::removeCorrection(NodeID node_id, FieldID field_id, int line, int pixel_start)
{
}

void DropoutPresenter::clearCorrections(NodeID node_id, FieldID field_id)
{
}

FieldDropoutStats DropoutPresenter::getFieldStats(NodeID node_id, FieldID field_id) const
{
    return FieldDropoutStats{};
}

std::map<FieldID, FieldDropoutStats> DropoutPresenter::getAllStats(NodeID node_id) const
{
    return {};
}

int DropoutPresenter::applyDecisionToSimilar(NodeID node_id, FieldID field_id,
                                              const DetectedDropout& reference_dropout,
                                              DropoutDecision decision)
{
    return 0;
}

int DropoutPresenter::autoDecideDropouts(NodeID node_id, FieldID field_id, double severity_threshold)
{
    return 0;
}

bool DropoutPresenter::exportCorrections(NodeID node_id, const std::string& file_path) const
{
    return false;
}

bool DropoutPresenter::importCorrections(NodeID node_id, const std::string& file_path)
{
    return false;
}

std::vector<uint8_t> DropoutPresenter::getFieldData(const std::shared_ptr<const orc::VideoFieldRepresentation>& field_repr,
                                                     FieldID field_id, int& width, int& height)
{
    width = 0;
    height = 0;
    
    if (!field_repr) {
        return {};
    }
    
    try {
        if (!field_repr->has_field(field_id)) {
            return {};
        }
        
        auto descriptor = field_repr->get_descriptor(field_id);
        if (!descriptor) {
            return {};
        }
        
        width = static_cast<int>(descriptor->width);
        height = static_cast<int>(descriptor->height);
        
        // Get full field data
        std::vector<uint16_t> field_data = field_repr->get_field(field_id);
        
        // Convert to 8-bit grayscale (scale 16-bit to 8-bit)
        std::vector<uint8_t> grayscale(field_data.size());
        for (size_t i = 0; i < field_data.size(); ++i) {
            grayscale[i] = static_cast<uint8_t>(field_data[i] >> 8);
        }
        
        return grayscale;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Error getting field data: {}", e.what());
        return {};
    }
}

std::vector<DropoutRegion> DropoutPresenter::getSourceDropouts(const std::shared_ptr<const orc::VideoFieldRepresentation>& field_repr,
                                                                FieldID field_id)
{
    if (!field_repr) {
        return {};
    }
    
    try {
        if (!field_repr->has_field(field_id)) {
            return {};
        }
        
        // Get dropout hints from TBC
        std::vector<orc::DropoutRegion> core_dropouts = field_repr->get_dropout_hints(field_id);
        
        // DropoutRegion is aliased to public_api::DropoutRegion, so just return directly
        return core_dropouts;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Error getting source dropouts: {}", e.what());
        return {};
    }
}

std::map<uint64_t, FieldDropoutMap> DropoutPresenter::getDropoutMap(NodeID node_id)
{
    try {
        // Find the node in the project
        const auto& nodes = impl_->project_presenter_.getNodes();
        auto node_it = std::find_if(nodes.begin(), nodes.end(),
            [&node_id](const orc::presenters::NodeInfo& n) { return n.node_id == node_id; });
        
        if (node_it == nodes.end()) {
            ORC_LOG_WARN("Node {} not found in project", node_id.to_string());
            return {};
        }
        
        // Check if it's a dropout_map stage
        if (node_it->stage_name != "dropout_map") {
            ORC_LOG_WARN("Node {} is not a dropout_map stage (it's {})", node_id.to_string(), node_it->stage_name);
            return {};
        }
        
        // Get parameters
        auto params = impl_->project_presenter_.getNodeParameters(node_id);
        auto it = params.find("dropout_map");
        if (it == params.end()) {
            return {};
        }
        
        std::string map_str = std::get<std::string>(it->second);
        
        // Parse core dropout map
        std::map<uint64_t, orc::FieldDropoutMap> core_map = orc::DropoutMapStage::parse_dropout_map(map_str);
        
        // Convert to presenter types (FieldDropoutMap structure, DropoutRegion is aliased so no conversion needed)
        std::map<uint64_t, FieldDropoutMap> presenter_map;
        for (const auto& [field_id, core_field_map] : core_map) {
            FieldDropoutMap presenter_field_map(core_field_map.field_id);
            presenter_field_map.additions = core_field_map.additions;
            presenter_field_map.removals = core_field_map.removals;
            presenter_map[field_id] = presenter_field_map;
        }
        
        return presenter_map;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Error getting dropout map: {}", e.what());
        return {};
    }
}

bool DropoutPresenter::setDropoutMap(NodeID node_id, const std::map<uint64_t, FieldDropoutMap>& dropout_map)
{
    try {
        // Convert presenter types to core types (FieldDropoutMap structure, DropoutRegion is aliased so no conversion needed)
        std::map<uint64_t, orc::FieldDropoutMap> core_map;
        for (const auto& [field_id, presenter_field_map] : dropout_map) {
            orc::FieldDropoutMap core_field_map(presenter_field_map.field_id);
            core_field_map.additions = presenter_field_map.additions;
            core_field_map.removals = presenter_field_map.removals;
            core_map[field_id] = core_field_map;
        }
        
        // Encode to string
        std::string map_str = orc::DropoutMapStage::encode_dropout_map(core_map);
        
        // Set parameter using ProjectPresenter
        std::map<std::string, orc::ParameterValue> params;
        params["dropout_map"] = map_str;
        
        impl_->project_presenter_.setNodeParameters(node_id, params);
        
        return true;
    } catch (const std::exception& e) {
        ORC_LOG_ERROR("Error setting dropout map: {}", e.what());
        return false;
    }
}

size_t DropoutPresenter::getFieldCount(const std::shared_ptr<const orc::VideoFieldRepresentation>& field_repr)
{
    if (!field_repr) {
        return 0;
    }
    return field_repr->field_count();
}

} // namespace orc::presenters
