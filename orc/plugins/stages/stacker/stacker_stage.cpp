/*
 * File:        stacker_stage.cpp
 * Module:      orc-core
 * Purpose:     Multi-source CVBS_U10_4FSC frame stacking stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include <logging.h>
#include <stacker_stage.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace orc {

// ============================================================================
// StackedVideoFrameRepresentation
// ============================================================================

StackedVideoFrameRepresentation::StackedVideoFrameRepresentation(
    const std::vector<std::shared_ptr<const VideoFrameRepresentation>>& sources,
    StackerStage* stage)
    : VideoFrameRepresentationWrapper(sources.empty() ? nullptr : sources[0]),
      Artifact(ArtifactID("stacked_frame"), Provenance{}),
      sources_(sources),
      stage_(stage),
      stacked_frames_(kMaxCachedFrames),
      stacked_luma_(kMaxCachedFrames),
      stacked_chroma_(kMaxCachedFrames),
      stacked_dropouts_(kMaxCachedFrames),
      stacked_audio_(kMaxCachedFrames),
      stacked_efm_(kMaxCachedFrames),
      best_source_cache_(kMaxCachedFrames) {
  if (!sources_.empty()) {
    bool first_yc = sources_[0]->has_separate_channels();
    for (size_t i = 1; i < sources_.size(); ++i) {
      if (sources_[i]->has_separate_channels() != first_yc) {
        throw std::runtime_error(
            "StackerStage: all sources must have the same channel mode "
            "(all composite or all YC)");
      }
    }
  }
}

FrameIDRange StackedVideoFrameRepresentation::frame_range() const {
  return source_ ? source_->frame_range() : FrameIDRange{};
}

size_t StackedVideoFrameRepresentation::frame_count() const {
  return source_ ? source_->frame_count() : 0;
}

bool StackedVideoFrameRepresentation::has_frame(FrameID id) const {
  return source_ ? source_->has_frame(id) : false;
}

std::optional<FrameDescriptor>
StackedVideoFrameRepresentation::get_frame_descriptor(FrameID id) const {
  return source_ ? source_->get_frame_descriptor(id) : std::nullopt;
}

bool StackedVideoFrameRepresentation::has_separate_channels() const {
  return sources_.empty() ? false : sources_[0]->has_separate_channels();
}

std::vector<FrameID> StackedVideoFrameRepresentation::collect_source_frame_ids(
    FrameID ref_id) const {
  if (sources_.empty()) return {};

  auto ref_desc = source_->get_frame_descriptor(ref_id);
  int ref_cfi = ref_desc ? ref_desc->colour_frame_index : -1;

  std::vector<FrameID> ids;
  ids.reserve(sources_.size());

  static constexpr FrameID kInvalid = UINT64_MAX;

  for (const auto& src : sources_) {
    if (!src) {
      ids.push_back(kInvalid);
      continue;
    }
    if (!src->has_frame(ref_id)) {
      ids.push_back(kInvalid);
      continue;
    }
    auto desc = src->get_frame_descriptor(ref_id);
    if (desc && desc->is_padding_frame) {
      ids.push_back(kInvalid);
      continue;
    }
    if (ref_cfi < 0) {
      ids.push_back(ref_id);
      continue;
    }

    // Colour-frame-index alignment: search ±4 frames.
    FrameIDRange src_range = src->frame_range();
    FrameID best = kInvalid;

    for (int64_t delta = 0; delta <= 4; ++delta) {
      for (int sign : {0, 1}) {
        int64_t off = (sign == 0) ? delta : -delta;
        int64_t raw = static_cast<int64_t>(ref_id) + off;
        if (raw < 0) { continue; }
        FrameID candidate = static_cast<FrameID>(raw);
        if (candidate < src_range.first || candidate > src_range.last) {
          continue;
        }
        if (!src->has_frame(candidate)) { continue; }
        auto cd = src->get_frame_descriptor(candidate);
        if (!cd || cd->is_padding_frame) { continue; }
        if (cd->colour_frame_index == ref_cfi) {
          best = candidate;
          break;
        }
      }
      if (best != kInvalid) { break; }
    }

    ids.push_back(best);
  }

  return ids;
}

FrameID StackedVideoFrameRepresentation::resolve_source_frame(size_t src_idx,
                                                              FrameID ref_id) const {
  auto ids = collect_source_frame_ids(ref_id);
  if (src_idx >= ids.size()) { return UINT64_MAX; }
  return ids[src_idx];
}

size_t StackedVideoFrameRepresentation::get_source_count(FrameID ref_id) const {
  size_t count = 0;
  for (const auto& src : sources_) {
    if (!src || !src->has_frame(ref_id)) { continue; }
    auto desc = src->get_frame_descriptor(ref_id);
    if (desc && !desc->is_padding_frame) { ++count; }
  }
  return count;
}

size_t StackedVideoFrameRepresentation::get_best_source_index(
    FrameID id) const {
  std::lock_guard<std::mutex> lk(cache_mutex_);
  if (const auto* p = best_source_cache_.get_ptr(id)) { return *p; }

  auto src_ids = collect_source_frame_ids(id);
  size_t best = 0;
  size_t min_dropouts = SIZE_MAX;

  for (size_t i = 0; i < sources_.size() && i < src_ids.size(); ++i) {
    if (src_ids[i] == UINT64_MAX) { continue; }
    if (!sources_[i]) { continue; }
    auto runs = sources_[i]->get_dropout_hints(src_ids[i]);
    size_t total = 0;
    for (const auto& r : runs) { total += r.sample_count; }
    if (total < min_dropouts) {
      min_dropouts = total;
      best = i;
    }
  }

  best_source_cache_.put(id, best);
  return best;
}

void StackedVideoFrameRepresentation::ensure_frame_stacked(FrameID id) const {
  if (stacked_frames_.contains(id) && stacked_dropouts_.contains(id)) { return; }

  ORC_LOG_DEBUG("StackedVideoFrameRepresentation: stacking frame {}", id);

  auto src_ids = collect_source_frame_ids(id);
  std::vector<sample_type> stacked_samples;
  std::vector<DropoutRun> stacked_do;

  stage_->stack_frame(src_ids, sources_, stacked_samples, stacked_do);

  stacked_frames_.put(id, std::move(stacked_samples));
  stacked_dropouts_.put(id, std::move(stacked_do));
}

void StackedVideoFrameRepresentation::ensure_frame_stacked_yc(
    FrameID id) const {
  if (stacked_luma_.contains(id) && stacked_chroma_.contains(id) &&
      stacked_dropouts_.contains(id)) {
    return;
  }

  ORC_LOG_DEBUG("StackedVideoFrameRepresentation: stacking YC frame {}", id);

  auto src_ids = collect_source_frame_ids(id);
  std::vector<sample_type> luma, chroma;
  std::vector<DropoutRun> dos;

  stage_->stack_frame_yc(src_ids, sources_, luma, chroma, dos);

  stacked_luma_.put(id, std::move(luma));
  stacked_chroma_.put(id, std::move(chroma));
  stacked_dropouts_.put(id, std::move(dos));
}

// ── Flat access ──────────────────────────────────────────────────────────────

const VideoFrameRepresentation::sample_type*
StackedVideoFrameRepresentation::get_frame(FrameID id) const {
  std::lock_guard<std::mutex> lk(cache_mutex_);
  ensure_frame_stacked(id);
  const auto* p = stacked_frames_.get_ptr(id);
  return (p && !p->empty()) ? p->data() : nullptr;
}

const VideoFrameRepresentation::sample_type*
StackedVideoFrameRepresentation::get_line(FrameID id, size_t line) const {
  std::lock_guard<std::mutex> lk(cache_mutex_);
  ensure_frame_stacked(id);
  const auto* p = stacked_frames_.get_ptr(id);
  if (!p || p->empty()) { return nullptr; }

  auto desc = source_ ? source_->get_frame_descriptor(id) : std::nullopt;
  if (!desc || line >= desc->height) { return nullptr; }
  size_t stride = desc->samples_per_line_nominal;
  return p->data() + line * stride;
}

std::vector<VideoFrameRepresentation::sample_type>
StackedVideoFrameRepresentation::get_frame_copy(FrameID id) const {
  {
    std::lock_guard<std::mutex> lk(cache_mutex_);
    if (stacked_frames_.contains(id) && stacked_dropouts_.contains(id)) {
      const auto* p = stacked_frames_.get_ptr(id);
      if (p) { return *p; }
    }
  }

  std::vector<sample_type> samples;
  std::vector<DropoutRun> dos;
  auto src_ids = collect_source_frame_ids(id);
  stage_->stack_frame(src_ids, sources_, samples, dos);

  {
    std::lock_guard<std::mutex> lk(cache_mutex_);
    if (!stacked_frames_.contains(id)) {
      stacked_frames_.put(id, samples);
      stacked_dropouts_.put(id, std::move(dos));
    }
  }
  return samples;
}

// ── YC access ────────────────────────────────────────────────────────────────

const VideoFrameRepresentation::sample_type*
StackedVideoFrameRepresentation::get_frame_luma(FrameID id) const {
  if (!has_separate_channels()) { return nullptr; }
  std::lock_guard<std::mutex> lk(cache_mutex_);
  ensure_frame_stacked_yc(id);
  const auto* p = stacked_luma_.get_ptr(id);
  return (p && !p->empty()) ? p->data() : nullptr;
}

const VideoFrameRepresentation::sample_type*
StackedVideoFrameRepresentation::get_frame_chroma(FrameID id) const {
  if (!has_separate_channels()) { return nullptr; }
  std::lock_guard<std::mutex> lk(cache_mutex_);
  ensure_frame_stacked_yc(id);
  const auto* p = stacked_chroma_.get_ptr(id);
  return (p && !p->empty()) ? p->data() : nullptr;
}

const VideoFrameRepresentation::sample_type*
StackedVideoFrameRepresentation::get_line_luma(FrameID id, size_t line) const {
  if (!has_separate_channels()) { return nullptr; }
  std::lock_guard<std::mutex> lk(cache_mutex_);
  ensure_frame_stacked_yc(id);
  const auto* p = stacked_luma_.get_ptr(id);
  if (!p || p->empty()) { return nullptr; }
  auto desc = source_ ? source_->get_frame_descriptor(id) : std::nullopt;
  if (!desc || line >= desc->height) { return nullptr; }
  return p->data() + line * desc->samples_per_line_nominal;
}

const VideoFrameRepresentation::sample_type*
StackedVideoFrameRepresentation::get_line_chroma(FrameID id,
                                                  size_t line) const {
  if (!has_separate_channels()) { return nullptr; }
  std::lock_guard<std::mutex> lk(cache_mutex_);
  ensure_frame_stacked_yc(id);
  const auto* p = stacked_chroma_.get_ptr(id);
  if (!p || p->empty()) { return nullptr; }
  auto desc = source_ ? source_->get_frame_descriptor(id) : std::nullopt;
  if (!desc || line >= desc->height) { return nullptr; }
  return p->data() + line * desc->samples_per_line_nominal;
}

// ── Dropout hints ─────────────────────────────────────────────────────────────

std::vector<DropoutRun> StackedVideoFrameRepresentation::get_dropout_hints(
    FrameID id) const {
  {
    std::lock_guard<std::mutex> lk(cache_mutex_);
    if (stacked_dropouts_.contains(id)) {
      const auto* p = stacked_dropouts_.get_ptr(id);
      if (p) { return *p; }
    }
  }

  get_frame_copy(id);  // populates the dropout cache as a side-effect

  std::lock_guard<std::mutex> lk(cache_mutex_);
  const auto* p = stacked_dropouts_.get_ptr(id);
  return p ? *p : std::vector<DropoutRun>{};
}

// ── Audio ─────────────────────────────────────────────────────────────────────

bool StackedVideoFrameRepresentation::has_audio() const {
  for (const auto& src : sources_) {
    if (src && src->has_audio()) { return true; }
  }
  return false;
}

bool StackedVideoFrameRepresentation::audio_locked() const {
  for (const auto& src : sources_) {
    if (src && src->has_audio()) { return src->audio_locked(); }
  }
  return false;
}

uint32_t StackedVideoFrameRepresentation::get_audio_sample_count(
    FrameID id) const {
  for (const auto& src : sources_) {
    if (src && src->has_audio() && src->has_frame(id)) {
      return src->get_audio_sample_count(id);
    }
  }
  return 0;
}

std::vector<int16_t> StackedVideoFrameRepresentation::get_audio_samples(
    FrameID id) const {
  if (!has_audio()) { return {}; }

  {
    std::lock_guard<std::mutex> lk(cache_mutex_);
    if (const auto* p = stacked_audio_.get_ptr(id)) { return *p; }
  }

  size_t best = get_best_source_index(id);
  auto src_ids = collect_source_frame_ids(id);
  auto result = stage_->stack_audio(src_ids, sources_, best);

  std::lock_guard<std::mutex> lk(cache_mutex_);
  if (!stacked_audio_.contains(id)) { stacked_audio_.put(id, result); }
  return result;
}

// ── EFM ──────────────────────────────────────────────────────────────────────

bool StackedVideoFrameRepresentation::has_efm() const {
  for (const auto& src : sources_) {
    if (src && src->has_efm()) { return true; }
  }
  return false;
}

uint32_t StackedVideoFrameRepresentation::get_efm_sample_count(
    FrameID id) const {
  for (const auto& src : sources_) {
    if (src && src->has_efm() && src->has_frame(id)) {
      return src->get_efm_sample_count(id);
    }
  }
  return 0;
}

std::vector<uint8_t> StackedVideoFrameRepresentation::get_efm_samples(
    FrameID id) const {
  if (!has_efm()) { return {}; }

  {
    std::lock_guard<std::mutex> lk(cache_mutex_);
    if (const auto* p = stacked_efm_.get_ptr(id)) { return *p; }
  }

  size_t best = get_best_source_index(id);
  auto src_ids = collect_source_frame_ids(id);
  auto result = stage_->stack_efm(src_ids, sources_, best);

  std::lock_guard<std::mutex> lk(cache_mutex_);
  if (!stacked_efm_.contains(id)) { stacked_efm_.put(id, result); }
  return result;
}

// ============================================================================
// StackerStage
// ============================================================================

StackerStage::StackerStage() = default;

std::vector<ArtifactPtr> StackerStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  (void)observation_context;

  if (inputs.empty()) {
    throw DAGExecutionError("StackerStage requires at least 1 input");
  }
  if (inputs.size() > 16) {
    throw DAGExecutionError("StackerStage supports maximum 16 inputs");
  }

  if (!parameters.empty()) {
    set_parameters(parameters);
    cached_output_.reset();
  }

  std::vector<std::shared_ptr<const VideoFrameRepresentation>> sources;
  for (const auto& inp : inputs) {
    auto vfr = std::dynamic_pointer_cast<const VideoFrameRepresentation>(inp);
    if (!vfr) {
      throw DAGExecutionError(
          "StackerStage: input is not a VideoFrameRepresentation");
    }
    sources.push_back(vfr);
  }

  bool reuse = false;
  if (cached_output_ && cached_sources_.size() == sources.size()) {
    reuse = true;
    for (size_t i = 0; i < sources.size(); ++i) {
      if (cached_sources_[i] != sources[i]) {
        reuse = false;
        break;
      }
    }
  }

  std::shared_ptr<const VideoFrameRepresentation> result;
  if (reuse) {
    result = cached_output_;
  } else {
    result = process(sources);
    cached_output_ = result;
    cached_sources_ = sources;
  }

  // When process() returns the single input source unchanged (passthrough),
  // the result is already an Artifact via the original input pointer.
  if (sources.size() == 1) {
    return {inputs[0]};
  }

  // Multi-source path: result is a StackedVideoFrameRepresentation which
  // inherits from both VideoFrameRepresentationWrapper and Artifact.
  auto stacked =
      std::const_pointer_cast<StackedVideoFrameRepresentation>(
          std::dynamic_pointer_cast<const StackedVideoFrameRepresentation>(
              result));
  if (!stacked) {
    // Fallback: should not happen
    return {inputs[0]};
  }
  return {stacked};
}

std::shared_ptr<const VideoFrameRepresentation> StackerStage::process(
    const std::vector<std::shared_ptr<const VideoFrameRepresentation>>&
        sources) const {
  if (sources.empty()) { return nullptr; }
  if (sources.size() == 1) { return sources[0]; }

  return std::make_shared<StackedVideoFrameRepresentation>(
      sources, const_cast<StackerStage*>(this));
}

// ── Core stacking ─────────────────────────────────────────────────────────────

void StackerStage::stack_frame(
    const std::vector<FrameID>& source_ids,
    const std::vector<std::shared_ptr<const VideoFrameRepresentation>>& sources,
    std::vector<sample_type>& output_samples,
    std::vector<DropoutRun>& output_dropouts) const {
  if (source_ids.size() != sources.size()) { return; }

  std::optional<FrameDescriptor> ref_desc;
  size_t ref_src = 0;
  for (size_t i = 0; i < source_ids.size(); ++i) {
    if (source_ids[i] == UINT64_MAX || !sources[i]) { continue; }
    ref_desc = sources[i]->get_frame_descriptor(source_ids[i]);
    if (ref_desc) {
      ref_src = i;
      break;
    }
  }
  if (!ref_desc) { return; }

  size_t height = ref_desc->height;
  size_t nominal_width = ref_desc->samples_per_line_nominal;
  size_t total = height * nominal_width;

  auto params = sources[ref_src]->get_video_parameters();
  int32_t black_level = params ? params->black_level : 282;

  output_samples.resize(total, static_cast<sample_type>(black_level));
  output_dropouts.clear();

  std::vector<std::vector<sample_type>> all_frames(sources.size());
  std::vector<bool> frame_valid(sources.size(), false);
  std::vector<std::vector<DropoutRun>> all_dropouts(sources.size());

  for (size_t i = 0; i < sources.size(); ++i) {
    if (source_ids[i] == UINT64_MAX || !sources[i]) { continue; }
    if (!sources[i]->has_frame(source_ids[i])) { continue; }
    auto d = sources[i]->get_frame_descriptor(source_ids[i]);
    if (!d || d->is_padding_frame) { continue; }
    all_frames[i] = sources[i]->get_frame_copy(source_ids[i]);
    if (!all_frames[i].empty()) {
      frame_valid[i] = true;
      all_dropouts[i] = sources[i]->get_dropout_hints(source_ids[i]);
    }
  }

  size_t n_threads = static_cast<size_t>(m_thread_count);
  if (n_threads == 0) {
    n_threads = std::thread::hardware_concurrency();
    if (n_threads == 0) { n_threads = 4; }
  }
  if (n_threads == 1 || height < n_threads * 4) { n_threads = 1; }

  size_t total_do = 0;
  size_t total_stacked = 0;

  if (n_threads == 1) {
    process_lines_range(0, height, nominal_width, all_frames, frame_valid,
                        all_dropouts, sources.size(), black_level,
                        static_cast<int32_t>(nominal_width), output_samples,
                        output_dropouts, total_do, total_stacked);
  } else {
    std::vector<std::thread> threads;
    std::vector<std::vector<DropoutRun>> thread_dos(n_threads);
    std::vector<size_t> thread_do(n_threads, 0);
    std::vector<size_t> thread_st(n_threads, 0);
    size_t lpt = (height + n_threads - 1) / n_threads;

    for (size_t t = 0; t < n_threads; ++t) {
      size_t s = t * lpt;
      size_t e = std::min(s + lpt, height);
      if (s >= height) { break; }
      threads.emplace_back([&, t, s, e]() {
        process_lines_range(s, e, nominal_width, all_frames, frame_valid,
                            all_dropouts, sources.size(), black_level,
                            static_cast<int32_t>(nominal_width), output_samples,
                            thread_dos[t], thread_do[t], thread_st[t]);
      });
    }
    for (auto& th : threads) { th.join(); }
    for (size_t t = 0; t < n_threads; ++t) {
      output_dropouts.insert(output_dropouts.end(), thread_dos[t].begin(),
                             thread_dos[t].end());
      total_do += thread_do[t];
      total_stacked += thread_st[t];
    }
  }

  ORC_LOG_DEBUG("StackerStage::stack_frame: {} dropouts, {} stacked pixels",
                output_dropouts.size(), total_stacked);
  (void)total_do;
}

void StackerStage::stack_frame_yc(
    const std::vector<FrameID>& source_ids,
    const std::vector<std::shared_ptr<const VideoFrameRepresentation>>& sources,
    std::vector<sample_type>& output_luma,
    std::vector<sample_type>& output_chroma,
    std::vector<DropoutRun>& output_dropouts) const {
  if (source_ids.size() != sources.size()) { return; }

  std::optional<FrameDescriptor> ref_desc;
  size_t ref_src = 0;
  for (size_t i = 0; i < source_ids.size(); ++i) {
    if (source_ids[i] == UINT64_MAX || !sources[i]) { continue; }
    ref_desc = sources[i]->get_frame_descriptor(source_ids[i]);
    if (ref_desc) {
      ref_src = i;
      break;
    }
  }
  if (!ref_desc) { return; }

  size_t height = ref_desc->height;
  size_t nominal_width = ref_desc->samples_per_line_nominal;
  size_t total = height * nominal_width;

  auto params = sources[ref_src]->get_video_parameters();
  int32_t black_level = params ? params->black_level : 282;

  output_luma.resize(total, static_cast<sample_type>(black_level));
  output_chroma.resize(total, static_cast<sample_type>(black_level));
  output_dropouts.clear();

  std::vector<std::vector<sample_type>> all_luma(sources.size());
  std::vector<std::vector<sample_type>> all_chroma(sources.size());
  std::vector<bool> frame_valid(sources.size(), false);
  std::vector<std::vector<DropoutRun>> all_dropouts(sources.size());

  for (size_t i = 0; i < sources.size(); ++i) {
    if (source_ids[i] == UINT64_MAX || !sources[i]) { continue; }
    if (!sources[i]->has_frame(source_ids[i])) { continue; }
    auto d = sources[i]->get_frame_descriptor(source_ids[i]);
    if (!d || d->is_padding_frame) { continue; }
    const sample_type* lp = sources[i]->get_frame_luma(source_ids[i]);
    const sample_type* cp = sources[i]->get_frame_chroma(source_ids[i]);
    if (!lp || !cp) { continue; }
    all_luma[i].assign(lp, lp + total);
    all_chroma[i].assign(cp, cp + total);
    frame_valid[i] = true;
    all_dropouts[i] = sources[i]->get_dropout_hints(source_ids[i]);
  }

  size_t n_threads = static_cast<size_t>(m_thread_count);
  if (n_threads == 0) {
    n_threads = std::thread::hardware_concurrency();
    if (n_threads == 0) { n_threads = 4; }
  }
  if (n_threads == 1 || height < n_threads * 4) { n_threads = 1; }

  size_t total_do = 0;
  size_t total_stacked = 0;

  if (n_threads == 1) {
    process_lines_range_yc(0, height, nominal_width, all_luma, all_chroma,
                           frame_valid, all_dropouts, sources.size(),
                           black_level, static_cast<int32_t>(nominal_width),
                           output_luma, output_chroma, output_dropouts,
                           total_do, total_stacked);
  } else {
    std::vector<std::thread> threads;
    std::vector<std::vector<DropoutRun>> thread_dos(n_threads);
    std::vector<size_t> thread_do(n_threads, 0);
    std::vector<size_t> thread_st(n_threads, 0);
    size_t lpt = (height + n_threads - 1) / n_threads;

    for (size_t t = 0; t < n_threads; ++t) {
      size_t s = t * lpt;
      size_t e = std::min(s + lpt, height);
      if (s >= height) { break; }
      threads.emplace_back([&, t, s, e]() {
        process_lines_range_yc(s, e, nominal_width, all_luma, all_chroma,
                               frame_valid, all_dropouts, sources.size(),
                               black_level, static_cast<int32_t>(nominal_width),
                               output_luma, output_chroma, thread_dos[t],
                               thread_do[t], thread_st[t]);
      });
    }
    for (auto& th : threads) { th.join(); }
    for (size_t t = 0; t < n_threads; ++t) {
      output_dropouts.insert(output_dropouts.end(), thread_dos[t].begin(),
                             thread_dos[t].end());
    }
  }
  (void)total_do;
  (void)total_stacked;
}

// ── Per-line processing ───────────────────────────────────────────────────────

namespace {

bool is_sample_dropout(const std::vector<DropoutRun>& runs,
                       size_t line, size_t x, size_t width) {
  uint64_t offset = static_cast<uint64_t>(line * width + x);
  for (const auto& r : runs) {
    if (offset >= r.sample_start &&
        offset < r.sample_start + r.sample_count) {
      return true;
    }
  }
  return false;
}

}  // namespace

void StackerStage::process_lines_range(
    size_t start_line, size_t end_line, size_t width,
    const std::vector<std::vector<sample_type>>& all_frames,
    const std::vector<bool>& frame_valid,
    const std::vector<std::vector<DropoutRun>>& all_dropouts,
    size_t num_sources,
    int32_t black_level,
    int32_t /*nominal_width*/,
    std::vector<sample_type>& output_samples,
    std::vector<DropoutRun>& output_dropouts,
    size_t& total_dropouts,
    size_t& total_stacked) const {
  for (size_t y = start_line; y < end_line; ++y) {
    bool in_dropout = false;
    uint64_t do_start = 0;

    for (size_t x = 0; x < width; ++x) {
      std::vector<int16_t> good_values;
      std::vector<int16_t> dropout_values;
      std::vector<bool> is_do(num_sources, true);

      for (size_t si = 0; si < num_sources; ++si) {
        if (!frame_valid[si]) { continue; }
        size_t off = y * width + x;
        if (off >= all_frames[si].size()) { continue; }
        int16_t val = all_frames[si][off];
        bool do_flag = is_sample_dropout(all_dropouts[si], y, x, width);
        is_do[si] = do_flag;
        if (!do_flag) {
          good_values.push_back(val);
        } else if (!m_no_diff_dod) {
          dropout_values.push_back(val);
        }
      }

      bool all_do = std::all_of(is_do.begin(), is_do.end(),
                                [](bool b) { return b; });

      if (all_do && num_sources >= 3 && !m_no_diff_dod &&
          !dropout_values.empty()) {
        good_values = diff_dod(dropout_values, black_level);
      }

      int16_t stacked;
      if (good_values.empty()) {
        stacked = static_cast<int16_t>(black_level);
        ++total_dropouts;
        if (!in_dropout) {
          do_start = static_cast<uint64_t>(y * width + x);
          in_dropout = true;
        }
      } else {
        stacked = stack_mode(good_values, all_do);
        ++total_stacked;
        if (in_dropout) {
          DropoutRun r;
          r.frame_id = 0;
          r.sample_start = do_start;
          r.sample_count =
              static_cast<uint32_t>(y * width + x - do_start);
          r.severity = 50;
          output_dropouts.push_back(r);
          in_dropout = false;
        }
      }
      output_samples[y * width + x] = stacked;
    }

    if (in_dropout) {
      DropoutRun r;
      r.frame_id = 0;
      r.sample_start = do_start;
      r.sample_count = static_cast<uint32_t>(y * width + width - do_start);
      r.severity = 50;
      output_dropouts.push_back(r);
    }
  }
}

