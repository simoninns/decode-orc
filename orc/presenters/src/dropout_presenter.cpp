/*
 * File:        dropout_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Dropout presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "dropout_presenter.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <stdexcept>

#include "../core/include/logging.h"
#include "../core/include/project.h"
#include "../core/include/video_field_representation.h"
#include "i_project_presenter.h"

namespace orc::presenters {

namespace {

std::map<uint64_t, FieldDropoutMap> parse_dropout_map_string(
    const std::string& map_str) {
  std::map<uint64_t, FieldDropoutMap> result;

  if (map_str.empty() || map_str == "[]") {
    return result;
  }

  size_t pos = 0;
  auto skip_whitespace = [&]() {
    while (pos < map_str.length() &&
           std::isspace(static_cast<unsigned char>(map_str[pos]))) {
      ++pos;
    }
  };

  auto expect_char = [&](char c) -> bool {
    skip_whitespace();
    if (pos < map_str.length() && map_str[pos] == c) {
      ++pos;
      return true;
    }
    return false;
  };

  auto parse_uint = [&]() -> uint32_t {
    skip_whitespace();
    uint32_t value = 0;
    while (pos < map_str.length() &&
           std::isdigit(static_cast<unsigned char>(map_str[pos]))) {
      value = value * 10 + static_cast<uint32_t>(map_str[pos] - '0');
      ++pos;
    }
    return value;
  };

  auto parse_dropout_region = [&]() -> DropoutRegion {
    DropoutRegion region;
    region.basis = DropoutRegion::DetectionBasis::HINT_DERIVED;

    if (!expect_char('{')) {
      return region;
    }

    while (pos < map_str.length() && map_str[pos] != '}') {
      skip_whitespace();

      std::string key;
      while (pos < map_str.length() &&
             std::isalpha(static_cast<unsigned char>(map_str[pos]))) {
        key += map_str[pos++];
      }

      if (!expect_char(':')) {
        break;
      }

      if (key == "line") {
        region.line = parse_uint();
      } else if (key == "start") {
        region.start_sample = parse_uint();
      } else if (key == "end") {
        region.end_sample = parse_uint();
      }

      expect_char(',');
    }

    expect_char('}');
    return region;
  };

  auto parse_dropout_list = [&]() -> std::vector<DropoutRegion> {
    std::vector<DropoutRegion> regions;
    if (!expect_char('[')) {
      return regions;
    }

    while (pos < map_str.length() && map_str[pos] != ']') {
      skip_whitespace();
      if (pos < map_str.length() && map_str[pos] == '{') {
        regions.push_back(parse_dropout_region());
      }
      expect_char(',');
      skip_whitespace();
    }

    expect_char(']');
    return regions;
  };

  if (!expect_char('[')) {
    ORC_LOG_ERROR("DropoutPresenter: dropout_map must start with '['");
    return result;
  }

  while (pos < map_str.length() && map_str[pos] != ']') {
    skip_whitespace();

    if (!expect_char('{')) {
      break;
    }

    FieldDropoutMap field_map;

    while (pos < map_str.length() && map_str[pos] != '}') {
      skip_whitespace();

      std::string key;
      while (pos < map_str.length() &&
             std::isalpha(static_cast<unsigned char>(map_str[pos]))) {
        key += map_str[pos++];
      }

      if (!expect_char(':')) {
        break;
      }

      if (key == "field") {
        field_map.field_id = FieldID(parse_uint());
      } else if (key == "add") {
        field_map.additions = parse_dropout_list();
      } else if (key == "remove") {
        field_map.removals = parse_dropout_list();
      }

      expect_char(',');
    }

    expect_char('}');
    result[field_map.field_id.value()] = field_map;
    expect_char(',');
  }

  return result;
}

std::string encode_dropout_map_string(
    const std::map<uint64_t, FieldDropoutMap>& map) {
  if (map.empty()) {
    return "[]";
  }

  std::ostringstream oss;
  oss << "[";

  bool first_field = true;
  for (const auto& [field_num, field_map] : map) {
    if (!first_field) {
      oss << ",";
    }
    first_field = false;

    oss << "{field:" << field_num;

    if (!field_map.additions.empty()) {
      oss << ",add:[";
      bool first_region = true;
      for (const auto& region : field_map.additions) {
        if (!first_region) {
          oss << ",";
        }
        first_region = false;
        oss << "{line:" << region.line << ",start:" << region.start_sample
            << ",end:" << region.end_sample << "}";
      }
      oss << "]";
    }

    if (!field_map.removals.empty()) {
      oss << ",remove:[";
      bool first_region = true;
      for (const auto& region : field_map.removals) {
        if (!first_region) {
          oss << ",";
        }
        first_region = false;
        oss << "{line:" << region.line << ",start:" << region.start_sample
            << ",end:" << region.end_sample << "}";
      }
      oss << "]";
    }

    oss << "}";
  }

  oss << "]";
  return oss.str();
}

}  // namespace

class DropoutPresenter::Impl {
 public:
  explicit Impl(orc::presenters::IProjectPresenter& project_presenter)
      : project_presenter_(project_presenter) {}

  orc::presenters::IProjectPresenter& project_presenter_;
};  // DEBUG: Extra brace was needed here!

DropoutPresenter::DropoutPresenter(
    orc::presenters::IProjectPresenter& project_presenter)
    : impl_(std::make_unique<Impl>(project_presenter)) {}

DropoutPresenter::~DropoutPresenter() = default;

DropoutPresenter::DropoutPresenter(DropoutPresenter&&) noexcept = default;
DropoutPresenter& DropoutPresenter::operator=(DropoutPresenter&&) noexcept =
    default;

std::vector<DetectedDropout> DropoutPresenter::detectDropouts(
    NodeID node_id, FieldID field_id) {
  return {};
}

std::vector<DetectedDropout> DropoutPresenter::getDetectedDropouts(
    NodeID node_id, FieldID field_id) const {
  return {};
}

void DropoutPresenter::clearDetections(NodeID node_id, FieldID field_id) {}

void DropoutPresenter::updateDropoutDecision(
    NodeID node_id, FieldID field_id, int line, int pixel_start,
    DropoutDecision decision, const std::string& correction_method) {}

std::vector<DropoutCorrection> DropoutPresenter::getCorrections(
    NodeID node_id, FieldID field_id) const {
  return {};
}

void DropoutPresenter::removeCorrection(NodeID node_id, FieldID field_id,
                                        int line, int pixel_start) {}

void DropoutPresenter::clearCorrections(NodeID node_id, FieldID field_id) {}

FieldDropoutStats DropoutPresenter::getFieldStats(NodeID node_id,
                                                  FieldID field_id) const {
  return FieldDropoutStats{};
}

std::map<FieldID, FieldDropoutStats> DropoutPresenter::getAllStats(
    NodeID node_id) const {
  return {};
}

int DropoutPresenter::applyDecisionToSimilar(
    NodeID node_id, FieldID field_id, const DetectedDropout& reference_dropout,
    DropoutDecision decision) {
  return 0;
}

int DropoutPresenter::autoDecideDropouts(NodeID node_id, FieldID field_id,
                                         double severity_threshold) {
  return 0;
}

bool DropoutPresenter::exportCorrections(NodeID node_id,
                                         const std::string& file_path) const {
  return false;
}

bool DropoutPresenter::importCorrections(NodeID node_id,
                                         const std::string& file_path) {
  return false;
}

std::vector<uint8_t> DropoutPresenter::getFieldData(
    const std::shared_ptr<void>& field_repr_handle, FieldID field_id,
    int& width, int& height) {
  auto field_repr =
      std::static_pointer_cast<const orc::VideoFieldRepresentation>(
          field_repr_handle);
  width = 0;
  height = 0;

  if (!field_repr) {
    ORC_LOG_ERROR(
        "DropoutPresenter::getFieldData: field_repr cast returned null");
    return {};
  }

  try {
    if (!field_repr->has_field(field_id)) {
      ORC_LOG_WARN(
          "DropoutPresenter::getFieldData: field {} doesn't exist in "
          "representation",
          field_id.value());
      return {};
    }

    auto descriptor = field_repr->get_descriptor(field_id);
    if (!descriptor) {
      ORC_LOG_ERROR(
          "DropoutPresenter::getFieldData: get_descriptor returned null for "
          "field {}",
          field_id.value());
      return {};
    }

    width = static_cast<int>(descriptor->width);
    height = static_cast<int>(descriptor->height);
    ORC_LOG_DEBUG(
        "DropoutPresenter::getFieldData: field {} has dimensions {}x{}",
        field_id.value(), width, height);

    // Get full field data - handle both composite and YC representations
    std::vector<uint16_t> field_data;

    if (field_repr->has_separate_channels()) {
      // YC source - combine Y+C for visualization
      auto y_data = field_repr->get_field_luma(field_id);
      auto c_data = field_repr->get_field_chroma(field_id);

      if (y_data.size() != c_data.size()) {
        ORC_LOG_ERROR(
            "DropoutPresenter::getFieldData: Y and C channel sizes mismatch "
            "for field {}",
            field_id.value());
        return {};
      }

      // Combine Y and C for display (simple averaging or just use Y)
      // For dropout map editing, we use Y channel as main reference
      field_data = std::move(y_data);
      ORC_LOG_DEBUG(
          "DropoutPresenter::getFieldData: Combined YC data for field {} ({} "
          "samples)",
          field_id.value(), field_data.size());
    } else {
      // Composite source
      field_data = field_repr->get_field(field_id);
      ORC_LOG_DEBUG(
          "DropoutPresenter::getFieldData: Composite data for field {} ({} "
          "samples)",
          field_id.value(), field_data.size());
    }

    if (field_data.empty()) {
      ORC_LOG_ERROR(
          "DropoutPresenter::getFieldData: got {} bytes of field data for "
          "field {}",
          field_data.size(), field_id.value());
      return {};
    }

    // Convert to 8-bit grayscale (scale 16-bit to 8-bit)
    std::vector<uint8_t> grayscale(field_data.size());
    for (size_t i = 0; i < field_data.size(); ++i) {
      grayscale[i] = static_cast<uint8_t>(field_data[i] >> 8);
    }

    ORC_LOG_DEBUG(
        "DropoutPresenter::getFieldData: converted {} samples to 8-bit "
        "grayscale for field {}",
        field_data.size(), field_id.value());
    return grayscale;
  } catch (const std::exception& e) {
    ORC_LOG_ERROR("Error getting field data: {}", e.what());
    return {};
  }
}

std::vector<DropoutRegion> DropoutPresenter::getSourceDropouts(
    const std::shared_ptr<void>& field_repr_handle, FieldID field_id) {
  auto field_repr =
      std::static_pointer_cast<const orc::VideoFieldRepresentation>(
          field_repr_handle);
  if (!field_repr) {
    return {};
  }

  try {
    if (!field_repr->has_field(field_id)) {
      return {};
    }

    // Get dropout hints from TBC
    std::vector<orc::DropoutRegion> core_dropouts =
        field_repr->get_dropout_hints(field_id);

    // DropoutRegion is aliased to public_api::DropoutRegion, so just return
    // directly
    return core_dropouts;
  } catch (const std::exception& e) {
    ORC_LOG_ERROR("Error getting source dropouts: {}", e.what());
    return {};
  }
}

std::map<uint64_t, FieldDropoutMap> DropoutPresenter::getDropoutMap(
    NodeID node_id) {
  try {
    // Find the node in the project
    const auto& nodes = impl_->project_presenter_.getNodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
                                [&node_id](const orc::presenters::NodeInfo& n) {
                                  return n.node_id == node_id;
                                });

    if (node_it == nodes.end()) {
      ORC_LOG_WARN("Node {} not found in project", node_id.to_string());
      return {};
    }

    // Check if it's a dropout_map stage
    if (node_it->stage_name != "dropout_map") {
      ORC_LOG_WARN("Node {} is not a dropout_map stage (it's {})",
                   node_id.to_string(), node_it->stage_name);
      return {};
    }

    // Get parameters
    auto params = impl_->project_presenter_.getNodeParameters(node_id);
    auto it = params.find("dropout_map");
    if (it == params.end()) {
      return {};
    }

    std::string map_str = std::get<std::string>(it->second);

    return parse_dropout_map_string(map_str);
  } catch (const std::exception& e) {
    ORC_LOG_ERROR("Error getting dropout map: {}", e.what());
    return {};
  }
}

bool DropoutPresenter::setDropoutMap(
    NodeID node_id, const std::map<uint64_t, FieldDropoutMap>& dropout_map) {
  try {
    std::string map_str = encode_dropout_map_string(dropout_map);

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

size_t DropoutPresenter::getFieldCount(
    const std::shared_ptr<void>& field_repr_handle) {
  auto field_repr =
      std::static_pointer_cast<const orc::VideoFieldRepresentation>(
          field_repr_handle);
  if (!field_repr) {
    return 0;
  }
  return field_repr->field_count();
}

}  // namespace orc::presenters
