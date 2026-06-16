/*
 * File:        source_align_stage.cpp
 * Module:      orc-core
 * Purpose:     Source alignment stage implementation (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "source_align_stage.h"

#include <algorithm>
#include <limits>
#include <sstream>

#include "artifact.h"
#include "logging.h"
#include "preview_helpers.h"

namespace orc {

// ============================================================================
// AlignedSourceFrameRepresentation
// ============================================================================
// Wraps a VFrameR and remaps frame IDs by adding a fixed offset so that
// output frame_id 0 maps to source frame_id `offset_`.
class AlignedSourceFrameRepresentation : public VideoFrameRepresentationWrapper,
                                         public Artifact {
 public:
  AlignedSourceFrameRepresentation(
      std::shared_ptr<const VideoFrameRepresentation> source, FrameID offset,
      size_t source_index)
      : VideoFrameRepresentationWrapper(std::move(source)),
        Artifact(ArtifactID("aligned_source_" + std::to_string(source_index) +
                            "_offset_" + std::to_string(offset)),
                 Provenance{}),
        offset_(offset) {}

  std::string type_name() const override {
    return "aligned_source_frame_representation";
  }

  FrameIDRange frame_range() const override {
    if (!source_) {
      return FrameIDRange{};
    }
    const auto src = source_->frame_range();
    if (offset_ >= src.count()) {
      return FrameIDRange{};
    }
    const size_t new_size = static_cast<size_t>(src.count() - offset_);
    return FrameIDRange{FrameID{0}, FrameID{new_size - 1}};
  }

  size_t frame_count() const override {
    if (!source_) {
      return 0;
    }
    const auto src = source_->frame_range();
    if (offset_ >= src.count()) {
      return 0;
    }
    return static_cast<size_t>(src.count() - offset_);
  }

  bool has_frame(FrameID id) const override {
    return source_ ? source_->has_frame(id + offset_) : false;
  }

  std::optional<FrameDescriptor> get_frame_descriptor(
      FrameID id) const override {
    if (!source_) {
      return std::nullopt;
    }
    auto desc = source_->get_frame_descriptor(id + offset_);
    if (desc) {
      desc->frame_id = id;
    }
    return desc;
  }

  const sample_type* get_frame(FrameID id) const override {
    return source_ ? source_->get_frame(id + offset_) : nullptr;
  }

  const sample_type* get_line(FrameID id, size_t line) const override {
    return source_ ? source_->get_line(id + offset_, line) : nullptr;
  }

  std::vector<sample_type> get_frame_copy(FrameID id) const override {
    return source_ ? source_->get_frame_copy(id + offset_)
                   : std::vector<sample_type>{};
  }

  bool has_separate_channels() const override {
    return source_ ? source_->has_separate_channels() : false;
  }

  const sample_type* get_line_luma(FrameID id, size_t line) const override {
    return source_ ? source_->get_line_luma(id + offset_, line) : nullptr;
  }

  const sample_type* get_line_chroma(FrameID id, size_t line) const override {
    return source_ ? source_->get_line_chroma(id + offset_, line) : nullptr;
  }

  std::vector<DropoutRun> get_dropout_hints(FrameID id) const override {
    if (!source_) {
      return {};
    }
    auto runs = source_->get_dropout_hints(id + offset_);
    for (auto& run : runs) {
      run.frame_id = id;
    }
    return runs;
  }

  std::optional<VbiData> get_vbi_hint(FrameID id) const override {
    return source_ ? source_->get_vbi_hint(id + offset_) : std::nullopt;
  }

 private:
  FrameID offset_;
};

// ============================================================================
// SourceAlignStage
// ============================================================================

SourceAlignStage::SourceAlignStage() {
  set_configuration_status(orc::ConfigurationStatus::Red);
}

// ============================================================================
// SourceAlignStage helpers
// ============================================================================

std::vector<std::pair<size_t, size_t>> SourceAlignStage::parse_alignment_map(
    const std::string& alignment_spec) {
  std::vector<std::pair<size_t, size_t>> result;
  if (alignment_spec.empty()) {
    return result;
  }

  std::istringstream iss(alignment_spec);
  std::string entry;
  while (std::getline(iss, entry, ',')) {
    entry.erase(0, entry.find_first_not_of(" \t"));
    const auto last = entry.find_last_not_of(" \t");
    if (last != std::string::npos) {
      entry.erase(last + 1);
    }
    if (entry.empty()) {
      continue;
    }

    const size_t plus = entry.find('+');
    if (plus == std::string::npos) {
      ORC_LOG_ERROR("Invalid alignment map entry (missing '+'): {}", entry);
      return {};
    }
    try {
      result.push_back({std::stoull(entry.substr(0, plus)),
                        std::stoull(entry.substr(plus + 1))});
    } catch (...) {
      ORC_LOG_ERROR("Invalid alignment map entry (parse error): {}", entry);
      return {};
    }
  }
  return result;
}

int32_t SourceAlignStage::get_frame_number_from_vbi(
    const VideoFrameRepresentation& source, FrameID frame_id) const {
  (void)source;
  (void)frame_id;
  return -1;
}

std::vector<FrameID> SourceAlignStage::find_alignment_offsets(
    const std::vector<std::shared_ptr<const VideoFrameRepresentation>>& sources)
    const {
  if (sources.empty()) {
    return {};
  }
  if (sources.size() == 1) {
    return {FrameID{0}};
  }

  ORC_LOG_DEBUG("SourceAlignStage: Finding alignment for {} sources",
                sources.size());

  struct FrameLocation {
    FrameID frame_id;
    size_t source_index;
  };

  std::map<int32_t, std::vector<FrameLocation>> frame_map;

  for (size_t src_idx = 0; src_idx < sources.size(); ++src_idx) {
    const auto& src = sources[src_idx];
    if (!src) {
      continue;
    }

    const auto range = src->frame_range();
    ORC_LOG_DEBUG("  Source {}: {} frames (range {}-{})", src_idx,
                  src->frame_count(), range.first, range.last);

    size_t frames_with_vbi = 0;
    for (uint64_t i = 0; i < range.count(); ++i) {
      const FrameID fid = range.first + i;
      if (!src->has_frame(fid)) {
        continue;
      }

      const int32_t frame_num = get_frame_number_from_vbi(*src, fid);
      if (frame_num >= 0) {
        frame_map[frame_num].push_back({fid, src_idx});
        frames_with_vbi++;
      }
    }
    ORC_LOG_DEBUG("    Found VBI data in {} frames", frames_with_vbi);
  }

  std::vector<FrameID> offsets(sources.size(), FrameID{0});

  for (const auto& [frame_num, locations] : frame_map) {
    std::vector<bool> present(sources.size(), false);
    for (const auto& loc : locations) {
      present[loc.source_index] = true;
    }

    if (std::all_of(present.begin(), present.end(), [](bool p) { return p; })) {
      for (const auto& loc : locations) {
        offsets[loc.source_index] = loc.frame_id;
      }
      ORC_LOG_INFO("  First common VBI frame: #{}", frame_num);
      for (size_t i = 0; i < sources.size(); ++i) {
        ORC_LOG_INFO("    Source {}: starts at frame_id {}", i, offsets[i]);
      }
      return offsets;
    }
  }

  ORC_LOG_WARN(
      "SourceAlignStage: No common frame found - defaulting to zero offsets");
  return offsets;
}

// ============================================================================
// execute()
// ============================================================================

std::vector<ArtifactPtr> SourceAlignStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  (void)observation_context;
  if (inputs.empty()) {
    throw DAGExecutionError("SourceAlignStage requires at least 1 input");
  }
  if (inputs.size() > 16) {
    throw DAGExecutionError("SourceAlignStage supports maximum 16 inputs");
  }

  ORC_LOG_DEBUG("SourceAlignStage: Processing {} input(s)", inputs.size());

  std::vector<std::shared_ptr<const VideoFrameRepresentation>> sources;
  for (const auto& input : inputs) {
    auto src = std::dynamic_pointer_cast<const VideoFrameRepresentation>(input);
    if (!src) {
      throw DAGExecutionError(
          "SourceAlignStage input must be VideoFrameRepresentation");
    }
    sources.push_back(src);
  }

  if (!parameters.empty()) {
    set_parameters(parameters);
  }

  input_sources_ = sources;

  std::vector<FrameID> offsets;

  if (alignment_map_.empty()) {
    throw DAGExecutionError(
        "Alignment map is not configured. Use the Source Alignment tool to "
        "generate the map, or set the alignmentMap parameter manually.");
  }

  const auto entries = parse_alignment_map(alignment_map_);
  if (entries.empty()) {
    throw DAGExecutionError("Invalid alignment map: " + alignment_map_);
  }

  offsets.assign(sources.size(), std::numeric_limits<FrameID>::max());
  for (const auto& [input_id, offset_val] : entries) {
    if (input_id < 1 || input_id > sources.size()) {
      throw DAGExecutionError("Alignment map references invalid input ID: " +
                              std::to_string(input_id));
    }
    offsets[input_id - 1] = static_cast<FrameID>(offset_val);
  }
  ORC_LOG_DEBUG("Using alignment map: {}", alignment_map_);

  alignment_offsets_ = offsets;

  std::vector<ArtifactPtr> outputs;
  cached_outputs_.clear();

  const FrameID kExcluded = std::numeric_limits<FrameID>::max();

  for (size_t i = 0; i < sources.size(); ++i) {
    if (offsets[i] == kExcluded) {
      cached_outputs_.push_back(nullptr);
      ORC_LOG_DEBUG("  Source {}: EXCLUDED from output", i);
      continue;
    }

    if (offsets[i] == 0) {
      outputs.push_back(inputs[i]);
      cached_outputs_.push_back(sources[i]);
      ORC_LOG_DEBUG("  Source {}: pass-through (offset=0)", i);
    } else {
      auto aligned = std::make_shared<AlignedSourceFrameRepresentation>(
          sources[i], offsets[i], i);
      outputs.push_back(aligned);
      cached_outputs_.push_back(aligned);
      ORC_LOG_DEBUG("  Source {}: offset by {} frames, {} remaining", i,
                    offsets[i], aligned->frame_count());
    }
  }

  return outputs;
}

// ============================================================================
// Parameters
// ============================================================================

std::vector<ParameterDescriptor> SourceAlignStage::get_parameter_descriptors(
    VideoSystem, SourceType) const {
  return {ParameterDescriptor{
      "alignmentMap", "Alignment Map",
      "Alignment map ('1+2, 2+2, 3+1'). Format: input_id+frame_offset per "
      "input. Use the Source Alignment tool to generate this value.",
      ParameterType::STRING,
      ParameterConstraints{std::nullopt,
                           std::nullopt,
                           ParameterValue{std::string("")},
                           {},
                           false,
                           std::nullopt}}};
}

std::map<std::string, ParameterValue> SourceAlignStage::get_parameters() const {
  return {{"alignmentMap", ParameterValue{alignment_map_}}};
}

bool SourceAlignStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  for (const auto& [key, value] : params) {
    if (key == "alignmentMap") {
      if (const auto* s = std::get_if<std::string>(&value)) {
        alignment_map_ = *s;
      } else {
        return false;
      }
    } else {
      return false;
    }
  }
  set_configuration_status(alignment_map_.empty()
                               ? orc::ConfigurationStatus::Red
                               : orc::ConfigurationStatus::Green);
  return true;
}

// ============================================================================
// Report
// ============================================================================

std::optional<StageReport> SourceAlignStage::generate_report() const {
  StageReport report;
  report.summary = "Source Alignment Report";

  if (input_sources_.empty() || alignment_offsets_.empty()) {
    report.items.push_back({"Status", "Not yet executed"});
    return report;
  }

  const FrameID kExcluded = std::numeric_limits<FrameID>::max();

  for (size_t i = 0; i < input_sources_.size(); ++i) {
    const auto& src = input_sources_[i];
    if (!src) {
      continue;
    }

    const std::string lbl = "Source " + std::to_string(i);
    const FrameID offset = alignment_offsets_[i];

    if (offset == kExcluded) {
      report.items.push_back({lbl + " Status", "EXCLUDED"});
    } else {
      const size_t total = src->frame_count();
      report.items.push_back({lbl + " Status", "INCLUDED"});
      report.items.push_back({lbl + " Total Frames", std::to_string(total)});
      report.items.push_back(
          {lbl + " Alignment Offset", std::to_string(offset)});
      report.items.push_back({lbl + " Dropped Frames", std::to_string(offset)});
      report.items.push_back(
          {lbl + " Output Frames",
           std::to_string(total > offset ? total - offset : 0)});
    }
    if (i + 1 < input_sources_.size()) {
      report.items.push_back({"", ""});
    }
  }

  size_t total_dropped = 0;
  size_t excluded = 0;
  for (const auto& off : alignment_offsets_) {
    if (off == kExcluded) {
      excluded++;
    } else {
      total_dropped += off;
    }
  }
  report.metrics["source_count"] = static_cast<int64_t>(input_sources_.size());
  report.metrics["total_dropped_frames"] = static_cast<int64_t>(total_dropped);
  report.metrics["excluded_sources"] = static_cast<int64_t>(excluded);
  report.metrics["included_sources"] =
      static_cast<int64_t>(input_sources_.size() - excluded);

  return report;
}

// ============================================================================
// Preview
// ============================================================================

namespace {

PreviewImage render_vfr_grayscale(const VideoFrameRepresentation& vfr,
                                  FrameID fid, bool scale) {
  auto desc = vfr.get_frame_descriptor(fid);
  auto params = vfr.get_video_parameters();
  if (!desc || !params) {
    return PreviewImage{0, 0, {}, {}, {}};
  }
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
          scale ? static_cast<uint8_t>(
                      std::clamp((raw - b) * 255 / range, 0, 255))
                : static_cast<uint8_t>(std::clamp(raw * 255 / 1023, 0, 255));
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
    }
  }
  return img;
}

}  // namespace

StagePreviewCapability SourceAlignStage::get_preview_capability() const {
  for (const auto& out : cached_outputs_) {
    if (out && out->frame_count() > 0) {
      return PreviewHelpers::make_signal_preview_capability(out);
    }
  }
  return {};
}

std::vector<PreviewOption> SourceAlignStage::get_preview_options() const {
  std::vector<PreviewOption> options;
  for (size_t i = 0; i < cached_outputs_.size(); ++i) {
    const auto& out = cached_outputs_[i];
    if (!out || out->frame_count() == 0) {
      continue;
    }
    const auto params = out->get_video_parameters();
    const uint32_t W =
        params ? static_cast<uint32_t>(params->frame_width_nominal) : 910;
    const uint32_t H =
        params ? static_cast<uint32_t>(params->frame_height) : 525;
    options.push_back({"source_" + std::to_string(i),
                       "Aligned Source " + std::to_string(i), false, W, H,
                       static_cast<uint64_t>(out->frame_count()), 0.75});
  }
  return options;
}

PreviewImage SourceAlignStage::render_preview(const std::string& option_id,
                                              uint64_t index,
                                              PreviewNavigationHint) const {
  const std::string prefix = "source_";
  size_t src_idx = 0;
  if (option_id.compare(0, prefix.size(), prefix) == 0) {
    try {
      src_idx = std::stoull(option_id.substr(prefix.size()));
    } catch (...) {
      throw std::runtime_error("Invalid preview option_id: " + option_id);
    }
  }
  if (src_idx >= cached_outputs_.size() || !cached_outputs_[src_idx]) {
    throw std::runtime_error("No cached output for preview");
  }

  const auto& out = cached_outputs_[src_idx];
  const FrameID fid = static_cast<FrameID>(index);
  if (!out->has_frame(fid)) {
    return PreviewImage{};
  }
  return render_vfr_grayscale(*out, fid, true);
}

}  // namespace orc