void StackerStage::process_lines_range_yc(
    size_t start_line, size_t end_line, size_t width,
    const std::vector<std::vector<sample_type>>& all_luma,
    const std::vector<std::vector<sample_type>>& all_chroma,
    const std::vector<bool>& frame_valid,
    const std::vector<std::vector<DropoutRun>>& all_dropouts,
    size_t num_sources,
    int32_t black_level,
    int32_t /*nominal_width*/,
    std::vector<sample_type>& output_luma,
    std::vector<sample_type>& output_chroma,
    std::vector<DropoutRun>& output_dropouts,
    size_t& total_dropouts,
    size_t& total_stacked) const {
  for (size_t y = start_line; y < end_line; ++y) {
    bool in_dropout = false;
    uint64_t do_start = 0;

    for (size_t x = 0; x < width; ++x) {
      std::vector<int16_t> good_luma;
      std::vector<int16_t> good_chroma;
      std::vector<bool> is_do(num_sources, true);

      for (size_t si = 0; si < num_sources; ++si) {
        if (!frame_valid[si]) { continue; }
        size_t off = y * width + x;
        if (off >= all_luma[si].size()) { continue; }
        bool do_flag = is_sample_dropout(all_dropouts[si], y, x, width);
        is_do[si] = do_flag;
        if (!do_flag) {
          good_luma.push_back(all_luma[si][off]);
          if (off < all_chroma[si].size()) {
            good_chroma.push_back(all_chroma[si][off]);
          }
        }
      }

      bool all_do = std::all_of(is_do.begin(), is_do.end(),
                                [](bool b) { return b; });

      int16_t out_luma;
      int16_t out_chroma;
      if (good_luma.empty()) {
        out_luma = static_cast<int16_t>(black_level);
        out_chroma = static_cast<int16_t>(black_level);
        ++total_dropouts;
        if (!in_dropout) {
          do_start = static_cast<uint64_t>(y * width + x);
          in_dropout = true;
        }
      } else {
        out_luma = stack_mode(good_luma, all_do);
        out_chroma = good_chroma.empty()
                         ? static_cast<int16_t>(black_level)
                         : stack_mode(good_chroma, all_do);
        ++total_stacked;
        if (in_dropout) {
          DropoutRun r;
          r.frame_id = 0;
          r.sample_start = do_start;
          r.sample_count =
              static_cast<uint32_t>(y * width + x - do_start);
          r.severity = 50;
          output_dropouts.push_back(r);
          in_dropout = false;
        }
      }

      size_t off = y * width + x;
      output_luma[off] = out_luma;
      output_chroma[off] = out_chroma;
    }

    if (in_dropout) {
      DropoutRun r;
      r.frame_id = 0;
      r.sample_start = do_start;
      r.sample_count = static_cast<uint32_t>(y * width + width - do_start);
      r.severity = 50;
      output_dropouts.push_back(r);
    }
  }
}

