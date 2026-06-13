/*
 * File:        dropout_correct_stage.cpp
 * Module:      orc-core
 * Purpose:     Dropout correction stage implementation (VFrameR)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dropout_correct_stage.h"

#include <algorithm>
#include <cmath>

#include "cvbs_signal_constants.h"
#include "error_types.h"
#include "logging.h"

namespace orc {

// ============================================================================
// Helper: determine first-field line count for a VideoSystem
// ============================================================================

namespace {

size_t field1_lines_for_system(VideoSystem sys) {
  switch (sys) {
    case VideoSystem::PAL:
      return static_cast<size_t>(kPalField1Lines);
    case VideoSystem::NTSC:
      return static_cast<size_t>(kNtscField1Lines);
    case VideoSystem::PAL_M:
      return static_cast<size_t>(kPalMField1Lines);
    default:
      return static_cast<size_t>(kNtscField1Lines);
  }
}

bool is_pal(VideoSystem sys) { return sys == VideoSystem::PAL; }

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
// DropoutCorrectStage::runs_to_line_dropouts
// ============================================================================

std::vector<LineDropout> DropoutCorrectStage::runs_to_line_dropouts(
    const std::vector<DropoutRun>& runs, size_t nominal_spl) {
  if (nominal_spl == 0) {
    return {};
  }
  std::vector<LineDropout> result;
  result.reserve(runs.size());
  for (const auto& run : runs) {
    uint64_t remaining = run.sample_count;
    uint64_t offset = run.sample_start;
    while (remaining > 0) {
      const uint32_t line = static_cast<uint32_t>(offset / nominal_spl);
      const uint32_t start = static_cast<uint32_t>(offset % nominal_spl);
      const uint32_t avail = static_cast<uint32_t>(nominal_spl) - start;
      const uint32_t count =
          static_cast<uint32_t>(std::min(remaining, static_cast<uint64_t>(avail)));
      result.push_back({line, start, static_cast<uint32_t>(start + count - 1)});
      offset += count;
      remaining -= count;
    }
  }
  return result;
}

// ============================================================================
// execute()
// ============================================================================

std::vector<ArtifactPtr> DropoutCorrectStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  (void)observation_context;
  if (inputs.empty()) {
    throw DAGExecutionError("DropoutCorrectStage requires one input");
  }

  auto source =
      std::dynamic_pointer_cast<const VideoFrameRepresentation>(inputs[0]);
  if (!source) {
    throw DAGExecutionError(
        "DropoutCorrectStage input must be VideoFrameRepresentation");
  }

  if (!parameters.empty()) {
    set_parameters(parameters);
  }

  auto corrected = std::make_shared<CorrectedVideoFrameRepresentation>(
      source, this, config_.highlight_corrections);

  cached_output_ = corrected;

  std::vector<ArtifactPtr> outputs;
  outputs.push_back(corrected);
  return outputs;
}

// ============================================================================
// CorrectedVideoFrameRepresentation
// ============================================================================

CorrectedVideoFrameRepresentation::CorrectedVideoFrameRepresentation(
    std::shared_ptr<const VideoFrameRepresentation> source,
    DropoutCorrectStage* stage, bool highlight_corrections)
    : VideoFrameRepresentationWrapper(std::move(source)),
      Artifact(ArtifactID("corrected_frame"), Provenance{}),
      stage_(stage),
      highlight_corrections_(highlight_corrections),
      corrected_frames_(MAX_CACHED_FRAMES),
      corrected_luma_frames_(MAX_CACHED_FRAMES),
      corrected_chroma_frames_(MAX_CACHED_FRAMES) {}

void CorrectedVideoFrameRepresentation::ensure_frame_corrected(
    FrameID frame_id) const {
  if (source_->has_separate_channels()) {
    if (corrected_luma_frames_.contains(frame_id) &&
        corrected_chroma_frames_.contains(frame_id)) {
      return;
    }
  } else {
    if (corrected_frames_.contains(frame_id)) {
      return;
    }
  }
  stage_->correct_single_frame(
      const_cast<CorrectedVideoFrameRepresentation*>(this), source_, frame_id);
}

const int16_t* CorrectedVideoFrameRepresentation::get_line(FrameID id,
                                                           size_t line) const {
  ensure_frame_corrected(id);
  const auto* cached = corrected_frames_.get_ptr(id);
  if (cached && !cached->empty()) {
    auto desc = source_->get_frame_descriptor(id);
    if (desc && line < desc->height) {
      const size_t spl = desc->samples_per_line_nominal;
      return &(*cached)[line * spl];
    }
  }
  return source_->get_line(id, line);
}

std::vector<int16_t> CorrectedVideoFrameRepresentation::get_frame_copy(
    FrameID id) const {
  auto desc = source_->get_frame_descriptor(id);
  if (!desc) {
    return {};
  }
  std::vector<int16_t> data;
  data.reserve(desc->samples_total);
  for (size_t line = 0; line < desc->height; ++line) {
    const int16_t* ptr = get_line(id, line);
    const size_t w = desc->samples_per_line_nominal;
    if (ptr) {
      data.insert(data.end(), ptr, ptr + w);
    } else {
      data.insert(data.end(), w, int16_t{0});
    }
  }
  return data;
}

const int16_t* CorrectedVideoFrameRepresentation::get_line_luma(
    FrameID id, size_t line) const {
  if (!source_ || !source_->has_separate_channels()) {
    return VideoFrameRepresentationWrapper::get_line_luma(id, line);
  }
  ensure_frame_corrected(id);
  const auto* cached = corrected_luma_frames_.get_ptr(id);
  if (cached && !cached->empty()) {
    auto desc = source_->get_frame_descriptor(id);
    if (desc && line < desc->height) {
      return &(*cached)[line * desc->samples_per_line_nominal];
    }
  }
  return source_->get_line_luma(id, line);
}

const int16_t* CorrectedVideoFrameRepresentation::get_line_chroma(
    FrameID id, size_t line) const {
  if (!source_ || !source_->has_separate_channels()) {
    return VideoFrameRepresentationWrapper::get_line_chroma(id, line);
  }
  ensure_frame_corrected(id);
  const auto* cached = corrected_chroma_frames_.get_ptr(id);
  if (cached && !cached->empty()) {
    auto desc = source_->get_frame_descriptor(id);
    if (desc && line < desc->height) {
      return &(*cached)[line * desc->samples_per_line_nominal];
    }
  }
  return source_->get_line_chroma(id, line);
}

// ============================================================================
// Dropout classification helpers
// ============================================================================

DropoutCorrectStage::DropoutLocation DropoutCorrectStage::classify_dropout(
    const LineDropout& dropout, const FrameDescriptor& /*desc*/,
    const std::optional<SourceParameters>& params) const {
  uint32_t cb_end = 100;
  uint32_t av_end = 800;

  if (params) {
    if (params->colour_burst_end >= 0) {
      cb_end = static_cast<uint32_t>(params->colour_burst_end);
    }
    if (params->active_video_end >= 0) {
      av_end = static_cast<uint32_t>(params->active_video_end);
    }
  }

  if (dropout.start_sample <= cb_end) {
    return DropoutLocation::COLOUR_BURST;
  }
  if (dropout.start_sample > cb_end && dropout.start_sample <= av_end) {
    return DropoutLocation::VISIBLE_LINE;
  }
  return DropoutLocation::UNKNOWN;
}

