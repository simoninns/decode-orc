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

#include "../core/include/artifact.h"
#include "../core/include/frame_line_util.h"
#include "../core/include/logging.h"
#include "../core/include/project.h"
#include "../core/include/video_frame_representation.h"
#include "i_project_presenter.h"

namespace orc::presenters {

namespace {

std::map<uint64_t, FrameDropoutMap> parse_dropout_map_string(
    const std::string& map_str) {
  std::map<uint64_t, FrameDropoutMap> result;

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

    FrameDropoutMap frame_map;

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

      if (key == "frame") {
        frame_map.frame_id = static_cast<FrameID>(parse_uint());
      } else if (key == "add") {
        frame_map.additions = parse_dropout_list();
      } else if (key == "remove") {
        frame_map.removals = parse_dropout_list();
      }

      expect_char(',');
    }

    expect_char('}');
    result[frame_map.frame_id] = frame_map;
    expect_char(',');
  }

  return result;
}

std::string encode_dropout_map_string(
    const std::map<uint64_t, FrameDropoutMap>& map) {
  if (map.empty()) {
    return "[]";
  }

  std::ostringstream oss;
  oss << "[";

  bool first_frame = true;
  for (const auto& [frame_num, frame_map] : map) {
    if (!first_frame) {
      oss << ",";
    }
    first_frame = false;

    oss << "{frame:" << frame_num;

    if (!frame_map.additions.empty()) {
      oss << ",add:[";
      bool first_region = true;
      for (const auto& region : frame_map.additions) {
        if (!first_region) {
          oss << ",";
        }
        first_region = false;
        oss << "{line:" << region.line << ",start:" << region.start_sample
            << ",end:" << region.end_sample << "}";
      }
      oss << "]";
    }

    if (!frame_map.removals.empty()) {
      oss << ",remove:[";
      bool first_region = true;
      for (const auto& region : frame_map.removals) {
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
};

DropoutPresenter::DropoutPresenter(
    orc::presenters::IProjectPresenter& project_presenter)
    : impl_(std::make_unique<Impl>(project_presenter)) {}

DropoutPresenter::~DropoutPresenter() = default;

DropoutPresenter::DropoutPresenter(DropoutPresenter&&) noexcept = default;
DropoutPresenter& DropoutPresenter::operator=(DropoutPresenter&&) noexcept =
    default;

std::vector<DetectedDropout> DropoutPresenter::detectDropouts(
    NodeID node_id, FrameID frame_id) {
  (void)node_id;
  (void)frame_id;
  return {};
}

std::vector<DetectedDropout> DropoutPresenter::getDetectedDropouts(
    NodeID node_id, FrameID frame_id) const {
  (void)node_id;
  (void)frame_id;
  return {};
}

void DropoutPresenter::clearDetections(NodeID node_id, FrameID frame_id) {
  (void)node_id;
  (void)frame_id;
}

void DropoutPresenter::updateDropoutDecision(
    NodeID node_id, FrameID frame_id, int line, int pixel_start,
    DropoutDecision decision, const std::string& correction_method) {
  (void)node_id;
  (void)frame_id;
  (void)line;
  (void)pixel_start;
  (void)decision;
  (void)correction_method;
}

std::vector<DropoutCorrection> DropoutPresenter::getCorrections(
    NodeID node_id, FrameID frame_id) const {
  (void)node_id;
  (void)frame_id;
  return {};
}

void DropoutPresenter::removeCorrection(NodeID node_id, FrameID frame_id,
                                        int line, int pixel_start) {
  (void)node_id;
  (void)frame_id;
  (void)line;
  (void)pixel_start;
}

void DropoutPresenter::clearCorrections(NodeID node_id, FrameID frame_id) {
  (void)node_id;
  (void)frame_id;
}

FrameDropoutStats DropoutPresenter::getFrameStats(NodeID node_id,
                                                  FrameID frame_id) const {
  (void)node_id;
  (void)frame_id;
  return FrameDropoutStats{};
}

std::map<FrameID, FrameDropoutStats> DropoutPresenter::getAllStats(
    NodeID node_id) const {
  (void)node_id;
  return {};
}

int DropoutPresenter::applyDecisionToSimilar(
    NodeID node_id, FrameID frame_id, const DetectedDropout& reference_dropout,
    DropoutDecision decision) {
  (void)node_id;
  (void)frame_id;
  (void)reference_dropout;
  (void)decision;
  return 0;
}

int DropoutPresenter::autoDecideDropouts(NodeID node_id, FrameID frame_id,
                                         double severity_threshold) {
  (void)node_id;
  (void)frame_id;
  (void)severity_threshold;
  return 0;
}

bool DropoutPresenter::exportCorrections(NodeID node_id,
                                         const std::string& file_path) const {
  (void)node_id;
  (void)file_path;
  return false;
}

bool DropoutPresenter::importCorrections(NodeID node_id,
                                         const std::string& file_path) {
  (void)node_id;
  (void)file_path;
  return false;
}

std::vector<uint8_t> DropoutPresenter::getFrameData(
    const std::shared_ptr<void>& vfr_handle, FrameID frame_id, int& width,
    int& height) {
  width = 0;
  height = 0;

  auto artifact_base =
      std::static_pointer_cast<const orc::Artifact>(vfr_handle);
  auto vfr = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
      artifact_base);
  if (!vfr) {
    ORC_LOG_ERROR(
        "DropoutPresenter::getFrameData: no VideoFrameRepresentation found");
    return {};
  }

  try {
    if (!vfr->has_frame(frame_id)) {
      ORC_LOG_WARN(
          "DropoutPresenter::getFrameData: frame {} not in representation",
          frame_id);
      return {};
    }

    auto desc = vfr->get_frame_descriptor(frame_id);
    if (!desc) {
      ORC_LOG_ERROR(
          "DropoutPresenter::getFrameData: no descriptor for frame {}",
          frame_id);
      return {};
    }

    size_t frame_height = desc->height;
    size_t samples_per_line = desc->samples_per_line_nominal;

    width = static_cast<int>(samples_per_line);
    height = static_cast<int>(frame_height);

    ORC_LOG_DEBUG("DropoutPresenter::getFrameData: frame {} dims {}x{}",
                  frame_id, width, height);

    // Extract all lines of the frame and convert int16_t CVBS_U10_4FSC to
    // 8-bit grayscale. Displayed width is samples_per_line_nominal; the rare
    // PAL lines with one extra sample are truncated to nominal width.
    std::vector<uint8_t> grayscale;
    grayscale.reserve(frame_height * samples_per_line);

    bool use_luma = vfr->has_separate_channels();

    for (size_t line = 0; line < frame_height; ++line) {
      const orc::VideoFrameRepresentation::sample_type* line_data =
          use_luma ? vfr->get_line_luma(frame_id, line)
                   : vfr->get_line(frame_id, line);
      if (!line_data) {
        for (size_t s = 0; s < samples_per_line; ++s) {
          grayscale.push_back(0);
        }
        continue;
      }
      for (size_t s = 0; s < samples_per_line; ++s) {
        int16_t sample = line_data[s];
        int32_t clamped = (sample < 0) ? 0 : (sample > 1023 ? 1023 : sample);
        grayscale.push_back(static_cast<uint8_t>(clamped >> 2));
      }
    }

    ORC_LOG_DEBUG(
        "DropoutPresenter::getFrameData: {} samples extracted for frame {}",
        grayscale.size(), frame_id);
    return grayscale;
  } catch (const std::exception& e) {
    ORC_LOG_ERROR("Error getting frame data: {}", e.what());
    return {};
  }
}

std::vector<DropoutRegion> DropoutPresenter::getSourceDropouts(
    const std::shared_ptr<void>& vfr_handle, FrameID frame_id) {
  auto artifact_base =
      std::static_pointer_cast<const orc::Artifact>(vfr_handle);
  auto vfr = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
      artifact_base);
  if (!vfr) {
    return {};
  }

  try {
    if (!vfr->has_frame(frame_id)) {
      return {};
    }

    auto desc = vfr->get_frame_descriptor(frame_id);
    if (!desc) {
      return {};
    }

    auto params = vfr->get_video_parameters();
    if (!params) {
      return {};
    }

    // Get frame-flat dropout hints and convert to frame-flat DropoutRegions.
    // Coordinates match DropoutEntrySpec: line = frame-flat 0-based line index,
    // start_sample/end_sample = sample within that line.
    auto runs = vfr->get_dropout_hints(frame_id);
    std::vector<DropoutRegion> regions;
    regions.reserve(runs.size());

    for (const auto& run : runs) {
      auto [flat_line, start_sample] = frame_flat_offset_to_line_sample(
          params->system, static_cast<size_t>(desc->samples_per_line_nominal),
          run.sample_start);

      uint32_t end_sample =
          static_cast<uint32_t>(start_sample) + run.sample_count;
      uint32_t spl = static_cast<uint32_t>(desc->samples_per_line_nominal);
      if (end_sample > spl) {
        end_sample = spl;
      }

      DropoutRegion region;
      region.line = static_cast<uint32_t>(flat_line);
      region.start_sample = static_cast<uint32_t>(start_sample);
      region.end_sample = end_sample;
      region.basis = DropoutRegion::DetectionBasis::HINT_DERIVED;
      regions.push_back(region);
    }

    return regions;
  } catch (const std::exception& e) {
    ORC_LOG_ERROR("Error getting source dropouts: {}", e.what());
    return {};
  }
}

std::map<uint64_t, FrameDropoutMap> DropoutPresenter::getDropoutMap(
    NodeID node_id) {
  try {
    const auto& nodes = impl_->project_presenter_.getNodes();
    auto node_it = std::find_if(nodes.begin(), nodes.end(),
                                [&node_id](const orc::presenters::NodeInfo& n) {
                                  return n.node_id == node_id;
                                });

    if (node_it == nodes.end()) {
      ORC_LOG_WARN("Node {} not found in project", node_id.to_string());
      return {};
    }

    if (node_it->stage_name != "dropout_map") {
      ORC_LOG_WARN("Node {} is not a dropout_map stage (it's {})",
                   node_id.to_string(), node_it->stage_name);
      return {};
    }

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
    NodeID node_id, const std::map<uint64_t, FrameDropoutMap>& dropout_map) {
  try {
    std::string map_str = encode_dropout_map_string(dropout_map);

    std::map<std::string, orc::ParameterValue> params;
    params["dropout_map"] = map_str;

    impl_->project_presenter_.setNodeParameters(node_id, params);

    return true;
  } catch (const std::exception& e) {
    ORC_LOG_ERROR("Error setting dropout map: {}", e.what());
    return false;
  }
}

size_t DropoutPresenter::getFrameCount(
    const std::shared_ptr<void>& vfr_handle) {
  auto artifact_base =
      std::static_pointer_cast<const orc::Artifact>(vfr_handle);
  auto vfr = std::dynamic_pointer_cast<const orc::VideoFrameRepresentation>(
      artifact_base);
  if (!vfr) {
    return 0;
  }
  return vfr->frame_count();
}

}  // namespace orc::presenters