// ── Stacking mode ─────────────────────────────────────────────────────────────

int16_t StackerStage::stack_mode(const std::vector<int16_t>& values,
                                  bool /*all_dropout*/) const {
  if (values.empty()) { return 0; }

  int32_t mode = m_mode;
  if (mode == -1) { mode = (static_cast<int32_t>(values.size()) >= 3) ? 2 : 0; }

  switch (mode) {
    case 0:
      return static_cast<int16_t>(compute_mean(values));
    case 1:
      return compute_median(values);
    case 2: {
      int16_t med = compute_median(values);
      int32_t sum = 0;
      size_t count = 0;
      for (int16_t v : values) {
        if (std::abs(static_cast<int32_t>(v) - med) < m_smart_threshold) {
          sum += v;
          ++count;
        }
      }
      if (count == 0) { return med; }
      return static_cast<int16_t>(sum / static_cast<int32_t>(count));
    }
    default:
      return compute_median(values);
  }
}

int16_t StackerStage::compute_median(std::vector<int16_t> v) const {
  if (v.empty()) { return 0; }
  size_t n = v.size();
  std::sort(v.begin(), v.end());
  if (n % 2 == 0) {
    return static_cast<int16_t>(
        (static_cast<int32_t>(v[n / 2 - 1]) + v[n / 2]) / 2);
  }
  return v[n / 2];
}