std::vector<LineDropout> DropoutCorrectStage::split_dropout_regions(
    const std::vector<LineDropout>& dropouts,
    const FrameDescriptor& desc,
    const std::optional<SourceParameters>& params) const {
  std::vector<LineDropout> result;

  uint32_t cb_end = 100;
  uint32_t av_end = 800;

  if (params) {
    if (params->colour_burst_end >= 0) {
      cb_end = static_cast<uint32_t>(params->colour_burst_end);
    }
    if (params->active_video_end >= 0) {
      av_end = static_cast<uint32_t>(params->active_video_end);
    }
  }

  for (const auto& d : dropouts) {
    if (d.line >= desc.height) {
      ORC_LOG_DEBUG("split_dropout_regions: skipping dropout on line {} "
                    "(frame height {})",
                    d.line, desc.height);
      continue;
    }
    const auto loc = classify_dropout(d, desc, params);
    if (loc == DropoutLocation::COLOUR_BURST && d.end_sample > cb_end) {
      result.push_back({d.line, d.start_sample, cb_end});
      result.push_back({d.line, cb_end + 1, d.end_sample});
    } else if (loc == DropoutLocation::VISIBLE_LINE && d.end_sample > av_end) {
      LineDropout truncated = d;
      truncated.end_sample = av_end;
      result.push_back(truncated);
    } else {
      result.push_back(d);
    }
  }
  return result;
}

