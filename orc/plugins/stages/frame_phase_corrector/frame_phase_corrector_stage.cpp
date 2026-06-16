/*
 * File:        frame_phase_corrector_stage.cpp
 * Module:      orc-stage-plugin-frame-phase-corrector
 * Purpose:     Colour-frame sequence verification and field-swap correction
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "frame_phase_corrector_stage.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <sstream>

#include "error_types.h"
#include "frame_line_util.h"
#include "logging.h"
#include "preview_helpers.h"

namespace orc {

// ============================================================================
// PhaseCorectedRepresentation
// ============================================================================

PhaseCorectedRepresentation::PhaseCorectedRepresentation(
    std::shared_ptr<const VideoFrameRepresentation> source,
    std::map<FrameID, FrameCorrection> corrections)
    : VideoFrameRepresentationWrapper(std::move(source)),
      Artifact(ArtifactID("phase_corrected"), Provenance{}),
      corrections_(std::move(corrections)) {
  auto params = source_ ? source_->get_video_parameters() : std::nullopt;
  if (params) {
    switch (params->system) {
      case VideoSystem::PAL:
        field1_lines_ = kPalField1Lines;
        break;
      case VideoSystem::NTSC:
        field1_lines_ = kNtscField1Lines;
        break;
      case VideoSystem::PAL_M:
        field1_lines_ = kPalMField1Lines;
        break;
      default:
        field1_lines_ = 0;
        break;
    }
  }
}

size_t PhaseCorectedRepresentation::remap_line(size_t output_line,
                                               size_t frame_height) const {
  // Swap field1 and field2 blocks:
  // Output [0 .. field2_lines-1]         → Source [field1_lines .. H-1]
  // Output [field2_lines .. H-1]         → Source [0 .. field1_lines-1]
  if (field1_lines_ == 0 || frame_height == 0) return output_line;
  size_t field2_lines = frame_height - field1_lines_;
  if (output_line < field2_lines) {
    return field1_lines_ + output_line;
  }
  return output_line - field2_lines;
}

std::optional<FrameDescriptor>
PhaseCorectedRepresentation::get_frame_descriptor(FrameID id) const {
  auto desc = source_ ? source_->get_frame_descriptor(id) : std::nullopt;
  if (!desc) return std::nullopt;

  auto it = corrections_.find(id);
  if (it != corrections_.end()) {
    desc->colour_frame_index = it->second.corrected_colour_index;
  }
  return desc;
}

const VideoFrameRepresentation::sample_type*
PhaseCorectedRepresentation::get_frame(FrameID id) const {
  auto it = corrections_.find(id);
  if (it != corrections_.end() && it->second.swap_fields) {
    // No contiguous flat buffer exists for the swapped layout
    return nullptr;
  }
  return source_ ? source_->get_frame(id) : nullptr;
}

const VideoFrameRepresentation::sample_type*
PhaseCorectedRepresentation::get_line(FrameID id, size_t line) const {
  if (!source_) return nullptr;

  auto it = corrections_.find(id);
  if (it != corrections_.end() && it->second.swap_fields) {
    auto desc = source_->get_frame_descriptor(id);
    if (!desc) return nullptr;
    size_t src_line = remap_line(line, desc->height);
    return source_->get_line(id, src_line);
  }
  return source_->get_line(id, line);
}

std::vector<VideoFrameRepresentation::sample_type>
PhaseCorectedRepresentation::get_frame_copy(FrameID id) const {
  if (!source_) return {};

  auto it = corrections_.find(id);
  if (it == corrections_.end() || !it->second.swap_fields) {
    return source_->get_frame_copy(id);
  }

  auto desc = source_->get_frame_descriptor(id);
  if (!desc) return {};

  auto params = source_->get_video_parameters();
  int32_t nominal_spl = params ? params->frame_width_nominal : 1135;

  std::vector<sample_type> result;
  result.reserve(desc->samples_total);

  for (size_t out_line = 0; out_line < desc->height; ++out_line) {
    size_t src_line = remap_line(out_line, desc->height);
    const sample_type* ptr = source_->get_line(id, src_line);
    if (!ptr) {
      size_t width = frame_line_sample_count(
          desc->system, static_cast<size_t>(nominal_spl), src_line);
      result.insert(result.end(), width, 0);
    } else {
      size_t width = frame_line_sample_count(
          desc->system, static_cast<size_t>(nominal_spl), src_line);
      result.insert(result.end(), ptr, ptr + width);
    }
  }
  return result;
}

const VideoFrameRepresentation::sample_type*
PhaseCorectedRepresentation::get_line_luma(FrameID id, size_t line) const {
  if (!source_) return nullptr;
  auto it = corrections_.find(id);
  if (it != corrections_.end() && it->second.swap_fields) {
    auto desc = source_->get_frame_descriptor(id);
    if (!desc) return nullptr;
    return source_->get_line_luma(id, remap_line(line, desc->height));
  }
  return source_->get_line_luma(id, line);
}

const VideoFrameRepresentation::sample_type*
PhaseCorectedRepresentation::get_line_chroma(FrameID id, size_t line) const {
  if (!source_) return nullptr;
  auto it = corrections_.find(id);
  if (it != corrections_.end() && it->second.swap_fields) {
    auto desc = source_->get_frame_descriptor(id);
    if (!desc) return nullptr;
    return source_->get_line_chroma(id, remap_line(line, desc->height));
  }
  return source_->get_line_chroma(id, line);
}

std::optional<int> PhaseCorectedRepresentation::get_frame_phase_hint(
    FrameID id) const {
  auto it = corrections_.find(id);
  if (it != corrections_.end()) {
    return it->second.corrected_colour_index;
  }
  return source_ ? source_->get_frame_phase_hint(id) : std::nullopt;
}

// ============================================================================
// FramePhaseCorrectorStage
// ============================================================================

int FramePhaseCorrectorStage::next_colour_index(int current, VideoSystem sys) {
  if (current < 0) return -1;
  switch (sys) {
    case VideoSystem::PAL:
    case VideoSystem::PAL_M:
      // EBU Tech. 3280-E §1.1.1 / ITU-R BT.1700-1: 4-frame sequence 1-2-3-4
      return (current % 4) + 1;
    case VideoSystem::NTSC:
      // SMPTE 244M-2003 §3.2: 2-frame A/B sequence 0-1
      return 1 - current;
    default:
      return -1;
  }
}

int FramePhaseCorrectorStage::measure_colour_frame_index(
    const VideoFrameRepresentation& src, FrameID id, VideoSystem sys) {
  auto params = src.get_video_parameters();
  if (!params) return -1;

  const int16_t* frame_data = src.get_frame(id);
  if (!frame_data) return -1;

  // EBU Tech. 3280-E §1.2: PAL colour burst at samples 93..132.
  // SMPTE 244M-2003 §4.2.1: NTSC colour burst at samples 74..109.
  // ITU-R BT.1700-1 Annex 1 Part B: PAL_M uses same window as NTSC.
  constexpr size_t kRefLine = 9;
  constexpr size_t kBurstCount = 40;
  size_t burst_start = 0;
  size_t line_start = 0;

  switch (sys) {
    case VideoSystem::PAL:
      burst_start = 93;
      line_start = static_cast<size_t>(kRefLine) *
                   static_cast<size_t>(kPalSamplesPerLineNominal);
      break;
    case VideoSystem::NTSC:
      burst_start = 74;
      line_start = static_cast<size_t>(kRefLine) * kNtscSamplesPerLine;
      break;
    case VideoSystem::PAL_M:
      burst_start = 74;
      line_start = static_cast<size_t>(kRefLine) * kPalMSamplesPerLine;
      break;
    default:
      return -1;
  }

  const size_t abs_offset = line_start + burst_start;
  const int phase_base = static_cast<int>(abs_offset % 4);
  const int16_t* burst_ptr = frame_data + abs_offset;
  const int32_t blanking = params->blanking_level;

  double I = 0.0;
  double Q = 0.0;
  for (size_t n = 0; n < kBurstCount; ++n) {
    const double ac = static_cast<double>(burst_ptr[n]) - blanking;
    switch ((phase_base + static_cast<int>(n)) % 4) {
      case 0:
        I += ac;
        break;
      case 1:
        Q += ac;
        break;
      case 2:
        I -= ac;
        break;
      case 3:
        Q -= ac;
        break;
      default:
        break;
    }
  }

  constexpr double kMinBurstAmplitude = 20.0;
  if (std::sqrt(I * I + Q * Q) < kMinBurstAmplitude) return -1;

  double angle_deg = std::atan2(Q, I) * (180.0 / M_PI);
  if (angle_deg < 0.0) angle_deg += 360.0;

  if (sys == VideoSystem::NTSC) {
    // SMPTE 244M-2003 §3.2: Frame A (~180°) → 0; Frame B (~0°) → 1.
    return (angle_deg >= 90.0 && angle_deg < 270.0) ? 0 : 1;
  }

  const int sector = static_cast<int>(angle_deg / 90.0) % 4;
  if (sys == VideoSystem::PAL) {
    // EBU Tech. 3280-E §1.1.1: sector [0°,90°)→1, [90°,180°)→2, …
    static constexpr int kPalMap[4] = {1, 2, 3, 4};
    return kPalMap[sector];
  }
  // ITU-R BT.1700-1 Annex 1 Part B: PAL_M
  static constexpr int kPalMMap[4] = {1, 4, 3, 2};
  return kPalMMap[sector];
}

double FramePhaseCorrectorStage::measure_field_burst_phase(
    const VideoFrameRepresentation& src, FrameID id, bool second_field,
    VideoSystem sys) {
  // Measure burst phase by sampling a few lines from the colour burst region.
  // The burst occupies roughly samples 5–36 of each active line (PAL/NTSC).
  // We return the mean of phase angles derived from quadrature samples.
  // This is a lightweight heuristic sufficient for field-swap detection.

  auto params = src.get_video_parameters();
  if (!params) return 0.0;

  auto desc = src.get_frame_descriptor(id);
  if (!desc || desc->height == 0) return 0.0;

  // Choose which field's lines to sample
  size_t field_start_line = second_field ? kPalField1Lines : 0;
  if (sys == VideoSystem::NTSC) {
    field_start_line = second_field ? kNtscField1Lines : 0;
  } else if (sys == VideoSystem::PAL_M) {
    field_start_line = second_field ? kPalMField1Lines : 0;
  }

  // Sample lines 10–20 of the chosen field block (skip VBI)
  constexpr size_t kBurstStart = 8;
  constexpr size_t kBurstSamples = 20;
  constexpr size_t kSampleLines = 5;

  double phase_sum = 0.0;
  size_t counted = 0;

  for (size_t l = 10; l < 10 + kSampleLines; ++l) {
    size_t frame_line = field_start_line + l;
    if (frame_line >= desc->height) break;
    const auto* ptr = src.get_line(id, frame_line);
    if (!ptr) continue;

    // Quadrature detect: compare sum of even vs odd burst samples
    double even_sum = 0.0, odd_sum = 0.0;
    for (size_t s = kBurstStart; s < kBurstStart + kBurstSamples; ++s) {
      if (s % 2 == 0) {
        even_sum += static_cast<double>(ptr[s] - params->blanking_level);
      } else {
        odd_sum += static_cast<double>(ptr[s] - params->blanking_level);
      }
    }
    phase_sum += std::atan2(odd_sum, even_sum);
    ++counted;
  }

  if (counted == 0) return 0.0;
  return phase_sum / static_cast<double>(counted);
}

bool FramePhaseCorrectorStage::detect_field_swap(
    const VideoFrameRepresentation& src, FrameID id, VideoSystem sys) {
  // Compare burst phase of field-1-block vs field-2-block.
  // For PAL: field 1 should have a specific V-axis sign; field 2 the opposite.
  // A simple heuristic: if field 2 block's phase leads field 1's phase by more
  // than π/2, the fields are likely stored in wrong temporal order.
  // This is intentionally conservative to avoid false positives.
  double phase1 = measure_field_burst_phase(src, id, false, sys);
  double phase2 = measure_field_burst_phase(src, id, true, sys);

  double diff = phase2 - phase1;
  // Normalise to [-π, π]
  while (diff > M_PI) diff -= 2.0 * M_PI;
  while (diff < -M_PI) diff += 2.0 * M_PI;

  // For PAL, the V component flips every field; a very large phase difference
  // (close to π) between fields 1 and 2 suggests the swap condition.
  // Threshold: more than 2.5 rad difference suggests swapped fields.
  return std::abs(diff) > 2.5;
}

std::vector<ArtifactPtr> FramePhaseCorrectorStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  if (inputs.empty()) {
    throw DAGExecutionError("FramePhaseCorrectorStage requires one input");
  }

  auto source =
      std::dynamic_pointer_cast<const VideoFrameRepresentation>(inputs[0]);
  if (!source) {
    throw DAGExecutionError(
        "FramePhaseCorrectorStage: input must be a VideoFrameRepresentation");
  }

  if (!parameters.empty()) set_parameters(parameters);

  // Fast pass-through when both corrections are disabled
  if (!correct_field_swap_ && !verify_phase_sequence_) {
    cached_output_ = source;
    return {inputs[0]};
  }

  auto params = source->get_video_parameters();
  VideoSystem sys = params ? params->system : VideoSystem::Unknown;

  std::map<FrameID, PhaseCorectedRepresentation::FrameCorrection> corrections;

  int64_t swaps_corrected = 0;
  int64_t breaks_detected = 0;
  int64_t breaks_marked = 0;

  FrameIDRange rng = source->frame_range();
  int prev_colour_index = -1;

  for (FrameID fid = rng.first; fid <= rng.last; ++fid) {
    if (!source->has_frame(fid)) continue;

    if (!source->get_frame_descriptor(fid)) continue;

    // Measure colour-frame index from the raw signal; the source stage does
    // not perform this measurement (that is ColourFramePhaseObserver's role).
    const int measured_colour_index =
        (sys != VideoSystem::Unknown)
            ? measure_colour_frame_index(*source, fid, sys)
            : -1;

    PhaseCorectedRepresentation::FrameCorrection corr;
    corr.corrected_colour_index = measured_colour_index;
    bool needs_entry = false;

    // 1. Field-swap detection and correction
    if (correct_field_swap_ && sys != VideoSystem::Unknown) {
      if (detect_field_swap(*source, fid, sys)) {
        corr.swap_fields = true;
        needs_entry = true;
        ++swaps_corrected;

        // Derive corrected colour_frame_index by inferring from sequence
        // The swap changes which field is "first", which shifts the phase
        // by half a colour-frame period.  For a practical correction we
        // derive the corrected index from the previous frame.
        if (prev_colour_index >= 0) {
          corr.corrected_colour_index =
              next_colour_index(prev_colour_index, sys);
        }

        ORC_LOG_DEBUG(
            "FramePhaseCorrectorStage: field swap detected at frame {}; "
            "corrected colour_frame_index {} → {}",
            fid.value(), measured_colour_index, corr.corrected_colour_index);
      }
    }

    // 2. Phase sequence verification
    if (verify_phase_sequence_ && sys != VideoSystem::Unknown) {
      int effective_idx = corr.corrected_colour_index;
      if (prev_colour_index >= 0 && effective_idx >= 0) {
        int expected = next_colour_index(prev_colour_index, sys);
        if (effective_idx != expected) {
          ++breaks_detected;
          ++breaks_marked;
          corr.corrected_colour_index = -1;
          needs_entry = true;
          ORC_LOG_DEBUG(
              "FramePhaseCorrectorStage: phase break at frame {} "
              "(expected {}, got {}); marking as unknown",
              fid.value(), expected, effective_idx);
        }
      }
      prev_colour_index = corr.corrected_colour_index;
    } else {
      prev_colour_index = corr.corrected_colour_index;
    }

    if (needs_entry) {
      corrections[fid] = corr;
    }
  }

  // ObservationContext is still FieldID-keyed; use FieldID(0) for aggregates.
  if (swaps_corrected > 0) {
    observation_context.set(FieldID(0), "frame_phase_corrector",
                            "field_swaps_corrected", swaps_corrected);
  }
  if (breaks_detected > 0) {
    observation_context.set(FieldID(0), "frame_phase_corrector",
                            "phase_breaks_detected", breaks_detected);
  }
  if (breaks_marked > 0) {
    observation_context.set(FieldID(0), "frame_phase_corrector",
                            "phase_breaks_marked", breaks_marked);
  }

  ORC_LOG_DEBUG(
      "FramePhaseCorrectorStage: {} swap(s) corrected, {} break(s) detected",
      swaps_corrected, breaks_detected);

  if (corrections.empty()) {
    // Nothing to correct — pass through the source directly
    cached_output_ = source;
    return {inputs[0]};
  }

  auto result = std::make_shared<PhaseCorectedRepresentation>(
      source, std::move(corrections));
  cached_output_ = result;
  return {result};
}

std::vector<ParameterDescriptor>
FramePhaseCorrectorStage::get_parameter_descriptors(
    VideoSystem /*project_format*/, SourceType /*source_type*/) const {
  return {
      ParameterDescriptor{
          "correct_field_swap", "Correct Field Swap",
          "Detect and correct frames where the two field blocks are stored in "
          "the wrong temporal order",
          ParameterType::BOOL,
          ParameterConstraints{std::nullopt,
                               std::nullopt,
                               ParameterValue{true},
                               {},
                               false,
                               std::nullopt}},
      ParameterDescriptor{
          "verify_phase_sequence", "Verify Phase Sequence",
          "Walk the colour-frame-index sequence and mark break-point frames "
          "with colour_frame_index = -1",
          ParameterType::BOOL,
          ParameterConstraints{std::nullopt,
                               std::nullopt,
                               ParameterValue{true},
                               {},
                               false,
                               std::nullopt}},
  };
}

std::map<std::string, ParameterValue> FramePhaseCorrectorStage::get_parameters()
    const {
  return {{"correct_field_swap", ParameterValue{correct_field_swap_}},
          {"verify_phase_sequence", ParameterValue{verify_phase_sequence_}}};
}

bool FramePhaseCorrectorStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  for (const auto& [key, value] : params) {
    if (key == "correct_field_swap") {
      if (auto* v = std::get_if<bool>(&value)) {
        correct_field_swap_ = *v;
      } else {
        return false;
      }
    } else if (key == "verify_phase_sequence") {
      if (auto* v = std::get_if<bool>(&value)) {
        verify_phase_sequence_ = *v;
      } else {
        return false;
      }
    } else {
      ORC_LOG_WARN("FramePhaseCorrectorStage: unknown parameter '{}'", key);
      return false;
    }
  }
  return true;
}

StagePreviewCapability FramePhaseCorrectorStage::get_preview_capability()
    const {
  return PreviewHelpers::make_signal_preview_capability(cached_output_);
}

}  // namespace orc