int32_t StackerStage::compute_mean(const std::vector<int16_t>& v) const {
  if (v.empty()) { return 0; }
  int64_t sum = 0;
  for (int16_t s : v) { sum += s; }
  return static_cast<int32_t>(sum / static_cast<int64_t>(v.size()));
}

std::vector<int16_t> StackerStage::diff_dod(const std::vector<int16_t>& input,
                                             int32_t /*black_level*/) const {
  if (input.size() < 3) { return {}; }
  std::vector<int16_t> cp(input);
  int16_t med = compute_median(cp);
  std::vector<int16_t> result;
  for (int16_t v : input) {
    if (std::abs(static_cast<int32_t>(v) - med) < 500) { result.push_back(v); }
  }
  return result;
}

// ── Audio / EFM ──────────────────────────────────────────────────────────────

std::vector<int16_t> StackerStage::stack_audio(
    const std::vector<FrameID>& source_ids,
    const std::vector<std::shared_ptr<const VideoFrameRepresentation>>& sources,
    size_t best_src) const {
  if (m_audio_stacking_mode == AudioStackingMode::DISABLED) {
    if (best_src < sources.size() && source_ids[best_src] != UINT64_MAX &&
        sources[best_src] && sources[best_src]->has_audio()) {
      return sources[best_src]->get_audio_samples(source_ids[best_src]);
    }
    return {};
  }

  // Free-running audio: pass from first unlocked source unchanged.
  for (size_t i = 0; i < sources.size(); ++i) {
    if (source_ids[i] == UINT64_MAX || !sources[i]) { continue; }
    if (sources[i]->has_audio() && !sources[i]->audio_locked()) {
      return sources[i]->get_audio_samples(source_ids[i]);
    }
  }

  // Locked audio: collect and stack.
  std::vector<std::vector<int16_t>> all_audio;
  for (size_t i = 0; i < sources.size(); ++i) {
    if (source_ids[i] == UINT64_MAX || !sources[i]) { continue; }
    if (!sources[i]->has_audio() || !sources[i]->has_frame(source_ids[i])) {
      continue;
    }
    auto s = sources[i]->get_audio_samples(source_ids[i]);
    if (!s.empty()) { all_audio.push_back(std::move(s)); }
  }

  if (all_audio.empty()) { return {}; }
  if (all_audio.size() == 1) { return all_audio[0]; }

  size_t n = all_audio[0].size();
  std::vector<int16_t> result(n);
  for (size_t si = 0; si < n; ++si) {
    std::vector<int16_t> vals;
    for (const auto& a : all_audio) {
      if (si < a.size()) { vals.push_back(a[si]); }
    }
    if (m_audio_stacking_mode == AudioStackingMode::MEDIAN) {
      result[si] = audio_median(vals);
    } else {
      result[si] = audio_mean(vals);
    }
  }
  return result;
}