// ============================================================================
// find_replacement_line
// ============================================================================

DropoutCorrectStage::ReplacementLine DropoutCorrectStage::find_replacement_line(
    const VideoFrameRepresentation& source, FrameID frame_id, uint32_t line,
    const LineDropout& dropout, bool intrafield,
    bool match_chroma_phase_override, size_t field1_lines,
    Channel channel) const {
  ReplacementLine best;

  const auto desc_opt = source.get_frame_descriptor(frame_id);
  if (!desc_opt) {
    return best;
  }
  const auto& desc = *desc_opt;

  const int32_t height = static_cast<int32_t>(desc.height);
  const size_t spl = desc.samples_per_line_nominal;

  const bool is_field1 = (line < field1_lines);

  uint32_t step = 1;
  int32_t other_field_off = 0;

  if (match_chroma_phase_override) {
    auto vp = source.get_video_parameters();
    if (vp) {
      if (is_pal(vp->system)) {
        step = 4;
        other_field_off = is_field1 ? -3 : -1;
      } else {
        step = 2;
        other_field_off = -1;
      }
    }
  }

  int32_t first_active = 0;
  int32_t last_active = height;
  const auto vp = source.get_video_parameters();
  if (vp && vp->first_active_frame_line >= 0 &&
      vp->last_active_frame_line >= 0) {
    first_active = vp->first_active_frame_line;
    last_active = vp->last_active_frame_line;
  }

  auto get_line_fn = [&source, frame_id, channel](size_t ln) -> const int16_t* {
    switch (channel) {
      case Channel::LUMA:
        return source.get_line_luma(frame_id, ln);
      case Channel::CHROMA:
        return source.get_line_chroma(frame_id, ln);
      default:
        return source.get_line(frame_id, ln);
    }
  };

  // Conservative: no overlap check (matches ld-dropout-correct behaviour)
  auto has_overlap = [&dropout](uint32_t /*chk_line*/) -> bool {
    (void)dropout;
    return false;
  };

  std::vector<ReplacementLine> candidates;

  if (intrafield) {
    const int32_t field_start =
        is_field1 ? 0 : static_cast<int32_t>(field1_lines);
    const int32_t field_end =
        is_field1 ? static_cast<int32_t>(field1_lines) : height;

    // Search upward
    int32_t sl = static_cast<int32_t>(line) - static_cast<int32_t>(step);
    while (sl >= std::max(first_active, field_start)) {
      const uint32_t cl = static_cast<uint32_t>(sl);
      if (!has_overlap(cl)) {
        const int16_t* data = get_line_fn(cl);
        if (data) {
          ReplacementLine c;
          c.found = true;
          c.source_frame = frame_id;
          c.source_line = cl;
          c.quality = calculate_line_quality(data, spl, dropout);
          c.distance =
              static_cast<uint32_t>(std::abs(static_cast<int32_t>(line) - sl));
          c.cached_data = data;
          candidates.push_back(c);
          break;
        }
      }
      sl -= static_cast<int32_t>(step);
    }

    // Search downward
    sl = static_cast<int32_t>(line) + static_cast<int32_t>(step);
    while (sl < std::min(last_active, field_end)) {
      const uint32_t cl = static_cast<uint32_t>(sl);
      if (!has_overlap(cl)) {
        const int16_t* data = get_line_fn(cl);
        if (data) {
          ReplacementLine c;
          c.found = true;
          c.source_frame = frame_id;
          c.source_line = cl;
          c.quality = calculate_line_quality(data, spl, dropout);
          c.distance =
              static_cast<uint32_t>(std::abs(static_cast<int32_t>(line) - sl));
          c.cached_data = data;
          candidates.push_back(c);
          break;
        }
      }
      sl += static_cast<int32_t>(step);
    }
  } else {
    const int32_t field_offset = is_field1
                                     ? static_cast<int32_t>(field1_lines)
                                     : -static_cast<int32_t>(field1_lines);
    const int32_t start_line =
        static_cast<int32_t>(line) + field_offset + other_field_off;

    const int32_t other_field_start =
        is_field1 ? static_cast<int32_t>(field1_lines) : 0;
    const int32_t other_field_end =
        is_field1 ? height : static_cast<int32_t>(field1_lines);

    if (start_line < other_field_start || start_line >= other_field_end) {
      return best;
    }

    // Search up in other field
    int32_t sl = start_line;
    while (sl >= std::max(first_active, other_field_start)) {
      const uint32_t cl = static_cast<uint32_t>(sl);
      if (!has_overlap(cl)) {
        const int16_t* data = get_line_fn(cl);
        if (data) {
          ReplacementLine c;
          c.found = true;
          c.source_frame = frame_id;
          c.source_line = cl;
          c.quality = calculate_line_quality(data, spl, dropout);
          c.distance =
              static_cast<uint32_t>(std::abs(static_cast<int32_t>(line) - sl));
          c.cached_data = data;
          candidates.push_back(c);
          break;
        }
      }
      sl -= static_cast<int32_t>(step);
    }

    // Search down in other field
    sl = start_line + static_cast<int32_t>(step);
    while (sl < std::min(last_active, other_field_end)) {
      const uint32_t cl = static_cast<uint32_t>(sl);
      if (!has_overlap(cl)) {
        const int16_t* data = get_line_fn(cl);
        if (data) {
          ReplacementLine c;
          c.found = true;
          c.source_frame = frame_id;
          c.source_line = cl;
          c.quality = calculate_line_quality(data, spl, dropout);
          c.distance =
              static_cast<uint32_t>(std::abs(static_cast<int32_t>(line) - sl));
          c.cached_data = data;
          candidates.push_back(c);
          break;
        }
      }
      sl += static_cast<int32_t>(step);
    }
  }

  for (const auto& c : candidates) {
    if (c.distance < best.distance ||
        (c.distance == best.distance && c.quality > best.quality)) {
      best = c;
    }
  }
  return best;
}

