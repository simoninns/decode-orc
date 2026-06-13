/*
 * File:        dropout_map_stage.cpp
 * Module:      orc-core
 * Purpose:     Dropout map stage implementation (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dropout_map_stage.h"

#include <algorithm>
#include <cctype>
#include <sstream>

#include "cvbs_signal_constants.h"
#include "error_types.h"
#include "logging.h"

namespace orc {

// ============================================================================
// Frame-flat sample offset helpers
// ============================================================================

namespace {

// Width of a single frame-flat line (accounts for PAL non-uniform lines).
static uint64_t frame_line_width(VideoSystem sys, size_t line,
                                 int32_t nominal_spl) {
  if (sys == VideoSystem::PAL) {
    for (int i = 0; i < 4; ++i) {
      if (line == static_cast<size_t>(kPalExtraSampleLines[i])) {
        return static_cast<uint64_t>(nominal_spl) + 1;
      }
    }
  }
  return static_cast<uint64_t>(nominal_spl);
}

// Frame-flat sample offset of (line, sample_within_line).
static uint64_t frame_flat_offset(VideoSystem sys, int32_t nominal_spl,
                                  size_t line, uint32_t sample) {
  uint64_t off = 0;
  for (size_t l = 0; l < line; ++l) {
    off += frame_line_width(sys, l, nominal_spl);
  }
  return off + sample;
}

PreviewImage render_vfr_grayscale(const VideoFrameRepresentation& vfr,
                                  FrameID fid, bool scale) {
  auto desc = vfr.get_frame_descriptor(fid);
  auto params = vfr.get_video_parameters();
  if (!desc || !params) return PreviewImage{0, 0, {}, {}, {}};
  const size_t H = desc->height;
  const size_t W = static_cast<size_t>(params->frame_width_nominal);
  const int32_t b = params->blanking_level;
  const int32_t w = params->white_level;
  const int32_t range = (w > b) ? (w - b) : 1;
  PreviewImage img;
  img.width = static_cast<uint32_t>(W);
  img.height = static_cast<uint32_t>(H);
  img.rgb_data.reserve(W * H * 3);
  for (size_t line = 0; line < H; ++line) {
    const int16_t* ptr = vfr.get_line(fid, line);
    for (size_t s = 0; s < W; ++s) {
      const int32_t raw = ptr ? static_cast<int32_t>(ptr[s]) : b;
      const uint8_t grey =
          scale
              ? static_cast<uint8_t>(std::clamp((raw - b) * 255 / range, 0, 255))
              : static_cast<uint8_t>(std::clamp(raw * 255 / 1023, 0, 255));
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
    }
  }
  return img;
}

}  // namespace

// ============================================================================
// DropoutMappedFrameRepresentation
// ============================================================================

DropoutMappedFrameRepresentation::DropoutMappedFrameRepresentation(
    std::shared_ptr<const VideoFrameRepresentation> source,
    const std::map<uint64_t, FrameDropoutMapEntry>& dropout_map)
    : VideoFrameRepresentationWrapper(std::move(source)),
      Artifact(ArtifactID("dropout_map"), Provenance{}),
      dropout_map_(dropout_map) {}

std::optional<DropoutRun> DropoutMappedFrameRepresentation::entry_to_run(
    VideoSystem sys, int32_t nominal_spl, FrameID frame_id,
    const DropoutEntrySpec& entry) {
  if (entry.end_sample < entry.start_sample) return std::nullopt;
  const uint64_t start =
      frame_flat_offset(sys, nominal_spl, entry.line, entry.start_sample);
  const uint64_t count =
      static_cast<uint64_t>(entry.end_sample - entry.start_sample) + 1;
  return DropoutRun{frame_id, start, static_cast<uint32_t>(count),
                    uint8_t{128}};
}

std::vector<DropoutRun> DropoutMappedFrameRepresentation::get_dropout_hints(
    FrameID id) const {
  std::vector<DropoutRun> result;
  if (source_) result = source_->get_dropout_hints(id);

  auto it = dropout_map_.find(id);
  if (it == dropout_map_.end()) return result;

  const FrameDropoutMapEntry& entry = it->second;

  // Determine VideoSystem and nominal samples per line for coordinate conversion.
  VideoSystem sys = VideoSystem::PAL;
  int32_t nominal_spl = kPalMaxSamplesPerLine - 1;  // 1135 for PAL
  if (source_) {
    auto params = source_->get_video_parameters();
    if (params) {
      sys = params->system;
      nominal_spl = params->frame_width_nominal;
    }
  }

  ORC_LOG_DEBUG(
      "DropoutMappedFrameRepresentation::get_dropout_hints - frame {} has "
      "{} source hints, {} additions, {} removals",
      id, result.size(), entry.additions.size(), entry.removals.size());

  // Apply removals: remove any source run whose range overlaps a removal spec.
  for (const auto& rem : entry.removals) {
    const uint64_t rem_start =
        frame_flat_offset(sys, nominal_spl, rem.line, rem.start_sample);
    const uint64_t rem_end =
        frame_flat_offset(sys, nominal_spl, rem.line, rem.end_sample);

    result.erase(
        std::remove_if(result.begin(), result.end(),
                       [&](const DropoutRun& run) {
                         if (run.frame_id != id) return false;
                         const uint64_t run_end =
                             run.sample_start + run.sample_count - 1;
                         return !(run_end < rem_start ||
                                  run.sample_start > rem_end);
                       }),
        result.end());
  }

  // Apply additions.
  for (const auto& add : entry.additions) {
    auto run = entry_to_run(sys, nominal_spl, id, add);
    if (run) result.push_back(*run);
  }

  // Sort by sample_start for consistency.
  std::sort(result.begin(), result.end(),
            [](const DropoutRun& a, const DropoutRun& b) {
              return a.sample_start < b.sample_start;
            });

  return result;
}

// ============================================================================
// DropoutMapStage
// ============================================================================

std::vector<ArtifactPtr> DropoutMapStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  (void)observation_context;
  if (inputs.size() != 1) {
    throw DAGExecutionError(
        "DropoutMapStage requires exactly one input");
  }

  auto source =
      std::dynamic_pointer_cast<const VideoFrameRepresentation>(inputs[0]);
  if (!source) {
    throw DAGExecutionError(
        "DropoutMapStage input must be VideoFrameRepresentation");
  }

  if (!parameters.empty()) set_parameters(parameters);

  std::map<uint64_t, FrameDropoutMapEntry> dropout_map =
      parse_dropout_map(dropout_map_str_);
  ORC_LOG_DEBUG("DropoutMapStage: {} frame mappings loaded", dropout_map.size());

  auto mapped = std::make_shared<DropoutMappedFrameRepresentation>(
      source, dropout_map);
  cached_output_ = mapped;

  std::vector<ArtifactPtr> out;
  out.push_back(
      std::const_pointer_cast<DropoutMappedFrameRepresentation>(
          std::dynamic_pointer_cast<const DropoutMappedFrameRepresentation>(
              mapped)));
  return out;
}

// ============================================================================
// Parameter Interface
// ============================================================================

std::vector<ParameterDescriptor> DropoutMapStage::get_parameter_descriptors(
    VideoSystem, SourceType) const {
  ParameterDescriptor desc;
  desc.name = "dropout_map";
  desc.display_name = "Dropout Map";
  desc.description =
      "Per-frame dropout overrides: "
      "[{field:N,add:[{line:L,start:S,end:E}],remove:[...]}] "
      "(field = frame_id, line/start/end are frame-flat 0-based)";
  desc.type = ParameterType::STRING;
  desc.constraints.default_value = std::string("[]");
  desc.constraints.required = false;
  return {desc};
}

std::map<std::string, ParameterValue> DropoutMapStage::get_parameters() const {
  return {{"dropout_map", ParameterValue{dropout_map_str_}}};
}

bool DropoutMapStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  if (params.count("dropout_map")) {
    const auto* v = std::get_if<std::string>(&params.at("dropout_map"));
    if (v) dropout_map_str_ = *v;
  }
  return true;
}

// ============================================================================
// Preview
// ============================================================================

std::vector<PreviewOption> DropoutMapStage::get_preview_options() const {
  if (!cached_output_) return {};
  auto params = cached_output_->get_video_parameters();
  const size_t fc = cached_output_->frame_count();
  if (fc == 0 || !params) return {};
  const uint32_t w = static_cast<uint32_t>(params->frame_width_nominal);
  const uint32_t h = static_cast<uint32_t>(params->frame_height);
  return {PreviewOption{"frame", "Frame (Scaled)", false, w, h,
                        static_cast<uint64_t>(fc), 0.7},
          PreviewOption{"frame_raw", "Frame (Raw)", false, w, h,
                        static_cast<uint64_t>(fc), 0.7}};
}

PreviewImage DropoutMapStage::render_preview(const std::string& option_id,
                                             uint64_t index,
                                             PreviewNavigationHint) const {
  if (!cached_output_) return PreviewImage{};
  const FrameID fid = static_cast<FrameID>(index);
  if (!cached_output_->has_frame(fid)) return PreviewImage{};
  return render_vfr_grayscale(*cached_output_, fid, option_id != "frame_raw");
}

// ============================================================================
// Parse/Encode helpers (public for GUI dropout editor)
// ============================================================================

std::map<uint64_t, FrameDropoutMapEntry> DropoutMapStage::parse_dropout_map(
    const std::string& map_str) {
  std::map<uint64_t, FrameDropoutMapEntry> result;

  if (map_str.empty() || map_str == "[]") return result;

  size_t pos = 0;

  auto skip_ws = [&]() {
    while (pos < map_str.size() && std::isspace(map_str[pos])) {
      pos++;
    }
  };
  auto expect = [&](char c) -> bool {
    skip_ws();
    if (pos < map_str.size() && map_str[pos] == c) { pos++; return true; }
    return false;
  };
  auto parse_uint = [&]() -> uint32_t {
    skip_ws();
    uint32_t v = 0;
    while (pos < map_str.size() && std::isdigit(map_str[pos])) {
      v = v * 10 + static_cast<uint32_t>(map_str[pos++] - '0');
    }
    return v;
  };

  auto parse_entry_spec = [&]() -> DropoutEntrySpec {
    DropoutEntrySpec e{};
    if (!expect('{')) return e;
    while (pos < map_str.size() && map_str[pos] != '}') {
      skip_ws();
      std::string key;
      while (pos < map_str.size() && std::isalpha(map_str[pos])) {
        key += map_str[pos++];
      }
      if (!expect(':')) { break; }
      if (key == "line") {
        e.line = parse_uint();
      } else if (key == "start") {
        e.start_sample = parse_uint();
      } else if (key == "end") {
        e.end_sample = parse_uint();
      }
      expect(',');
    }
    expect('}');
    return e;
  };

  auto parse_entry_list = [&]() -> std::vector<DropoutEntrySpec> {
    std::vector<DropoutEntrySpec> list;
    if (!expect('[')) return list;
    while (pos < map_str.size() && map_str[pos] != ']') {
      skip_ws();
      if (map_str[pos] == '{') { list.push_back(parse_entry_spec()); }
      expect(',');
      skip_ws();
    }
    expect(']');
    return list;
  };

  if (!expect('[')) {
    ORC_LOG_ERROR("DropoutMapStage: map must start with '['");
    return result;
  }

  while (pos < map_str.size() && map_str[pos] != ']') {
    skip_ws();
    if (!expect('{')) break;

    FrameDropoutMapEntry entry;
    while (pos < map_str.size() && map_str[pos] != '}') {
      skip_ws();
      std::string key;
      while (pos < map_str.size() && std::isalpha(map_str[pos])) {
        key += map_str[pos++];
      }
      if (!expect(':')) { break; }
      if (key == "field") {
        entry.frame_id = static_cast<uint64_t>(parse_uint());
      } else if (key == "add") {
        entry.additions = parse_entry_list();
      } else if (key == "remove") {
        entry.removals = parse_entry_list();
      }
      expect(',');
    }
    expect('}');
    result[entry.frame_id] = entry;
    expect(',');
  }

  return result;
}

std::string DropoutMapStage::encode_dropout_map(
    const std::map<uint64_t, FrameDropoutMapEntry>& map) {
  if (map.empty()) return "[]";

  std::ostringstream oss;
  oss << "[";
  bool first = true;
  for (const auto& [frame_id, entry] : map) {
    if (!first) oss << ",";
    first = false;
    oss << "{field:" << frame_id;
    if (!entry.additions.empty()) {
      oss << ",add:[";
      bool fa = true;
      for (const auto& a : entry.additions) {
        if (!fa) oss << ",";
        fa = false;
        oss << "{line:" << a.line << ",start:" << a.start_sample
            << ",end:" << a.end_sample << "}";
      }
      oss << "]";
    }
    if (!entry.removals.empty()) {
      oss << ",remove:[";
      bool fr = true;
      for (const auto& r : entry.removals) {
        if (!fr) oss << ",";
        fr = false;
        oss << "{line:" << r.line << ",start:" << r.start_sample
            << ",end:" << r.end_sample << "}";
      }
      oss << "]";
    }
    oss << "}";
  }
  oss << "]";
  return oss.str();
}

}  // namespace orc