std::vector<uint8_t> StackerStage::stack_efm(
    const std::vector<FrameID>& source_ids,
    const std::vector<std::shared_ptr<const VideoFrameRepresentation>>& sources,
    size_t best_src) const {
  if (m_efm_stacking_mode == EFMStackingMode::DISABLED) {
    if (best_src < sources.size() && source_ids[best_src] != UINT64_MAX &&
        sources[best_src] && sources[best_src]->has_efm()) {
      return sources[best_src]->get_efm_samples(source_ids[best_src]);
    }
    return {};
  }

  std::vector<std::vector<uint8_t>> all_efm;
  for (size_t i = 0; i < sources.size(); ++i) {
    if (source_ids[i] == UINT64_MAX || !sources[i]) { continue; }
    if (!sources[i]->has_efm() || !sources[i]->has_frame(source_ids[i])) {
      continue;
    }
    auto s = sources[i]->get_efm_samples(source_ids[i]);
    if (!s.empty()) { all_efm.push_back(std::move(s)); }
  }

  if (all_efm.empty()) { return {}; }
  if (all_efm.size() == 1) { return all_efm[0]; }

  size_t n = all_efm[0].size();
  std::vector<uint8_t> result(n);
  for (size_t si = 0; si < n; ++si) {
    std::vector<uint8_t> vals;
    for (const auto& e : all_efm) {
      if (si < e.size()) { vals.push_back(e[si]); }
    }
    if (m_efm_stacking_mode == EFMStackingMode::MEDIAN) {
      result[si] = efm_median(vals);
    } else {
      result[si] = efm_mean(vals);
    }
  }
  return result;
}