// ============================================================================
// apply_correction / calculate_line_quality
// ============================================================================

void DropoutCorrectStage::apply_correction(std::vector<int16_t>& line_data,
                                           const LineDropout& dropout,
                                           const int16_t* replacement_data,
                                           int16_t highlight_value,
                                           bool highlight) const {
  for (uint32_t s = dropout.start_sample;
       s <= dropout.end_sample &&
       s < static_cast<uint32_t>(line_data.size());
       ++s) {
    line_data[s] = highlight ? highlight_value : replacement_data[s];
  }
}

double DropoutCorrectStage::calculate_line_quality(
    const int16_t* line_data, size_t width,
    const LineDropout& dropout) const {
  if (dropout.start_sample >= dropout.end_sample ||
      dropout.end_sample >= width) {
    return 0.0;
  }
  double sum = 0.0;
  const uint32_t count = dropout.end_sample - dropout.start_sample;
  for (uint32_t i = dropout.start_sample; i < dropout.end_sample; ++i) {
    sum += line_data[i];
  }
  const double mean = sum / count;
  double mad = 0.0;
  for (uint32_t i = dropout.start_sample; i < dropout.end_sample; ++i) {
    mad += std::abs(static_cast<double>(line_data[i]) - mean);
  }
  return 1.0 / (mad / count + 1.0);
}

// ============================================================================
// correct_single_frame
// ============================================================================

