/*
 * File:        cvbs_source_stage.cpp
 * Module:      orc-core
 * Purpose:     CVBS source loading stage implementation (Phase 1 skeleton)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "cvbs_source_stage.h"

#include <soxr.h>
#include <sqlite3.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <vector>

#include "error_types.h"
#include "logging.h"
#include "preview_helpers.h"
#include "preview_renderer.h"

namespace orc {

namespace {

constexpr int32_t kInternalSampleScale = 64;

struct CVBSGeometry {
  VideoSystem system = VideoSystem::Unknown;
  size_t samples_per_line = 0;
  size_t first_field_lines = 0;
  size_t second_field_lines = 0;
  double sample_rate = 0.0;
  double fsc = 0.0;
  int32_t active_samples = -1;
  int32_t active_video_start = -1;
  int32_t sync_tip_ire_16b = -1;
  int32_t blanking_ire_16b = -1;
  int32_t black_ire_16b = -1;
  int32_t white_ire_16b = -1;
  int32_t colour_burst_start = -1;
  int32_t colour_burst_end = -1;
  size_t fallback_sync_start_sample = 0;
  int32_t first_active_field_line = -1;
  int32_t last_active_field_line = -1;
  int32_t first_active_frame_line = -1;
  int32_t last_active_frame_line = -1;
};

// Helper: Determine sample count for a PAL line at given field-line index
// (0-indexed within field). PAL spec: exactly 4 lines per frame have 1136
// samples, remainder use 1135. Pattern per field: line_index in {155, 311} for
// even field; {156, 312} for odd field.
inline size_t pal_line_sample_count(size_t field_line_index,
                                    bool is_odd_field) {
  if (is_odd_field) {
    // Odd field (313 lines): lines 156 and 312 have 1136
    return (field_line_index == 156 || field_line_index == 312) ? 1136 : 1135;
  } else {
    // Even field (312 lines): lines 155 and 311 have 1136
    return (field_line_index == 155 || field_line_index == 311) ? 1136 : 1135;
  }
}

// Calculate total samples in a PAL field accounting for 1136-sample fractional
// lines.
inline size_t pal_field_total_samples(bool is_odd_field) {
  const size_t max_lines = is_odd_field ? 313 : 312;
  size_t total = 0;
  for (size_t i = 0; i < max_lines; ++i) {
    total += pal_line_sample_count(i, is_odd_field);
  }
  return total;
}

CVBSGeometry geometry_for_standard(const std::string& video_standard) {
  if (video_standard == "PAL") {
    const double fsc = (283.75 * 15625.0) + 25.0;
    return CVBSGeometry{
        VideoSystem::PAL,
        1135,  // nominal samples per line (some lines are 1136 for 4fsc phase)
        313,
        312,
        4.0 * fsc,
        fsc,
        948,
        157,
        4 * kInternalSampleScale,
        256 * kInternalSampleScale,
        282 * kInternalSampleScale,
        844 * kInternalSampleScale,
        93,
        137,
        978,
        22,
        310,
        44,
        620,
    };
  }

  const double fsc = 315.0e6 / 88.0;
  return CVBSGeometry{
      VideoSystem::NTSC,
      910,
      263,
      262,
      4.0 * fsc,
      fsc,
      768,
      126,
      16 * kInternalSampleScale,
      240 * kInternalSampleScale,
      252 * kInternalSampleScale,
      800 * kInternalSampleScale,
      74,
      110,
      784,
      20,
      259,
      40,
      525,
  };
}

FieldParity parity_for_field(VideoSystem system, bool is_first_field) {
  switch (system) {
    case VideoSystem::NTSC:
    case VideoSystem::PAL_M:
    case VideoSystem::PAL:
    case VideoSystem::Unknown:
    default:
      // CVBS source emits first temporal field as the upper field.
      return is_first_field ? FieldParity::Top : FieldParity::Bottom;
  }
}

std::string derive_metadata_sidecar_path(const std::string& input_path) {
  namespace fs = std::filesystem;
  const fs::path source_path(input_path);
  fs::path meta_path = source_path;
  meta_path.replace_extension(".meta");
  return meta_path.string();
}

std::optional<size_t> detect_sync_start_sample(const uint16_t* line_data,
                                               size_t line_length,
                                               int32_t blanking_ire_16b,
                                               int32_t sync_tip_ire_16b) {
  if (line_data == nullptr || line_length < 4) {
    return std::nullopt;
  }

  const int32_t threshold =
      sync_tip_ire_16b + ((blanking_ire_16b - sync_tip_ire_16b) / 2);
  constexpr size_t kMinimumSyncRun = 16;
  const size_t search_begin = 0;
  const size_t search_end = line_length;

  for (size_t sample = search_begin; sample < search_end; ++sample) {
    const int32_t current = static_cast<int32_t>(line_data[sample]);
    const size_t next_index = (sample + 1) % line_length;
    const int32_t next = static_cast<int32_t>(line_data[next_index]);
    if (current > threshold && next <= threshold) {
      size_t low_run = 0;
      for (size_t offset = 0; offset < line_length; ++offset) {
        const size_t probe_index = (next_index + offset) % line_length;
        if (static_cast<int32_t>(line_data[probe_index]) <= threshold) {
          ++low_run;
        } else {
          break;
        }
      }

      if (low_run >= kMinimumSyncRun) {
        return next_index;
      }
    }
  }

  return std::nullopt;
}

size_t detect_global_sync_start_sample(
    const std::vector<uint16_t>& normalized_words, const CVBSGeometry& geometry,
    size_t full_frame_count) {
  const size_t frame_samples =
      geometry.samples_per_line *
      (geometry.first_field_lines + geometry.second_field_lines);
  std::vector<size_t> detections;
  detections.reserve(
      std::min(full_frame_count, static_cast<size_t>(8)) *
      (geometry.first_field_lines + geometry.second_field_lines));

  const size_t probe_frame_count =
      std::min(full_frame_count, static_cast<size_t>(8));

  for (size_t frame_index = 0; frame_index < probe_frame_count; ++frame_index) {
    const size_t frame_offset = frame_index * frame_samples;
    const size_t lines_in_frame =
        geometry.first_field_lines + geometry.second_field_lines;

    for (size_t line_index = 0; line_index < lines_in_frame; ++line_index) {
      const size_t line_offset =
          frame_offset + (line_index * geometry.samples_per_line);
      const uint16_t* line_ptr = normalized_words.data() + line_offset;
      const auto detected = detect_sync_start_sample(
          line_ptr, geometry.samples_per_line, geometry.blanking_ire_16b,
          geometry.sync_tip_ire_16b);
      if (detected.has_value()) {
        detections.push_back(*detected);
      }
    }
  }

  if (detections.empty()) {
    return geometry.fallback_sync_start_sample;
  }

  std::sort(detections.begin(), detections.end());
  return detections[detections.size() / 2];
}

// Rotate uniform-length lines to sync start (used for NTSC and standard PAL
// processing).
void rotate_field_lines_to_sync_start(std::vector<uint16_t>& field_samples,
                                      size_t line_length,
                                      int32_t blanking_ire_16b,
                                      int32_t sync_tip_ire_16b) {
  if (line_length == 0) {
    return;
  }

  const size_t line_count = field_samples.size() / line_length;

  for (size_t line_index = 0; line_index < line_count; ++line_index) {
    const size_t offset = line_index * line_length;
    uint16_t* line_ptr = field_samples.data() + offset;

    const auto detected_sync = detect_sync_start_sample(
        line_ptr, line_length, blanking_ire_16b, sync_tip_ire_16b);

    if (detected_sync.has_value()) {
      const size_t shift = *detected_sync % line_length;
      if (shift > 0) {
        auto line_begin =
            field_samples.begin() + static_cast<std::ptrdiff_t>(offset);
        std::rotate(line_begin, line_begin + static_cast<std::ptrdiff_t>(shift),
                    line_begin + static_cast<std::ptrdiff_t>(line_length));
      }
    }
  }
}

std::optional<size_t> detect_field_median_sync_start_sample(
    const std::vector<uint16_t>& field_samples, size_t line_length,
    int32_t blanking_ire_16b, int32_t sync_tip_ire_16b) {
  if (line_length == 0) {
    return std::nullopt;
  }

  const size_t line_count = field_samples.size() / line_length;
  if (line_count == 0) {
    return std::nullopt;
  }

  std::vector<size_t> detections;
  detections.reserve(line_count);

  for (size_t line_index = 0; line_index < line_count; ++line_index) {
    const size_t offset = line_index * line_length;
    const uint16_t* line_ptr = field_samples.data() + offset;
    const auto detected_sync = detect_sync_start_sample(
        line_ptr, line_length, blanking_ire_16b, sync_tip_ire_16b);

    if (detected_sync.has_value()) {
      detections.push_back(*detected_sync);
    }
  }

  if (detections.empty()) {
    return std::nullopt;
  }

  std::sort(detections.begin(), detections.end());
  return detections[detections.size() / 2];
}

std::optional<size_t> detect_shared_frame_sync_start_sample(
    const std::vector<uint16_t>& first_field_samples,
    const std::vector<uint16_t>& second_field_samples, size_t line_length,
    int32_t blanking_ire_16b, int32_t sync_tip_ire_16b) {
  if (line_length == 0) {
    return std::nullopt;
  }

  std::vector<size_t> detections;
  detections.reserve(
      (first_field_samples.size() + second_field_samples.size()) / line_length);

  const auto append_detections =
      [&](const std::vector<uint16_t>& field_samples) {
        const size_t line_count = field_samples.size() / line_length;
        for (size_t line_index = 0; line_index < line_count; ++line_index) {
          const size_t offset = line_index * line_length;
          const uint16_t* line_ptr = field_samples.data() + offset;
          const auto detected_sync = detect_sync_start_sample(
              line_ptr, line_length, blanking_ire_16b, sync_tip_ire_16b);
          if (detected_sync.has_value()) {
            detections.push_back(*detected_sync);
          }
        }
      };

  append_detections(first_field_samples);
  append_detections(second_field_samples);

  if (detections.empty()) {
    return std::nullopt;
  }

  std::sort(detections.begin(), detections.end());
  return detections[detections.size() / 2];
}

std::optional<size_t> detect_pal_field_median_sync_start_sample_variable(
    const std::vector<uint16_t>& field_samples, bool is_odd_field,
    int32_t blanking_ire_16b, int32_t sync_tip_ire_16b) {
  std::vector<size_t> detections;
  detections.reserve(is_odd_field ? 313u : 312u);

  size_t sample_offset = 0;
  const size_t max_field_lines = is_odd_field ? 313u : 312u;
  for (size_t field_line_index = 0; field_line_index < max_field_lines;
       ++field_line_index) {
    const size_t actual_line_length =
        pal_line_sample_count(field_line_index, is_odd_field);
    if (sample_offset + actual_line_length > field_samples.size()) {
      break;
    }

    const uint16_t* line_ptr = field_samples.data() + sample_offset;
    const auto detected_sync = detect_sync_start_sample(
        line_ptr, actual_line_length, blanking_ire_16b, sync_tip_ire_16b);
    if (detected_sync.has_value()) {
      detections.push_back(*detected_sync);
    }

    sample_offset += actual_line_length;
  }

  if (detections.empty()) {
    return std::nullopt;
  }

  std::sort(detections.begin(), detections.end());
  return detections[detections.size() / 2];
}

std::optional<size_t> detect_shared_pal_frame_sync_start_sample_variable(
    const std::vector<uint16_t>& odd_field_samples,
    const std::vector<uint16_t>& even_field_samples, int32_t blanking_ire_16b,
    int32_t sync_tip_ire_16b) {
  std::vector<size_t> detections;

  const auto odd_sync = detect_pal_field_median_sync_start_sample_variable(
      odd_field_samples, true, blanking_ire_16b, sync_tip_ire_16b);
  if (odd_sync.has_value()) {
    detections.push_back(*odd_sync);
  }

  const auto even_sync = detect_pal_field_median_sync_start_sample_variable(
      even_field_samples, false, blanking_ire_16b, sync_tip_ire_16b);
  if (even_sync.has_value()) {
    detections.push_back(*even_sync);
  }

  if (detections.empty()) {
    return std::nullopt;
  }

  std::sort(detections.begin(), detections.end());
  return detections[detections.size() / 2];
}

void rotate_field_lines_by_fixed_shift(std::vector<uint16_t>& field_samples,
                                       size_t line_length, size_t shift) {
  if (line_length == 0) {
    return;
  }

  const size_t normalized_shift = shift % line_length;
  if (normalized_shift == 0) {
    return;
  }

  const size_t line_count = field_samples.size() / line_length;
  for (size_t line_index = 0; line_index < line_count; ++line_index) {
    const size_t offset = line_index * line_length;
    auto line_begin =
        field_samples.begin() + static_cast<std::ptrdiff_t>(offset);
    std::rotate(line_begin,
                line_begin + static_cast<std::ptrdiff_t>(normalized_shift),
                line_begin + static_cast<std::ptrdiff_t>(line_length));
  }
}

void rotate_pal_field_lines_by_fixed_shift(std::vector<uint16_t>& field_samples,
                                           bool is_odd_field, size_t shift) {
  size_t sample_offset = 0;
  const size_t max_field_lines = is_odd_field ? 313u : 312u;

  for (size_t field_line_index = 0; field_line_index < max_field_lines;
       ++field_line_index) {
    const size_t actual_line_length =
        pal_line_sample_count(field_line_index, is_odd_field);
    if (sample_offset + actual_line_length > field_samples.size()) {
      break;
    }

    const size_t normalized_shift = shift % actual_line_length;
    if (normalized_shift > 0) {
      auto line_begin =
          field_samples.begin() + static_cast<std::ptrdiff_t>(sample_offset);
      std::rotate(line_begin,
                  line_begin + static_cast<std::ptrdiff_t>(normalized_shift),
                  line_begin + static_cast<std::ptrdiff_t>(actual_line_length));
    }

    sample_offset += actual_line_length;
  }
}

std::vector<uint16_t> resample_sequence_sinc_soxr(
    const std::vector<uint16_t>& input, size_t output_samples) {
  if (output_samples == 0 || input.empty()) {
    return {};
  }

  if (input.size() == output_samples) {
    return input;
  }

  std::vector<float> input_float;
  input_float.reserve(input.size());
  for (uint16_t sample : input) {
    input_float.push_back(static_cast<float>(sample));
  }

  std::vector<float> output_float(output_samples, 0.0f);

  if (output_samples == 1 || input.size() == 1) {
    std::vector<uint16_t> output(output_samples, input.front());
    return output;
  }

  soxr_error_t error = nullptr;
  const soxr_io_spec_t io_spec = soxr_io_spec(SOXR_FLOAT32_I, SOXR_FLOAT32_I);
  const soxr_quality_spec_t quality_spec = soxr_quality_spec(SOXR_VHQ, 0);
  const soxr_runtime_spec_t runtime_spec = soxr_runtime_spec(0);
  soxr_t resampler = soxr_create(static_cast<double>(input.size()),
                                 static_cast<double>(output_samples), 1, &error,
                                 &io_spec, &quality_spec, &runtime_spec);

  if (error != nullptr) {
    throw std::runtime_error(std::string("CVBS PAL resampler init failed: ") +
                             error);
  }

  size_t output_done = 0;
  error =
      soxr_process(resampler, input_float.data(), input_float.size(), nullptr,
                   output_float.data(), output_float.size(), &output_done);
  soxr_delete(resampler);

  if (error != nullptr) {
    throw std::runtime_error(
        std::string("CVBS PAL resampler process failed: ") + error);
  }

  if (output_done < output_samples && output_done > 0) {
    const float last = output_float[output_done - 1];
    std::fill(output_float.begin() + static_cast<std::ptrdiff_t>(output_done),
              output_float.end(), last);
  }

  std::vector<uint16_t> output;
  output.reserve(output_samples);
  for (float sample : output_float) {
    const float clamped = std::clamp(sample, 0.0f, 65535.0f);
    output.push_back(static_cast<uint16_t>(std::lround(clamped)));
  }

  return output;
}

// Rotate PAL field lines accounting for variable line lengths (fractional 4fsc
// lines). Each line gets rotated independently based on its actual sample count
// (1135 or 1136).
void rotate_pal_field_lines_to_sync_start(std::vector<uint16_t>& field_samples,
                                          bool is_odd_field,
                                          int32_t blanking_ire_16b,
                                          int32_t sync_tip_ire_16b) {
  size_t sample_offset = 0;
  const size_t max_field_lines = is_odd_field ? 313 : 312;

  for (size_t field_line_index = 0; field_line_index < max_field_lines;
       ++field_line_index) {
    const size_t actual_line_length =
        pal_line_sample_count(field_line_index, is_odd_field);

    if (sample_offset + actual_line_length > field_samples.size()) {
      break;  // Incomplete line at end of field
    }

    uint16_t* line_ptr = field_samples.data() + sample_offset;

    const auto detected_sync = detect_sync_start_sample(
        line_ptr, actual_line_length, blanking_ire_16b, sync_tip_ire_16b);

    if (detected_sync.has_value()) {
      const size_t shift = *detected_sync % actual_line_length;
      if (shift > 0) {
        auto line_begin =
            field_samples.begin() + static_cast<std::ptrdiff_t>(sample_offset);
        std::rotate(
            line_begin, line_begin + static_cast<std::ptrdiff_t>(shift),
            line_begin + static_cast<std::ptrdiff_t>(actual_line_length));
      }
    }

    sample_offset += actual_line_length;
  }
}

int32_t normalize_sample_to_internal_domain(uint16_t raw_word,
                                            const std::string& sample_encoding,
                                            size_t& clamped_low_count) {
  int32_t value_10bit = 0;

  if (sample_encoding == "CVBS_TPG21_4FSC") {
    const int16_t signed_word = static_cast<int16_t>(raw_word);
    const auto decoded = static_cast<int32_t>(
        std::lround(static_cast<double>(signed_word) / 64.0));
    value_10bit = decoded + 508;
  } else {
    value_10bit = static_cast<int32_t>(raw_word) / 64;
  }

  if (value_10bit < 0) {
    ++clamped_low_count;
    value_10bit = 0;
  }
  if (value_10bit > 1023) {
    value_10bit = 1023;
  }

  return value_10bit * kInternalSampleScale;
}

class CVBSDecodedFieldRepresentation final : public VideoFieldRepresentation {
 public:
  CVBSDecodedFieldRepresentation(
      CVBSGeometry geometry, std::shared_ptr<ICVBSSourceStageDeps> deps,
      std::string input_path, std::string sample_encoding,
      std::string stage_name, size_t frame_samples, size_t first_field_samples,
      size_t second_field_samples, size_t frame_count,
      SourceParameters video_params, ArtifactID artifact_id,
      Provenance provenance)
      : VideoFieldRepresentation(std::move(artifact_id), std::move(provenance)),
        geometry_(geometry),
        deps_(std::move(deps)),
        input_path_(std::move(input_path)),
        sample_encoding_(std::move(sample_encoding)),
        stage_name_(std::move(stage_name)),
        frame_samples_(frame_samples),
        first_field_samples_(first_field_samples),
        second_field_samples_(second_field_samples),
        frame_count_(frame_count),
        video_params_(std::move(video_params)) {}

  FieldIDRange field_range() const override {
    if (frame_count_ == 0) {
      return FieldIDRange();
    }
    return FieldIDRange(FieldID(0), FieldID(frame_count_ * 2));
  }

  size_t field_count() const override { return frame_count_ * 2; }

  bool has_field(FieldID id) const override {
    return id.is_valid() && id.value() < (frame_count_ * 2);
  }

  std::optional<FieldDescriptor> get_descriptor(FieldID id) const override {
    if (!has_field(id)) {
      return std::nullopt;
    }

    const bool is_first_field = (id.value() % 2) == 0;

    FieldDescriptor descriptor;
    descriptor.field_id = id;
    descriptor.parity = parity_for_field(geometry_.system, is_first_field);
    descriptor.format = video_format_from_system(geometry_.system);
    descriptor.system = geometry_.system;
    descriptor.width = geometry_.samples_per_line;
    descriptor.height = is_first_field ? geometry_.first_field_lines
                                       : geometry_.second_field_lines;
    return descriptor;
  }

  const sample_type* get_line(FieldID id, size_t line) const override {
    auto descriptor = get_descriptor(id);
    if (!descriptor.has_value() || line >= descriptor->height) {
      return nullptr;
    }

    const auto& field = get_decoded_field(id);
    const size_t line_offset = line * descriptor->width;
    if (line_offset + descriptor->width > field.size()) {
      return nullptr;
    }

    return &field[line_offset];
  }

  std::vector<sample_type> get_field(FieldID id) const override {
    if (!has_field(id)) {
      return {};
    }
    return get_decoded_field(id);
  }

  std::optional<FieldParityHint> get_field_parity_hint(
      FieldID id) const override {
    if (!has_field(id)) {
      return std::nullopt;
    }

    return FieldParityHint{
        (id.value() % 2) == 0,
        HintSource::METADATA,
        HintTraits::METADATA_CONFIDENCE,
    };
  }

  std::optional<ActiveLineHint> get_active_line_hint() const override {
    return ActiveLineHint{
        video_params_.first_active_frame_line,
        video_params_.last_active_frame_line,
        video_params_.first_active_field_line,
        video_params_.last_active_field_line,
        HintSource::METADATA,
        HintTraits::METADATA_CONFIDENCE,
    };
  }

  std::optional<SourceParameters> get_video_parameters() const override {
    return video_params_;
  }

  std::string type_name() const override {
    return "CVBSDecodedFieldRepresentation";
  }

 private:
  struct DecodedFrame {
    std::vector<sample_type> first_field;
    std::vector<sample_type> second_field;
  };

  const std::vector<sample_type>& get_decoded_field(FieldID id) const {
    const size_t field_index = id.value();
    const size_t frame_index = field_index / 2;
    const bool want_first_field = (field_index % 2) == 0;

    ensure_frame_cached(frame_index);

    std::lock_guard<std::mutex> lock(cache_mutex_);
    const auto it = frame_cache_.find(frame_index);
    if (it == frame_cache_.end()) {
      throw std::runtime_error("CVBS frame cache lookup failed after decode");
    }
    return want_first_field ? it->second.first_field : it->second.second_field;
  }

  void ensure_frame_cached(size_t frame_index) const {
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      if (frame_cache_.find(frame_index) != frame_cache_.end()) {
        return;
      }
    }

    DecodedFrame decoded = decode_frame(frame_index);

    std::lock_guard<std::mutex> lock(cache_mutex_);
    frame_cache_.try_emplace(frame_index, std::move(decoded));
  }

  DecodedFrame decode_frame(size_t frame_index) const {
    const size_t frame_word_offset = frame_index * frame_samples_;

    std::vector<uint16_t> raw_frame_words;
    std::string read_error;
    if (!deps_->read_input_words_at(input_path_, frame_word_offset,
                                    frame_samples_, raw_frame_words,
                                    read_error)) {
      throw std::runtime_error(
          "Failed to read frame " + std::to_string(frame_index) +
          " from CVBS payload '" + input_path_ + "': " + read_error);
    }

    if (raw_frame_words.size() != frame_samples_) {
      throw std::runtime_error("Short CVBS frame read at frame " +
                               std::to_string(frame_index) + " from payload '" +
                               input_path_ + "'");
    }

    size_t clamped_low_count = 0;
    std::vector<uint16_t> normalized_words;
    normalized_words.reserve(raw_frame_words.size());
    for (uint16_t raw_word : raw_frame_words) {
      normalized_words.push_back(
          static_cast<uint16_t>(normalize_sample_to_internal_domain(
              raw_word, sample_encoding_, clamped_low_count)));
    }

    if (clamped_low_count > 0) {
      ORC_LOG_WARN(
          "{}: Clamped {} samples below 0 while decoding frame {} using '{}'",
          stage_name_, clamped_low_count, frame_index, sample_encoding_);
    }

    DecodedFrame result;

    if (geometry_.system == VideoSystem::PAL) {
      std::vector<uint16_t> first_field(
          normalized_words.begin(),
          normalized_words.begin() +
              static_cast<std::ptrdiff_t>(first_field_samples_));
      std::vector<uint16_t> second_field(
          normalized_words.begin() +
              static_cast<std::ptrdiff_t>(first_field_samples_),
          normalized_words.begin() +
              static_cast<std::ptrdiff_t>(first_field_samples_ +
                                          second_field_samples_));

      auto& odd_field = first_field;
      auto& even_field = second_field;

      const auto shared_sync =
          detect_shared_pal_frame_sync_start_sample_variable(
              odd_field, even_field, geometry_.blanking_ire_16b,
              geometry_.sync_tip_ire_16b);
      if (shared_sync.has_value()) {
        rotate_pal_field_lines_by_fixed_shift(even_field, false, *shared_sync);
        rotate_pal_field_lines_by_fixed_shift(odd_field, true, *shared_sync);
      }

      std::vector<uint16_t> frame_samples_aligned;
      frame_samples_aligned.reserve(first_field.size() + second_field.size());
      frame_samples_aligned.insert(frame_samples_aligned.end(),
                                   first_field.begin(), first_field.end());
      frame_samples_aligned.insert(frame_samples_aligned.end(),
                                   second_field.begin(), second_field.end());

      const size_t pal_uniform_frame_samples =
          (geometry_.first_field_lines + geometry_.second_field_lines) *
          geometry_.samples_per_line;
      std::vector<uint16_t> frame_uniform = resample_sequence_sinc_soxr(
          frame_samples_aligned, pal_uniform_frame_samples);

      const size_t odd_uniform_samples =
          geometry_.first_field_lines * geometry_.samples_per_line;
      result.first_field.assign(
          frame_uniform.begin(),
          frame_uniform.begin() +
              static_cast<std::ptrdiff_t>(odd_uniform_samples));
      result.second_field.assign(
          frame_uniform.begin() +
              static_cast<std::ptrdiff_t>(odd_uniform_samples),
          frame_uniform.end());
    } else {
      result.first_field.assign(
          normalized_words.begin(),
          normalized_words.begin() +
              static_cast<std::ptrdiff_t>(first_field_samples_));
      rotate_field_lines_to_sync_start(
          result.first_field, geometry_.samples_per_line,
          geometry_.blanking_ire_16b, geometry_.sync_tip_ire_16b);

      result.second_field.assign(
          normalized_words.begin() +
              static_cast<std::ptrdiff_t>(first_field_samples_),
          normalized_words.begin() +
              static_cast<std::ptrdiff_t>(first_field_samples_ +
                                          second_field_samples_));
      rotate_field_lines_to_sync_start(
          result.second_field, geometry_.samples_per_line,
          geometry_.blanking_ire_16b, geometry_.sync_tip_ire_16b);
    }

    return result;
  }

  CVBSGeometry geometry_;
  std::shared_ptr<ICVBSSourceStageDeps> deps_;
  std::string input_path_;
  std::string sample_encoding_;
  std::string stage_name_;
  size_t frame_samples_ = 0;
  size_t first_field_samples_ = 0;
  size_t second_field_samples_ = 0;
  size_t frame_count_ = 0;
  mutable std::mutex cache_mutex_;
  mutable std::map<size_t, DecodedFrame> frame_cache_;
  SourceParameters video_params_;
};

class CVBSSourceStageDeps final : public ICVBSSourceStageDeps {
 public:
  bool validate_input_file(const std::string& input_path,
                           std::string& error_message) const override {
    namespace fs = std::filesystem;

    std::error_code ec;
    if (!fs::exists(input_path, ec)) {
      error_message = "CVBS source file not found: '" + input_path + "'";
      return false;
    }

    if (!fs::is_regular_file(input_path, ec)) {
      error_message =
          "CVBS source path is not a regular file: '" + input_path + "'";
      return false;
    }

    std::ifstream input_stream(input_path, std::ios::binary);
    if (!input_stream.is_open()) {
      error_message = "CVBS source file is not readable: '" + input_path + "'";
      return false;
    }

    input_stream.seekg(0, std::ios::end);
    if (!input_stream.good()) {
      error_message =
          "Failed to inspect CVBS source file size: '" + input_path + "'";
      return false;
    }

    if (input_stream.tellg() <= 0) {
      error_message = "CVBS source file is empty: '" + input_path + "'";
      return false;
    }

    return true;
  }

  std::optional<CVBSMetadataRecord> load_metadata(
      const std::string& meta_path, std::string& error_message) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(meta_path, ec)) {
      error_message = "Metadata file not found: '" + meta_path + "'";
      return std::nullopt;
    }

    if (!fs::is_regular_file(meta_path, ec)) {
      error_message =
          "Metadata path is not a regular file: '" + meta_path + "'";
      return std::nullopt;
    }

    sqlite3* db = nullptr;
    const int open_result =
        sqlite3_open_v2(meta_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (open_result != SQLITE_OK) {
      error_message = "Failed to open metadata file '" + meta_path + "': " +
                      std::string(db != nullptr ? sqlite3_errmsg(db)
                                                : "unknown sqlite error");
      if (db != nullptr) {
        sqlite3_close(db);
      }
      return std::nullopt;
    }

    sqlite3_stmt* stmt = nullptr;
    constexpr const char* kSql =
        "SELECT preset, sample_encoding_preset, signal_state_preset, "
        "signal_type "
        "FROM cvbs_file ORDER BY cvbs_file_id LIMIT 1";

    const int prepare_result = sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr);
    if (prepare_result != SQLITE_OK) {
      error_message = "Failed to query cvbs_file metadata from '" + meta_path +
                      "': " + sqlite3_errmsg(db);
      sqlite3_close(db);
      return std::nullopt;
    }

    const int step_result = sqlite3_step(stmt);
    if (step_result != SQLITE_ROW) {
      error_message =
          "Metadata file '" + meta_path + "' does not contain a cvbs_file row";
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return std::nullopt;
    }

    CVBSMetadataRecord record;
    const unsigned char* preset = sqlite3_column_text(stmt, 0);
    const unsigned char* sample_encoding = sqlite3_column_text(stmt, 1);
    const unsigned char* signal_state = sqlite3_column_text(stmt, 2);
    const unsigned char* signal_type = sqlite3_column_text(stmt, 3);

    record.preset =
        preset != nullptr ? reinterpret_cast<const char*>(preset) : "";
    record.sample_encoding_preset =
        sample_encoding != nullptr
            ? reinterpret_cast<const char*>(sample_encoding)
            : "";
    record.signal_state_preset =
        signal_state != nullptr ? reinterpret_cast<const char*>(signal_state)
                                : "";
    record.signal_type = signal_type != nullptr
                             ? reinterpret_cast<const char*>(signal_type)
                             : "";

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return record;
  }

  std::optional<size_t> get_input_word_count(
      const std::string& input_path,
      std::string& error_message) const override {
    std::ifstream input_stream(input_path, std::ios::binary);
    if (!input_stream.is_open()) {
      error_message = "Failed to open CVBS payload file: '" + input_path + "'";
      return std::nullopt;
    }

    input_stream.seekg(0, std::ios::end);
    const std::streamoff size_bytes = input_stream.tellg();
    if (size_bytes <= 0) {
      error_message = "CVBS payload file is empty: '" + input_path + "'";
      return std::nullopt;
    }

    if ((size_bytes % 2) != 0) {
      error_message =
          "CVBS payload file size is not 16-bit aligned: '" + input_path + "'";
      return std::nullopt;
    }

    return static_cast<size_t>(size_bytes / 2);
  }

  bool read_input_words_at(const std::string& input_path, size_t word_offset,
                           size_t word_count, std::vector<uint16_t>& out_words,
                           std::string& error_message) const override {
    std::ifstream input_stream(input_path, std::ios::binary);
    if (!input_stream.is_open()) {
      error_message =
          "Failed to open CVBS payload file for reading: '" + input_path + "'";
      return false;
    }

    const std::streamoff byte_offset =
        static_cast<std::streamoff>(word_offset) * 2;
    input_stream.seekg(byte_offset, std::ios::beg);
    if (!input_stream.good()) {
      error_message =
          "Failed to seek in CVBS payload file: '" + input_path + "'";
      return false;
    }

    out_words.resize(word_count);
    input_stream.read(reinterpret_cast<char*>(out_words.data()),
                      static_cast<std::streamsize>(word_count * 2));
    if (!input_stream.good() && !input_stream.eof()) {
      error_message =
          "Failed to read CVBS payload words from file: '" + input_path + "'";
      return false;
    }

    const auto bytes_read = input_stream.gcount();
    const size_t words_read = static_cast<size_t>(bytes_read) / 2;
    if (words_read < word_count) {
      out_words.resize(words_read);
    }

    return true;
  }
};

inline constexpr CVBSStageIdentity kPALIdentity{
    "PAL_CVBS_Source",
    "PAL CVBS Source",
    "PAL composite input source - loads PAL CVBS 4fsc files",
    VideoFormatCompatibility::PAL_ONLY,
    "PAL",
};

inline constexpr CVBSStageIdentity kNTSCIdentity{
    "NTSC_CVBS_Source",
    "NTSC CVBS Source",
    "NTSC composite input source - loads NTSC CVBS 4fsc files",
    VideoFormatCompatibility::NTSC_ONLY,
    "NTSC",
};

}  // namespace

FixedFormatCVBSSourceStage::FixedFormatCVBSSourceStage(
    CVBSStageIdentity identity, std::shared_ptr<ICVBSSourceStageDeps> deps)
    : identity_(identity),
      input_path_(""),
      use_metadata_(false),
      sample_encoding_("CVBS_U16_4FSC"),
      deps_override_(std::move(deps)) {
  if (!deps_override_) {
    deps_override_ = std::make_shared<CVBSSourceStageDeps>();
  }
}

PALCVBSSourceStage::PALCVBSSourceStage(
    std::shared_ptr<ICVBSSourceStageDeps> deps)
    : FixedFormatCVBSSourceStage(kPALIdentity, std::move(deps)) {}

NTSCCVBSSourceStage::NTSCCVBSSourceStage(
    std::shared_ptr<ICVBSSourceStageDeps> deps)
    : FixedFormatCVBSSourceStage(kNTSCIdentity, std::move(deps)) {}

std::vector<ArtifactPtr> FixedFormatCVBSSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  // Serialize concurrent execute() calls. The GUI thread can reach execute()
  // via RenderCoordinator::mapFieldToImage → get_representation_at_node while
  // the worker thread is also executing, causing a data race on
  // cached_representation_.
  std::lock_guard<std::mutex> execute_lock(execute_mutex_);

  (void)observation_context;  // Unused for now

  // Source stage should have no inputs
  if (!inputs.empty()) {
    throw std::runtime_error(std::string(identity_.stage_name) +
                             " stage should have no inputs");
  }

  // Get input_path parameter
  auto input_path_it = parameters.find("input_path");
  if (input_path_it == parameters.end() ||
      std::get<std::string>(input_path_it->second).empty()) {
    // No file path configured - return empty artifact (0 fields)
    ORC_LOG_DEBUG("{}: No input_path configured, returning empty output",
                  identity_.stage_name);
    return {};
  }
  std::string input_path = std::get<std::string>(input_path_it->second);

  // Return cached representation if the same file is already loaded. A second
  // thread may reach here after blocking on execute_mutex_ while the first
  // thread completed the load.
  if (cached_representation_ && cached_input_path_ == input_path) {
    return {cached_representation_};
  }

  // Get use_metadata parameter
  auto use_metadata_it = parameters.find("use_metadata");
  bool use_metadata = false;
  if (use_metadata_it != parameters.end()) {
    use_metadata = std::get<bool>(use_metadata_it->second);
  }

  // Get sample_encoding parameter (for manual mode)
  std::string sample_encoding = "CVBS_U16_4FSC";
  auto sample_encoding_it = parameters.find("sample_encoding");
  if (sample_encoding_it != parameters.end()) {
    sample_encoding = std::get<std::string>(sample_encoding_it->second);
  }

  std::string file_validation_error;
  if (!deps_override_->validate_input_file(input_path, file_validation_error)) {
    throw UserDataError(file_validation_error);
  }

  std::string resolved_video_standard = identity_.fixed_video_standard;
  std::string resolved_sample_encoding = sample_encoding;

  // Validate parameters based on mode
  if (use_metadata) {
    std::string meta_path = derive_metadata_sidecar_path(input_path);
    std::string validation_error =
        validate_metadata_mode(input_path, meta_path, resolved_sample_encoding);
    if (!validation_error.empty()) {
      throw UserDataError(validation_error);
    }
  } else {
    std::string validation_error = validate_manual_mode(sample_encoding);
    if (!validation_error.empty()) {
      throw UserDataError(validation_error);
    }
  }

  ORC_LOG_INFO("{}: Loading CVBS file: {}", identity_.stage_name, input_path);
  ORC_LOG_DEBUG("  Mode: {}", use_metadata ? "Metadata-driven" : "Manual");
  ORC_LOG_DEBUG("  Video Standard: {}", resolved_video_standard);
  ORC_LOG_DEBUG("  Sample Encoding: {}", resolved_sample_encoding);

  const CVBSGeometry geometry = geometry_for_standard(resolved_video_standard);

  // Step 1: Inspect the file size without loading any sample data.
  std::string word_count_error;
  const auto total_words_opt =
      deps_override_->get_input_word_count(input_path, word_count_error);
  if (!total_words_opt.has_value()) {
    throw UserDataError("Failed to inspect CVBS payload '" + input_path +
                        "': " + word_count_error);
  }
  const size_t total_words = *total_words_opt;

  // Calculate frame sample count accounting for PAL fractional lines.
  size_t frame_samples = 0;
  if (geometry.system == VideoSystem::PAL) {
    frame_samples =
        pal_field_total_samples(true) + pal_field_total_samples(false);
  } else {
    frame_samples = geometry.samples_per_line *
                    (geometry.first_field_lines + geometry.second_field_lines);
  }

  const size_t full_frame_count = total_words / frame_samples;
  const size_t trailing_samples = total_words % frame_samples;

  if (full_frame_count == 0) {
    throw UserDataError("CVBS payload '" + input_path +
                        "' is too short for one complete " +
                        resolved_video_standard + " frame at 4fsc geometry");
  }

  const int32_t active_video_start = geometry.active_video_start;
  const int32_t active_video_end =
      std::min(static_cast<int32_t>(geometry.samples_per_line),
               active_video_start + geometry.active_samples);

  size_t first_field_samples = 0;
  size_t second_field_samples = 0;
  if (geometry.system == VideoSystem::PAL) {
    first_field_samples = pal_field_total_samples(true);
    second_field_samples = pal_field_total_samples(false);
  } else {
    first_field_samples =
        geometry.samples_per_line * geometry.first_field_lines;
    second_field_samples =
        geometry.samples_per_line * geometry.second_field_lines;
  }

  ORC_LOG_DEBUG("{}: Horizontal sync alignment per-line (active {}..{})",
                identity_.stage_name, active_video_start, active_video_end);

  if (trailing_samples > 0) {
    ORC_LOG_WARN(
        "{}: Dropped {} trailing samples from '{}' that do not form a complete "
        "frame",
        identity_.stage_name, trailing_samples, input_path);
  }

  SourceParameters source_parameters;
  source_parameters.system = geometry.system;
  source_parameters.is_subcarrier_locked = true;
  source_parameters.field_width =
      static_cast<int32_t>(geometry.samples_per_line);
  source_parameters.field_height =
      static_cast<int32_t>(calculate_padded_field_height(geometry.system));
  source_parameters.number_of_sequential_fields =
      static_cast<int32_t>(full_frame_count * 2);
  source_parameters.is_first_field_first = true;
  source_parameters.active_video_start = active_video_start;
  source_parameters.active_video_end = active_video_end;
  source_parameters.colour_burst_start = geometry.colour_burst_start;
  source_parameters.colour_burst_end = geometry.colour_burst_end;
  source_parameters.first_active_field_line = geometry.first_active_field_line;
  source_parameters.last_active_field_line = geometry.last_active_field_line;
  source_parameters.first_active_frame_line = geometry.first_active_frame_line;
  source_parameters.last_active_frame_line = geometry.last_active_frame_line;
  source_parameters.blanking_16b_ire = geometry.blanking_ire_16b;
  source_parameters.black_16b_ire = geometry.black_ire_16b;
  source_parameters.white_16b_ire = geometry.white_ire_16b;
  source_parameters.sample_rate = geometry.sample_rate;
  source_parameters.fsc = geometry.fsc;
  source_parameters.is_mapped = false;
  source_parameters.tape_format = "cvbs";
  source_parameters.decoder = "cvbs-source";

  Provenance provenance;
  provenance.stage_name = identity_.stage_name;
  provenance.stage_version = version();
  provenance.parameters = {
      {"input_path", input_path},
      {"video_standard", resolved_video_standard},
      {"sample_encoding", resolved_sample_encoding},
      {"use_metadata", use_metadata ? "true" : "false"},
  };

  auto representation = std::make_shared<CVBSDecodedFieldRepresentation>(
      geometry, deps_override_, input_path, resolved_sample_encoding,
      identity_.stage_name, frame_samples, first_field_samples,
      second_field_samples, full_frame_count, source_parameters,
      ArtifactID(std::string(identity_.stage_name) + ":" + input_path + ":" +
                 resolved_sample_encoding),
      std::move(provenance));

  cached_representation_ = representation;
  cached_input_path_ = input_path;
  return {representation};
}

std::vector<ParameterDescriptor>
FixedFormatCVBSSourceStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType source_type) const {
  (void)project_format;  // Unused - source stages don't need project format
  (void)source_type;     // Unused - source stages define the source type

  std::vector<ParameterDescriptor> descriptors;

  // input_path parameter
  {
    ParameterDescriptor desc;
    desc.name = "input_path";
    desc.display_name = "CVBS File Path";
    desc.description =
        "Path to the CVBS (Composite Video Baseband Signal) data file. "
        "Optional .meta sidecar will be used if use_metadata is enabled.";
    desc.type = ParameterType::FILE_PATH;
    desc.constraints.required = false;
    desc.constraints.default_value = std::string("");
    desc.file_extension_hint = ".composite";
    descriptors.push_back(desc);
  }

  // use_metadata parameter
  {
    ParameterDescriptor desc;
    desc.name = "use_metadata";
    desc.display_name = "Use Metadata";
    desc.description =
        "Enable metadata-driven mode: reads sample encoding from .meta sidecar "
        "file and validates "
        "that metadata matches this stage's fixed video standard.";
    desc.type = ParameterType::BOOL;
    desc.constraints.required = false;
    desc.constraints.default_value = false;
    descriptors.push_back(desc);
  }

  // sample_encoding parameter (manual mode only)
  {
    ParameterDescriptor desc;
    desc.name = "sample_encoding";
    desc.display_name = "Sample Encoding";
    desc.description =
        "Sample encoding preset for manual mode (ignored if use_metadata is "
        "enabled). "
        "Supported: CVBS_U16_4FSC (16-bit unsigned direct encoding), "
        "CVBS_TPG21_4FSC (device-encoded with offset/scale).";
    desc.type = ParameterType::STRING;
    desc.constraints.required = false;
    desc.constraints.default_value = std::string("CVBS_U16_4FSC");
    desc.constraints.allowed_strings = {"CVBS_U16_4FSC", "CVBS_TPG21_4FSC"};
    descriptors.push_back(desc);
  }

  return descriptors;
}

std::map<std::string, ParameterValue>
FixedFormatCVBSSourceStage::get_parameters() const {
  std::map<std::string, ParameterValue> params;
  params["input_path"] = input_path_;
  params["use_metadata"] = use_metadata_;
  params["sample_encoding"] = sample_encoding_;
  return params;
}

bool FixedFormatCVBSSourceStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  for (const auto& [key, value] : params) {
    if (key == "input_path") {
      input_path_ = std::get<std::string>(value);
    } else if (key == "use_metadata") {
      use_metadata_ = std::get<bool>(value);
    } else if (key == "sample_encoding") {
      sample_encoding_ = std::get<std::string>(value);
    } else {
      ORC_LOG_WARN("{}: Unknown parameter: {}", identity_.stage_name, key);
      return false;
    }
  }
  return true;
}

bool FixedFormatCVBSSourceStage::supports_preview() const {
  return cached_representation_ != nullptr;
}

std::vector<PreviewOption> FixedFormatCVBSSourceStage::get_preview_options()
    const {
  std::vector<PreviewOption> options;

  if (!cached_representation_) {
    return options;
  }

  auto video_params = cached_representation_->get_video_parameters();
  if (!video_params) {
    return options;
  }

  const uint64_t field_count = cached_representation_->field_count();
  if (field_count == 0) {
    return options;
  }

  const uint32_t width = video_params->field_width;
  const uint32_t height = video_params->field_height;

  // Calculate DAR correction from active video region (4:3 target)
  double dar_correction = 0.7;
  if (video_params->active_video_start >= 0 &&
      video_params->active_video_end > video_params->active_video_start &&
      video_params->first_active_frame_line >= 0 &&
      video_params->last_active_frame_line >
          video_params->first_active_frame_line) {
    const uint32_t active_width = static_cast<uint32_t>(
        video_params->active_video_end - video_params->active_video_start);
    const uint32_t active_height =
        static_cast<uint32_t>(video_params->last_active_frame_line -
                              video_params->first_active_frame_line);
    const double active_ratio =
        static_cast<double>(active_width) / static_cast<double>(active_height);
    dar_correction = (4.0 / 3.0) / active_ratio;
  }

  options.push_back(PreviewOption{"field", "Field (Clamped)", false, width,
                                  height, field_count, dar_correction});

  options.push_back(PreviewOption{"field_raw", "Field (Raw)", false, width,
                                  height, field_count, dar_correction});

  if (field_count >= 2) {
    const uint64_t pair_count = field_count / 2;

    options.push_back(PreviewOption{"split", "Split (Clamped)", false, width,
                                    height * 2, pair_count, dar_correction});

    options.push_back(PreviewOption{"split_raw", "Split (Raw)", false, width,
                                    height * 2, pair_count, dar_correction});

    options.push_back(PreviewOption{"frame", "Frame (Clamped)", false, width,
                                    height * 2, pair_count, dar_correction});

    options.push_back(PreviewOption{"frame_raw", "Frame (Raw)", false, width,
                                    height * 2, pair_count, dar_correction});
  }

  return options;
}

PreviewImage FixedFormatCVBSSourceStage::render_preview(
    const std::string& option_id, uint64_t index,
    PreviewNavigationHint hint) const {
  (void)hint;
  return PreviewHelpers::render_standard_preview(cached_representation_,
                                                 option_id, index);
}

std::optional<StageReport> FixedFormatCVBSSourceStage::generate_report() const {
  StageReport report;
  report.summary = std::string(identity_.display_name) + " Status";

  if (input_path_.empty()) {
    report.items.push_back({"Source File", "Not configured"});
    report.items.push_back({"Status", "No input file path set"});
    return report;
  }

  report.items.push_back({"Source File", input_path_});
  report.items.push_back(
      {"Mode", use_metadata_ ? "Metadata-driven" : "Manual"});
  report.items.push_back({"Video Standard", identity_.fixed_video_standard});

  if (!use_metadata_) {
    report.items.push_back({"Sample Encoding", sample_encoding_.empty()
                                                   ? "Not set"
                                                   : sample_encoding_});
  }

  if (cached_representation_) {
    auto video_params = cached_representation_->get_video_parameters();
    if (video_params) {
      std::string system_str;
      switch (video_params->system) {
        case VideoSystem::PAL:
          system_str = "PAL";
          break;
        case VideoSystem::NTSC:
          system_str = "NTSC";
          break;
        default:
          system_str = "Unknown";
          break;
      }
      report.items.push_back({"Video System", system_str});
      report.items.push_back(
          {"Field Dimensions", std::to_string(video_params->field_width) +
                                   " x " +
                                   std::to_string(video_params->field_height)});
      report.items.push_back(
          {"Total Fields",
           std::to_string(video_params->number_of_sequential_fields)});
      report.items.push_back(
          {"Total Frames",
           std::to_string(video_params->number_of_sequential_fields / 2)});
      report.items.push_back({"Decoder", video_params->decoder});
      report.items.push_back({"Status", "Loaded"});

      report.metrics["field_count"] =
          static_cast<int64_t>(video_params->number_of_sequential_fields);
      report.metrics["frame_count"] =
          static_cast<int64_t>(video_params->number_of_sequential_fields / 2);
      report.metrics["field_width"] =
          static_cast<int64_t>(video_params->field_width);
      report.metrics["field_height"] =
          static_cast<int64_t>(video_params->field_height);
    } else {
      report.items.push_back({"Status", "Loaded (no video params)"});
    }
  } else {
    report.items.push_back({"Status", "Not yet loaded"});
  }

  return report;
}

std::string FixedFormatCVBSSourceStage::validate_metadata_mode(
    const std::string& input_path, const std::string& meta_path,
    std::string& resolved_sample_encoding) const {
  std::string metadata_error;
  const auto metadata_record =
      deps_override_->load_metadata(meta_path, metadata_error);
  if (!metadata_record.has_value()) {
    return "Failed to load CVBS metadata from '" + meta_path +
           "': " + metadata_error;
  }

  const auto& record = *metadata_record;

  if (record.preset != identity_.fixed_video_standard) {
    return "Unsupported metadata preset '" + record.preset + "' in '" +
           meta_path + "'. This stage requires: " +
           std::string(identity_.fixed_video_standard);
  }

  if (record.sample_encoding_preset != "CVBS_U16_4FSC" &&
      record.sample_encoding_preset != "CVBS_TPG21_4FSC") {
    return "Unsupported metadata sample_encoding_preset '" +
           record.sample_encoding_preset + "' in '" + meta_path +
           "'. Supported encodings: CVBS_U16_4FSC, CVBS_TPG21_4FSC";
  }
  resolved_sample_encoding = record.sample_encoding_preset;

  if (record.signal_state_preset != "STANDARD_TBC_LOCKED") {
    return "Unsupported metadata signal_state_preset '" +
           record.signal_state_preset + "' in '" + meta_path +
           "'. CVBS source requires: STANDARD_TBC_LOCKED";
  }

  if (record.signal_type != "composite") {
    return "Unsupported metadata signal_type '" + record.signal_type +
           "' in '" + meta_path + "'. CVBS source requires: composite";
  }

  (void)input_path;
  return "";
}

std::string FixedFormatCVBSSourceStage::validate_manual_mode(
    const std::string& sample_encoding) const {
  // Validate sample_encoding
  if (sample_encoding != "CVBS_U16_4FSC" &&
      sample_encoding != "CVBS_TPG21_4FSC") {
    return "Invalid sample_encoding '" + sample_encoding +
           "'. Supported: CVBS_U16_4FSC, CVBS_TPG21_4FSC";
  }

  // Signal state is implicitly STANDARD_TBC_LOCKED (validated in Phase 2 for
  // metadata mode)
  return "";
}

}  // namespace orc