int16_t StackerStage::audio_mean(const std::vector<int16_t>& v) const {
  if (v.empty()) { return 0; }
  int64_t s = 0;
  for (int16_t x : v) { s += x; }
  return static_cast<int16_t>(s / static_cast<int64_t>(v.size()));
}

int16_t StackerStage::audio_median(std::vector<int16_t> v) const {
  if (v.empty()) { return 0; }
  size_t n = v.size();
  std::nth_element(v.begin(),
                   v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
  if (n % 2 == 0) {
    std::nth_element(v.begin(),
                     v.begin() + static_cast<std::ptrdiff_t>((n - 1) / 2),
                     v.end());
    return static_cast<int16_t>(
        (static_cast<int32_t>(v[(n - 1) / 2]) + v[n / 2]) / 2);
  }
  return v[n / 2];
}

uint8_t StackerStage::efm_mean(const std::vector<uint8_t>& v) const {
  if (v.empty()) { return 0; }
  uint32_t s = 0;
  for (uint8_t x : v) { s += x; }
  return static_cast<uint8_t>(s / static_cast<uint32_t>(v.size()));
}

uint8_t StackerStage::efm_median(std::vector<uint8_t> v) const {
  if (v.empty()) { return 0; }
  size_t n = v.size();
  std::nth_element(v.begin(),
                   v.begin() + static_cast<std::ptrdiff_t>(n / 2), v.end());
  if (n % 2 == 0) {
    std::nth_element(v.begin(),
                     v.begin() + static_cast<std::ptrdiff_t>((n - 1) / 2),
                     v.end());
    return static_cast<uint8_t>(
        (static_cast<uint16_t>(v[(n - 1) / 2]) + v[n / 2]) / 2);
  }
  return v[n / 2];
}

// ── Parameters ────────────────────────────────────────────────────────────────

std::vector<ParameterDescriptor> StackerStage::get_parameter_descriptors(
    VideoSystem /*project_format*/, SourceType /*source_type*/) const {
  std::vector<ParameterDescriptor> d;

  d.push_back({"mode", "Stacking Mode", "Algorithm for combining sources",
                ParameterType::STRING,
                ParameterConstraints{std::nullopt, std::nullopt,
                                     ParameterValue{std::string("Auto")},
                                     {"Auto", "Mean", "Median", "Smart Mean",
                                      "Smart Neighbor", "Neighbor"},
                                     false, std::nullopt}});
  d.push_back({"smart_threshold", "Smart Threshold",
                "Threshold for Smart Mean mode (0-128, default 15)",
                ParameterType::INT32,
                ParameterConstraints{ParameterValue{static_cast<int32_t>(0)},
                                     ParameterValue{static_cast<int32_t>(128)},
                                     ParameterValue{static_cast<int32_t>(15)},
                                     {},
                                     false,
                                     std::nullopt}});
  d.push_back({"no_diff_dod", "Disable Differential Dropout Detection",
                "Strictly trust dropout markings; disable diff_dod recovery",
                ParameterType::BOOL,
                ParameterConstraints{std::nullopt, std::nullopt,
                                     ParameterValue{false},
                                     {},
                                     false,
                                     std::nullopt}});
  d.push_back({"passthrough", "Passthrough Universal Dropouts",
                "Preserve dropout regions present in all sources",
                ParameterType::BOOL,
                ParameterConstraints{std::nullopt, std::nullopt,
                                     ParameterValue{false},
                                     {},
                                     false,
                                     std::nullopt}});
  d.push_back({"audio_stacking", "Audio Stacking Mode",
                "How to combine audio: Disabled | Mean | Median",
                ParameterType::STRING,
                ParameterConstraints{std::nullopt, std::nullopt,
                                     ParameterValue{std::string("Mean")},
                                     {"Disabled", "Mean", "Median"},
                                     false,
                                     std::nullopt}});
  d.push_back({"efm_stacking", "EFM Stacking Mode",
                "How to combine EFM t-values: Disabled | Mean | Median",
                ParameterType::STRING,
                ParameterConstraints{std::nullopt, std::nullopt,
                                     ParameterValue{std::string("Mean")},
                                     {"Disabled", "Mean", "Median"},
                                     false,
                                     std::nullopt}});
  return d;
}

std::map<std::string, ParameterValue> StackerStage::get_parameters() const {
  const char* mode_names[] = {"Auto", "Mean", "Median",
                               "Smart Mean", "Smart Neighbor", "Neighbor"};
  int mi = m_mode + 1;
  if (mi < 0 || mi > 5) { mi = 0; }

  const char* audio_names[] = {"Disabled", "Mean", "Median"};
  const char* efm_names[] = {"Disabled", "Mean", "Median"};

  return {{"mode", ParameterValue{std::string(mode_names[mi])}},
          {"smart_threshold", ParameterValue{m_smart_threshold}},
          {"no_diff_dod", ParameterValue{m_no_diff_dod}},
          {"passthrough", ParameterValue{m_passthrough}},
          {"audio_stacking",
           ParameterValue{std::string(
               audio_names[static_cast<int>(m_audio_stacking_mode)])}},
          {"efm_stacking",
           ParameterValue{std::string(
               efm_names[static_cast<int>(m_efm_stacking_mode)])}}};
}

bool StackerStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  parameters_ = params;

  for (const auto& [key, value] : params) {
    if (key == "mode") {
      if (const auto* v = std::get_if<std::string>(&value)) {
        if (*v == "Auto") {
          m_mode = -1;
        } else if (*v == "Mean") {
          m_mode = 0;
        } else if (*v == "Median") {
          m_mode = 1;
        } else if (*v == "Smart Mean") {
          m_mode = 2;
        } else if (*v == "Smart Neighbor") {
          m_mode = 3;
        } else if (*v == "Neighbor") {
          m_mode = 4;
        } else {
          ORC_LOG_WARN("StackerStage: unknown mode '{}'", *v);
          return false;
        }
      } else if (const auto* iv = std::get_if<int32_t>(&value)) {
        if (*iv < -1 || *iv > 4) { return false; }
        m_mode = *iv;
      } else {
        return false;
      }
    } else if (key == "smart_threshold") {
      if (const auto* v = std::get_if<int32_t>(&value)) {
        if (*v < 0 || *v > 128) { return false; }
        m_smart_threshold = *v;
      } else {
        return false;
      }
    } else if (key == "no_diff_dod") {
      if (const auto* v = std::get_if<bool>(&value)) {
        m_no_diff_dod = *v;
      } else {
        return false;
      }
    } else if (key == "passthrough") {
      if (const auto* v = std::get_if<bool>(&value)) {
        m_passthrough = *v;
      } else {
        return false;
      }
    } else if (key == "audio_stacking") {
      if (const auto* v = std::get_if<std::string>(&value)) {
        if (*v == "Disabled") {
          m_audio_stacking_mode = AudioStackingMode::DISABLED;
        } else if (*v == "Mean") {
          m_audio_stacking_mode = AudioStackingMode::MEAN;
        } else if (*v == "Median") {
          m_audio_stacking_mode = AudioStackingMode::MEDIAN;
        } else {
          ORC_LOG_WARN("StackerStage: unknown audio_stacking '{}'", *v);
          return false;
        }
      } else {
        return false;
      }
    } else if (key == "efm_stacking") {
      if (const auto* v = std::get_if<std::string>(&value)) {
        if (*v == "Disabled") {
          m_efm_stacking_mode = EFMStackingMode::DISABLED;
        } else if (*v == "Mean") {
          m_efm_stacking_mode = EFMStackingMode::MEAN;
        } else if (*v == "Median") {
          m_efm_stacking_mode = EFMStackingMode::MEDIAN;
        } else {
          ORC_LOG_WARN("StackerStage: unknown efm_stacking '{}'", *v);
          return false;
        }
      } else {
        return false;
      }
    }
  }
  return true;
}