void DropoutCorrectStage::correct_single_frame(
    CorrectedVideoFrameRepresentation* corrected,
    std::shared_ptr<const VideoFrameRepresentation> source,
    FrameID frame_id) const {
  ORC_LOG_DEBUG("DropoutCorrectStage::correct_single_frame - frame {}",
                frame_id);

  const auto desc_opt = source->get_frame_descriptor(frame_id);
  if (!desc_opt) {
    ORC_LOG_DEBUG("DropoutCorrectStage: no descriptor for frame {}", frame_id);
    return;
  }
  const auto& desc = *desc_opt;

  const size_t spl = desc.samples_per_line_nominal;
  const size_t height = desc.height;
  const size_t field1_lines = field1_lines_for_system(desc.system);

  auto runs = source->get_dropout_hints(frame_id);
  auto dropouts = runs_to_line_dropouts(runs, spl);

  ORC_LOG_DEBUG(
      "DropoutCorrectStage: frame {} has {} dropout hints ({} line-dropouts)",
      frame_id, runs.size(), dropouts.size());

  if (dropouts.empty()) {
    if (source->has_separate_channels()) {
      corrected->corrected_luma_frames_.put(frame_id, std::vector<int16_t>{});
      corrected->corrected_chroma_frames_.put(frame_id,
                                              std::vector<int16_t>{});
    } else {
      corrected->corrected_frames_.put(frame_id, std::vector<int16_t>{});
    }
    return;
  }

  const auto video_params = source->get_video_parameters();

  const int16_t highlight_val =
      video_params ? static_cast<int16_t>(video_params->white_level)
                   : int16_t{1023};

  if (config_.overcorrect_extension > 0) {
    for (auto& d : dropouts) {
      if (d.start_sample > config_.overcorrect_extension) {
        d.start_sample -= config_.overcorrect_extension;
      } else {
        d.start_sample = 0;
      }
      if (d.end_sample + config_.overcorrect_extension <
          static_cast<uint32_t>(spl)) {
        d.end_sample += config_.overcorrect_extension;
      } else {
        d.end_sample = static_cast<uint32_t>(spl - 1);
      }
    }
  }

  const auto split_dropouts =
      split_dropout_regions(dropouts, desc, video_params);

  // -----------------------------------------------------------------------
  // YC source path
  // -----------------------------------------------------------------------
  if (source->has_separate_channels()) {
    std::vector<int16_t> luma_data;
    std::vector<int16_t> chroma_data;
    luma_data.reserve(spl * height);
    chroma_data.reserve(spl * height);

    for (size_t line = 0; line < height; ++line) {
      const int16_t* ly = source->get_line_luma(frame_id, line);
      const int16_t* lc = source->get_line_chroma(frame_id, line);
      if (ly) {
        luma_data.insert(luma_data.end(), ly, ly + spl);
      } else {
        luma_data.insert(luma_data.end(), spl, int16_t{0});
      }
      if (lc) {
        chroma_data.insert(chroma_data.end(), lc, lc + spl);
      } else {
        chroma_data.insert(chroma_data.end(), spl, int16_t{0});
      }
    }

    size_t corrections = 0;
    for (const auto& d : split_dropouts) {
      if (d.line >= height) {
        continue;
      }
      int16_t* ly_ptr = &luma_data[d.line * spl];
      int16_t* lc_ptr = &chroma_data[d.line * spl];

      auto repl = find_replacement_line(*source, frame_id, d.line, d,
                                        config_.intrafield_only,
                                        config_.match_chroma_phase, field1_lines,
                                        Channel::LUMA);
      if (!repl.found && !config_.intrafield_only) {
        repl = find_replacement_line(*source, frame_id, d.line, d, false,
                                     config_.match_chroma_phase, field1_lines,
                                     Channel::LUMA);
      }

      if (repl.found) {
        const int16_t* ry = source->get_line_luma(frame_id, repl.source_line);
        const int16_t* rc =
            source->get_line_chroma(frame_id, repl.source_line);
        for (uint32_t s = d.start_sample; s <= d.end_sample && s < spl; ++s) {
          if (corrected->highlight_corrections_) {
            ly_ptr[s] = highlight_val;
            lc_ptr[s] = highlight_val;
          } else {
            ly_ptr[s] = ry ? ry[s] : int16_t{0};
            lc_ptr[s] = rc ? rc[s] : int16_t{0};
          }
        }
        corrections++;
      }
    }

    corrected->corrected_luma_frames_.put(frame_id, std::move(luma_data));
    corrected->corrected_chroma_frames_.put(frame_id, std::move(chroma_data));
    ORC_LOG_DEBUG("DropoutCorrectStage: YC frame {} done - {} corrections",
                  frame_id, corrections);
    return;
  }

  // -----------------------------------------------------------------------
  // Composite source path
  // -----------------------------------------------------------------------
  std::vector<int16_t> frame_data;
  frame_data.reserve(spl * height);
  for (size_t line = 0; line < height; ++line) {
    const int16_t* ptr = source->get_line(frame_id, line);
    if (ptr) {
      frame_data.insert(frame_data.end(), ptr, ptr + spl);
    } else {
      frame_data.insert(frame_data.end(), spl, int16_t{0});
    }
  }

  size_t corrections = 0;

  for (const auto& d : split_dropouts) {
    if (d.line >= height) {
      continue;
    }
    int16_t* line_ptr = &frame_data[d.line * spl];

    const auto loc = classify_dropout(d, desc, video_params);

    if (loc == DropoutLocation::VISIBLE_LINE) {
      auto luma_repl = find_replacement_line(
          *source, frame_id, d.line, d, config_.intrafield_only, false,
          field1_lines, Channel::COMPOSITE);
      if (!luma_repl.found && !config_.intrafield_only) {
        luma_repl = find_replacement_line(*source, frame_id, d.line, d, false,
                                          false, field1_lines,
                                          Channel::COMPOSITE);
      }

      auto chroma_repl = find_replacement_line(
          *source, frame_id, d.line, d, config_.intrafield_only,
          config_.match_chroma_phase, field1_lines, Channel::COMPOSITE);
      if (!chroma_repl.found && !config_.intrafield_only) {
        chroma_repl = find_replacement_line(
            *source, frame_id, d.line, d, false, config_.match_chroma_phase,
            field1_lines, Channel::COMPOSITE);
      }

      if (luma_repl.found || chroma_repl.found) {
        if (luma_repl.found && chroma_repl.found &&
            luma_repl.source_line == chroma_repl.source_line) {
          std::vector<int16_t> tmp_line(line_ptr, line_ptr + spl);
          apply_correction(
              tmp_line, d,
              source->get_line(frame_id, luma_repl.source_line), highlight_val,
              corrected->highlight_corrections_);
          std::copy(tmp_line.begin(), tmp_line.end(), line_ptr);
        } else {
          if (luma_repl.found) {
            const int16_t* rep =
                source->get_line(frame_id, luma_repl.source_line);
            for (uint32_t s = d.start_sample; s <= d.end_sample && s < spl;
                 ++s) {
              if (corrected->highlight_corrections_) {
                line_ptr[s] = highlight_val;
              } else if (rep) {
                line_ptr[s] = rep[s];
              }
            }
          }
        }
        corrections++;
      }
    } else {
      auto repl = find_replacement_line(
          *source, frame_id, d.line, d, config_.intrafield_only, false,
          field1_lines, Channel::COMPOSITE);
      if (!repl.found && !config_.intrafield_only) {
        repl = find_replacement_line(*source, frame_id, d.line, d, false,
                                     false, field1_lines, Channel::COMPOSITE);
      }
      if (repl.found) {
        std::vector<int16_t> tmp_line(line_ptr, line_ptr + spl);
        apply_correction(tmp_line, d,
                         source->get_line(frame_id, repl.source_line),
                         highlight_val, corrected->highlight_corrections_);
        std::copy(tmp_line.begin(), tmp_line.end(), line_ptr);
        corrections++;
      }
    }
  }

  corrected->corrected_frames_.put(frame_id, std::move(frame_data));
  ORC_LOG_DEBUG(
      "DropoutCorrectStage: composite frame {} done - {} corrections",
      frame_id, corrections);
}