// ── Report / Preview ──────────────────────────────────────────────────────────

std::optional<StageReport> StackerStage::generate_report() const {
  StageReport r;
  r.summary = "Stacker Configuration";

  const char* mode_names[] = {"Auto", "Mean", "Median",
                               "Smart Mean", "Smart Neighbor", "Neighbor"};
  int mi = m_mode + 1;
  if (mi < 0 || mi > 5) { mi = 0; }

  const char* stack_names[] = {"Disabled", "Mean", "Median"};
  r.items.push_back({"Stacking Mode", mode_names[mi]});
  r.items.push_back({"Smart Threshold", std::to_string(m_smart_threshold)});
  r.items.push_back({"Differential Dropout Detection",
                      m_no_diff_dod ? "Disabled" : "Enabled"});
  r.items.push_back({"Dropout Passthrough",
                      m_passthrough ? "Enabled" : "Disabled"});
  r.items.push_back(
      {"Audio Stacking",
       stack_names[static_cast<int>(m_audio_stacking_mode)]});
  r.items.push_back(
      {"EFM Stacking",
       stack_names[static_cast<int>(m_efm_stacking_mode)]});

  r.metrics["mode"] = static_cast<int64_t>(m_mode);
  r.metrics["smart_threshold"] = static_cast<int64_t>(m_smart_threshold);
  r.metrics["audio_stacking_mode"] =
      static_cast<int64_t>(m_audio_stacking_mode);
  r.metrics["efm_stacking_mode"] = static_cast<int64_t>(m_efm_stacking_mode);
  return r;
}

namespace {

PreviewImage render_stacker_grayscale(const VideoFrameRepresentation& vfr,
                                      FrameID frame_id, bool apply_scaling) {
  auto desc = vfr.get_frame_descriptor(frame_id);
  auto params = vfr.get_video_parameters();
  if (!desc || !params) { return PreviewImage{0, 0, {}, {}, {}}; }

  const size_t height = desc->height;
  const size_t width = static_cast<size_t>(params->frame_width_nominal);
  if (height == 0 || width == 0) { return PreviewImage{0, 0, {}, {}, {}}; }

  const int32_t blanking = params->blanking_level;
  const int32_t white = params->white_level;
  const int32_t range = (white > blanking) ? (white - blanking) : 1;

  PreviewImage img;
  img.width = static_cast<uint32_t>(width);
  img.height = static_cast<uint32_t>(height);
  img.rgb_data.reserve(width * height * 3);

  for (size_t line = 0; line < height; ++line) {
    const int16_t* ptr = vfr.get_line(frame_id, line);
    for (size_t s = 0; s < width; ++s) {
      const int32_t raw = ptr ? static_cast<int32_t>(ptr[s]) : blanking;
      uint8_t grey;
      if (apply_scaling) {
        grey = static_cast<uint8_t>(
            std::clamp((raw - blanking) * 255 / range, 0, 255));
      } else {
        grey = static_cast<uint8_t>(std::clamp(raw * 255 / 1023, 0, 255));
      }
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
    }
  }
  return img;
}

}  // namespace

std::vector<PreviewOption> StackerStage::get_preview_options() const {
  if (!cached_output_) { return {}; }

  auto params = cached_output_->get_video_parameters();
  const size_t fc = cached_output_->frame_count();
  if (fc == 0 || !params.has_value()) { return {}; }

  const uint32_t w = static_cast<uint32_t>(params->frame_width_nominal);
  const uint32_t h = static_cast<uint32_t>(params->frame_height);

  double dar = 0.7;
  if (params->active_video_start >= 0 &&
      params->active_video_end > params->active_video_start &&
      params->first_active_frame_line >= 0 &&
      params->last_active_frame_line > params->first_active_frame_line) {
    const double aw = static_cast<double>(params->active_video_end -
                                          params->active_video_start);
    const double ah = static_cast<double>(params->last_active_frame_line -
                                          params->first_active_frame_line);
    dar = (4.0 / 3.0) / (aw / ah);
  }

  return {
      PreviewOption{"frame", "Frame (Scaled)", false, w, h,
                    static_cast<uint64_t>(fc), dar},
      PreviewOption{"frame_raw", "Frame (Raw)", false, w, h,
                    static_cast<uint64_t>(fc), dar},
  };
}

PreviewImage StackerStage::render_preview(const std::string& option_id,
                                          uint64_t index,
                                          PreviewNavigationHint /*hint*/) const {
  if (!cached_output_) { return PreviewImage{0, 0, {}, {}, {}}; }
  const FrameID fid = static_cast<FrameID>(index);
  if (!cached_output_->has_frame(fid)) { return PreviewImage{0, 0, {}, {}, {}}; }

  bool apply_scaling = (option_id == "frame");
  return render_stacker_grayscale(*cached_output_, fid, apply_scaling);
}

}  // namespace orc