// ============================================================================
// Parameters
// ============================================================================

std::vector<ParameterDescriptor> DropoutCorrectStage::get_parameter_descriptors(
    VideoSystem, SourceType) const {
  return {
      ParameterDescriptor{
          "overcorrect_extension", "Overcorrect (samples)",
          "Extend dropout regions by N samples on each side "
          "(0 = disabled, 24 = typical for damaged sources).",
          ParameterType::UINT32,
          ParameterConstraints{ParameterValue(0U), ParameterValue(48U),
                               ParameterValue(0U), {}, false, std::nullopt}},
      ParameterDescriptor{
          "intrafield_only", "Intrafield Only",
          "Restrict replacement to the same field (no interfield correction).",
          ParameterType::BOOL,
          ParameterConstraints{std::nullopt, std::nullopt, ParameterValue(false),
                               {}, false, std::nullopt}},
      ParameterDescriptor{
          "max_replacement_distance", "Max Replacement Distance (lines)",
          "Maximum distance (in lines) to search for a replacement line.",
          ParameterType::UINT32,
          ParameterConstraints{ParameterValue(1U), ParameterValue(100U),
                               ParameterValue(10U), {}, false, std::nullopt}},
      ParameterDescriptor{
          "match_chroma_phase", "Match Chroma Phase",
          "Prefer replacement lines with matching chroma phase.",
          ParameterType::BOOL,
          ParameterConstraints{std::nullopt, std::nullopt, ParameterValue(true),
                               {}, false, std::nullopt}},
      ParameterDescriptor{
          "highlight_corrections", "Highlight Corrections",
          "Fill corrections with white level instead of replacement data "
          "(for debugging).",
          ParameterType::BOOL,
          ParameterConstraints{std::nullopt, std::nullopt,
                               ParameterValue(false), {}, false, std::nullopt}},
  };
}

std::map<std::string, ParameterValue> DropoutCorrectStage::get_parameters()
    const {
  return {
      {"overcorrect_extension",
       ParameterValue{static_cast<uint32_t>(config_.overcorrect_extension)}},
      {"intrafield_only", ParameterValue{config_.intrafield_only}},
      {"max_replacement_distance",
       ParameterValue{
           static_cast<uint32_t>(config_.max_replacement_distance)}},
      {"match_chroma_phase", ParameterValue{config_.match_chroma_phase}},
      {"highlight_corrections",
       ParameterValue{config_.highlight_corrections}},
  };
}

bool DropoutCorrectStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  for (const auto& [key, value] : params) {
    if (key == "overcorrect_extension") {
      if (const auto* v = std::get_if<uint32_t>(&value)) {
        if (*v > 48U) {
          return false;
        }
        config_.overcorrect_extension = *v;
      } else {
        return false;
      }
    } else if (key == "intrafield_only") {
      if (const auto* v = std::get_if<bool>(&value)) {
        config_.intrafield_only = *v;
      } else {
        return false;
      }
    } else if (key == "max_replacement_distance") {
      if (const auto* v = std::get_if<uint32_t>(&value)) {
        config_.max_replacement_distance = *v;
      } else {
        return false;
      }
    } else if (key == "match_chroma_phase") {
      if (const auto* v = std::get_if<bool>(&value)) {
        config_.match_chroma_phase = *v;
      } else {
        return false;
      }
    } else if (key == "highlight_corrections") {
      if (const auto* v = std::get_if<bool>(&value)) {
        config_.highlight_corrections = *v;
      } else {
        return false;
      }
    } else {
      return false;
    }
  }
  return true;
}

// ============================================================================
// Preview
// ============================================================================

std::vector<PreviewOption> DropoutCorrectStage::get_preview_options() const {
  if (!cached_output_) {
    return {};
  }
  const auto params = cached_output_->get_video_parameters();
  const size_t fc = cached_output_->frame_count();
  if (fc == 0 || !params) {
    return {};
  }
  const uint32_t w = static_cast<uint32_t>(params->frame_width_nominal);
  const uint32_t h = static_cast<uint32_t>(params->frame_height);
  return {PreviewOption{"frame", "Frame (Scaled)", false, w, h,
                        static_cast<uint64_t>(fc), 0.7},
          PreviewOption{"frame_raw", "Frame (Raw)", false, w, h,
                        static_cast<uint64_t>(fc), 0.7}};
}

PreviewImage DropoutCorrectStage::render_preview(const std::string& option_id,
                                                 uint64_t index,
                                                 PreviewNavigationHint) const {
  if (!cached_output_) {
    return PreviewImage{};
  }
  const FrameID fid = static_cast<FrameID>(index);
  if (!cached_output_->has_frame(fid)) {
    return PreviewImage{};
  }
  return render_vfr_grayscale(*cached_output_, fid, option_id != "frame_raw");
}

}  // namespace orc
