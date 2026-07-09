/*
 * File:        tbc_source_stage.cpp
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     Unified TBC source stage — PAL/NTSC/PAL_M TBC file loading
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "tbc_source_stage.h"

#include <orc/stage/cvbs_signal_constants.h>
#include <orc/stage/dropout_util.h>
#include <orc/stage/error_types.h>
#include <orc/stage/frame_line_util.h>
#include <orc/stage/logging.h>
#include <orc/stage/lru_cache.h>
#include <orc/stage/preview_helpers.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "audio_resampler.h"
#include "ntsc_tbc_converter.h"
#include "ntsc_tbc_yc_converter.h"
#include "pal_m_tbc_converter.h"
#include "pal_m_tbc_yc_converter.h"
#include "pal_tbc_converter.h"
#include "pal_tbc_yc_converter.h"
#include "tbc_metadata_json_reader.h"
#include "tbc_metadata_reader.h"
#include "tbc_metadata_types.h"
#include "tbc_reader.h"

namespace orc {

namespace {

// Build SourceParameters from TBCVideoParams using spec-defined CVBS levels.
SourceParameters build_source_params(const TBCVideoParams& tvp,
                                     int32_t frame_count) {
  SourceParameters sp;
  sp.system = tvp.system;
  sp.number_of_sequential_frames = frame_count;
  sp.is_widescreen = tvp.is_widescreen;
  sp.decoder = tvp.decoder;
  sp.tape_format = tvp.tape_format;
  sp.git_branch = tvp.git_branch;
  sp.git_commit = tvp.git_commit;

  switch (tvp.system) {
    case VideoSystem::PAL:
      // EBU Tech. 3280-E §1.1 level constants.
      sp.frame_width_nominal = kPalSamplesPerLineNominal;
      sp.frame_height = kPalFrameLines;
      sp.sync_tip_level = kPalSyncTip;
      sp.blanking_level = kPalBlanking;
      sp.black_level = kPalBlack;
      sp.white_level = kPalWhite;
      sp.peak_level = kPalPeak;
      sp.active_video_start = kPalActiveVideoStart;
      sp.active_video_end = kPalActiveVideoEnd;
      sp.first_active_frame_line = kPalFirstActiveFrameLine;
      sp.last_active_frame_line = kPalLastActiveFrameLine;
      break;
    case VideoSystem::NTSC:
      // SMPTE 244M-2003 level constants.
      sp.frame_width_nominal = kNtscSamplesPerLine;
      sp.frame_height = kNtscFrameLines;
      sp.sync_tip_level = kNtscSyncTip;
      sp.blanking_level = kNtscBlanking;
      sp.black_level = kNtscBlack;
      sp.white_level = kNtscWhite;
      sp.peak_level = kNtscPeak;
      sp.active_video_start = kNtscActiveVideoStart;
      sp.active_video_end = kNtscActiveVideoEnd;
      sp.first_active_frame_line = kNtscFirstActiveFrameLine;
      sp.last_active_frame_line = kNtscLastActiveFrameLine;
      if (tvp.ntsc_j_black_level_16b.has_value()) {
        // NTSC-J: convert 16-bit TBC domain to CVBS 10-bit.
        const double n =
            static_cast<double>(tvp.ntsc_j_black_level_16b.value() -
                                tvp.blanking_16b) /
            static_cast<double>(tvp.white_16b - tvp.blanking_16b);
        const int32_t cvbs_j = static_cast<int32_t>(
            n * (kNtscWhite - kNtscBlanking) + kNtscBlanking);
        sp.black_level = cvbs_j;
        sp.has_nonstandard_values = true;
      }
      break;
    case VideoSystem::PAL_M:
      // ITU-R BT.1700-1 Annex 1 Part B: PAL_M uses NTSC signal levels.
      sp.frame_width_nominal = kPalMSamplesPerLine;
      sp.frame_height = kPalMFrameLines;
      sp.sync_tip_level = kNtscSyncTip;
      sp.blanking_level = kNtscBlanking;
      sp.black_level = kNtscBlack;
      sp.white_level = kNtscWhite;
      sp.peak_level = kNtscPeak;
      sp.active_video_start = kNtscActiveVideoStart;
      sp.active_video_end = kNtscActiveVideoEnd;
      sp.first_active_frame_line = kNtscFirstActiveFrameLine;
      sp.last_active_frame_line = kNtscLastActiveFrameLine;
      break;
    default:
      break;
  }

  // Compute chroma DC offset in CVBS domain: the raw .tbcc chroma is centred
  // at 32768 (uint16 midpoint); map that to CVBS using the same formula as
  // tbc_to_cvbs so the Y+C combined view can remove the DC before summing.
  if (tvp.blanking_16b > 0 && tvp.white_16b > tvp.blanking_16b &&
      sp.blanking_level >= 0 && sp.white_level > sp.blanking_level) {
    const double n = (32768.0 - static_cast<double>(tvp.blanking_16b)) /
                     static_cast<double>(tvp.white_16b - tvp.blanking_16b);
    const double cvbs =
        n * static_cast<double>(sp.white_level - sp.blanking_level) +
        static_cast<double>(sp.blanking_level);
    sp.chroma_dc_offset = static_cast<int32_t>(std::lround(cvbs));
  }

  return sp;
}

// Resolve display name: "<System> TBC <Composite|YC>"
std::string make_display_name(VideoSystem system, bool is_yc) {
  const std::string sys =
      (system == VideoSystem::Unknown) ? "TBC" : video_system_to_string(system);
  return sys + " TBC " + (is_yc ? "YC" : "Composite");
}

// ---------------------------------------------------------------------------
// TBCDecodedFrameRepresentation
// ---------------------------------------------------------------------------
// Implements VideoFrameRepresentation backed by a pair of ld-decode TBC fields
// assembled into CVBS_U10_4FSC frames.  Currently PAL composite and YC are
// fully implemented.  NTSC and PAL_M are reserved for Phase 5.
class TBCDecodedFrameRepresentation final : public VideoFrameRepresentation,
                                            public Artifact {
 public:
  TBCDecodedFrameRepresentation(
      TBCVideoParams video_params, SourceParameters source_params,
      std::vector<TBCFieldMeta> field_meta,
      std::shared_ptr<ITBCSourceStageDeps> deps,
      std::string tbc_path,  // composite .tbc (or Y .tbc for YC)
      std::string c_path,    // chroma .tbc for YC mode; empty for composite
      std::string pcm_path, std::string efm_bin_path, std::string ac3_bin_path,
      std::string ac3_meta_path, bool has_audio, bool has_efm, bool has_ac3,
      ArtifactID artifact_id, Provenance provenance)
      : Artifact(std::move(artifact_id), std::move(provenance)),
        video_params_(std::move(video_params)),
        source_params_(std::move(source_params)),
        field_meta_(std::move(field_meta)),
        deps_(std::move(deps)),
        tbc_path_(std::move(tbc_path)),
        c_path_(std::move(c_path)),
        pcm_path_(std::move(pcm_path)),
        efm_bin_path_(std::move(efm_bin_path)),
        ac3_bin_path_(std::move(ac3_bin_path)),
        ac3_meta_path_(std::move(ac3_meta_path)),
        has_audio_(has_audio),
        has_efm_(has_efm),
        has_ac3_(has_ac3),
        is_yc_(!c_path_.empty()) {
    // Pre-compute per-frame audio and EFM offsets from field metadata (cheap,
    // metadata-only, no disk I/O).  The NTSC/PAL_M audio resample is deferred
    // to first audio access (see ensure_ntsc_palM_audio) so it never runs on
    // the source-execute / preview hot path — doing it eagerly here read the
    // whole PCM and ran a full SoXR HQ pass, stalling the render worker on long
    // NTSC sources even though video preview never needs audio (issue #209).
    compute_audio_offsets();
    compute_efm_offsets();
  }

  // --------------------------------------------------------------------------
  // Artifact
  // --------------------------------------------------------------------------
  std::string type_name() const override {
    return "TBCDecodedFrameRepresentation";
  }

  // --------------------------------------------------------------------------
  // Navigation
  // --------------------------------------------------------------------------
  FrameIDRange frame_range() const override {
    if (frame_count() == 0) return FrameIDRange{0, 0};
    return FrameIDRange{0, static_cast<FrameID>(frame_count() - 1)};
  }

  size_t frame_count() const override {
    return static_cast<size_t>(video_params_.number_of_fields / 2);
  }

  bool has_frame(FrameID id) const override {
    return id < static_cast<FrameID>(frame_count());
  }

  std::optional<FrameDescriptor> get_frame_descriptor(
      FrameID id) const override {
    if (!has_frame(id)) return std::nullopt;
    // All descriptor fields come from pre-loaded metadata — no disk read
    // needed. colour_frame_index is derived from field_meta_ via
    // compute_colour_frame_index.
    FrameDescriptor desc;
    desc.frame_id = id;
    desc.system = video_params_.system;
    desc.height = static_cast<size_t>(source_params_.frame_height);
    desc.samples_total = static_cast<size_t>(frame_samples_total());
    desc.samples_per_line_nominal =
        static_cast<size_t>(source_params_.frame_width_nominal);
    desc.colour_frame_index = compute_colour_frame_index(id);
    if (video_params_.ntsc_j_black_level_16b.has_value()) {
      desc.black_level_override = source_params_.black_level;
    }
    return desc;
  }

  // --------------------------------------------------------------------------
  // Flat sample access
  // --------------------------------------------------------------------------
  const sample_type* get_frame(FrameID id) const override {
    if (!has_frame(id)) return nullptr;
    ensure_frame_cached(id);
    const CachedFrame* cf = frame_cache_.get_ptr(id);
    return cf ? cf->samples.data() : nullptr;
  }

  std::vector<sample_type> get_frame_copy(FrameID id) const override {
    if (!has_frame(id)) return {};
    const sample_type* ptr = get_frame(id);
    if (!ptr) return {};
    return std::vector<sample_type>(ptr, ptr + frame_samples_total());
  }

  // --------------------------------------------------------------------------
  // YC channels
  // --------------------------------------------------------------------------
  bool has_separate_channels() const override { return is_yc_; }

  const sample_type* get_frame_luma(FrameID id) const override {
    if (!is_yc_ || !has_frame(id)) return nullptr;
    ensure_frame_cached(id);
    const CachedFrame* cf = frame_cache_.get_ptr(id);
    return (cf && !cf->luma.empty()) ? cf->luma.data() : nullptr;
  }

  const sample_type* get_frame_chroma(FrameID id) const override {
    if (!is_yc_ || !has_frame(id)) return nullptr;
    ensure_frame_cached(id);
    const CachedFrame* cf = frame_cache_.get_ptr(id);
    return (cf && !cf->chroma.empty()) ? cf->chroma.data() : nullptr;
  }

  const sample_type* get_line_luma(FrameID id, size_t line) const override {
    const auto* ptr = get_frame_luma(id);
    if (!ptr) return nullptr;
    const auto params = get_video_parameters();
    if (!params) return nullptr;
    return ptr + line * static_cast<size_t>(params->frame_width_nominal);
  }

  const sample_type* get_line_chroma(FrameID id, size_t line) const override {
    const auto* ptr = get_frame_chroma(id);
    if (!ptr) return nullptr;
    const auto params = get_video_parameters();
    if (!params) return nullptr;
    return ptr + line * static_cast<size_t>(params->frame_width_nominal);
  }

  // --------------------------------------------------------------------------
  // Hints
  // --------------------------------------------------------------------------
  std::vector<DropoutRun> get_dropout_hints(FrameID id) const override {
    if (!has_frame(id)) return {};

    const VideoSystem sys = video_params_.system;
    const size_t tbc_f1_idx = static_cast<size_t>(id) * 2;
    const size_t tbc_f2_idx = tbc_f1_idx + 1;

    std::vector<DropoutRun> result;

    // PAL/NTSC/PAL_M: even index (TBC field 1, isFirstField) → VFR field 1
    // (top).
    const int32_t f1_vfr = 1;
    const int32_t f2_vfr = 2;

    // Upper bound on a valid field-relative line index. Reject out-of-range
    // lines (including a -1 produced by an upstream underflow) before feeding
    // them to the frame-geometry math — an out-of-range line yields a
    // pathological ~2^64 sample offset that hangs the preview render worker
    // (issue #209).
    const int32_t max_field_lines =
        (sys == VideoSystem::PAL)     ? kPalField1Lines
        : (sys == VideoSystem::NTSC)  ? kNtscField1Lines
        : (sys == VideoSystem::PAL_M) ? kPalMField1Lines
                                      : 0;

    const auto emit_run = [&](const DropoutInfo& d, int32_t vfr_field) {
      if (d.end_sample < d.start_sample) return;
      const int32_t line = static_cast<int32_t>(d.line);
      if (line < 0 || line >= max_field_lines) return;
      const uint64_t flat_start = dropout_util::field_line_to_frame_sample(
          sys, vfr_field, line, static_cast<int32_t>(d.start_sample));
      const uint32_t count = d.end_sample - d.start_sample + 1;
      result.push_back({id, flat_start, count, 100});
    };

    if (tbc_f1_idx < field_meta_.size()) {
      for (const auto& d : field_meta_[tbc_f1_idx].dropouts) {
        emit_run(d, f1_vfr);
      }
    }

    if (tbc_f2_idx < field_meta_.size()) {
      for (const auto& d : field_meta_[tbc_f2_idx].dropouts) {
        emit_run(d, f2_vfr);
      }
    }

    return result;
  }

  std::optional<SourceParameters> get_video_parameters() const override {
    return source_params_;
  }

  // --------------------------------------------------------------------------
  // Audio
  // --------------------------------------------------------------------------
  bool has_audio() const override { return has_audio_; }

  // TBC audio is always frame-locked (the PCM file is segmented per-field in
  // ld-decode metadata).
  bool audio_locked() const override { return has_audio_; }

  uint32_t get_audio_sample_count(FrameID id) const override {
    if (!has_audio_ || !has_frame(id)) return 0;
    const size_t idx = static_cast<size_t>(id);

    if (video_params_.system != VideoSystem::PAL) {
      // NTSC/PAL_M: fixed 1470 stereo pairs per frame after resampling.
      ensure_ntsc_palM_audio();
      if (idx < resampled_audio_frames_.size() &&
          !resampled_audio_frames_[idx].empty()) {
        return static_cast<uint32_t>(resampled_audio_frames_[idx].size() / 2);
      }
      return 0;
    }

    // PAL: per-frame count from field metadata.
    if (idx >= audio_frame_pair_counts_.size()) return 0;
    return static_cast<uint32_t>(audio_frame_pair_counts_[idx]);
  }

  std::vector<int16_t> get_audio_samples(FrameID id) const override {
    if (!has_audio_ || !has_frame(id)) return {};
    const size_t idx = static_cast<size_t>(id);

    if (video_params_.system != VideoSystem::PAL) {
      // NTSC/PAL_M: return pre-resampled block.
      ensure_ntsc_palM_audio();
      if (idx < resampled_audio_frames_.size()) {
        return resampled_audio_frames_[idx];
      }
      return {};
    }

    // PAL: read raw PCM using pre-computed per-frame offsets.
    if (idx >= audio_frame_offsets_.size()) return {};
    const size_t pair_offset = audio_frame_offsets_[idx];
    const size_t pair_count = audio_frame_pair_counts_[idx];
    if (pair_count == 0) return {};
    return deps_->read_audio_samples_at(pcm_path_, pair_offset, pair_count);
  }

  // --------------------------------------------------------------------------
  // EFM
  // --------------------------------------------------------------------------
  bool has_efm() const override { return has_efm_; }

  uint32_t get_efm_sample_count(FrameID id) const override {
    if (!has_efm_) return 0;
    const size_t idx = static_cast<size_t>(id);
    if (idx >= efm_frame_byte_counts_.size()) return 0;
    return static_cast<uint32_t>(efm_frame_byte_counts_[idx]);
  }

  std::vector<uint8_t> get_efm_samples(FrameID id) const override {
    if (!has_efm_ || !has_frame(id)) return {};
    const size_t idx = static_cast<size_t>(id);
    if (idx >= efm_frame_offsets_.size()) return {};
    const size_t byte_offset = efm_frame_offsets_[idx];
    const size_t byte_count = efm_frame_byte_counts_[idx];
    if (byte_count == 0) return {};
    return deps_->read_efm_bytes_at(efm_bin_path_, byte_offset, byte_count);
  }

  // --------------------------------------------------------------------------
  // AC3 RF
  // --------------------------------------------------------------------------
  bool has_ac3_rf() const override { return has_ac3_; }

  std::vector<uint8_t> get_ac3_symbols(FrameID id) const override {
    if (!has_ac3_ || !has_frame(id)) return {};
    const int32_t fld1 = static_cast<int32_t>(id) * 2;
    const int32_t fld2 = fld1 + 1;
    auto result =
        deps_->read_ac3_for_frame(ac3_bin_path_, ac3_meta_path_, fld1, fld2);
    return result.value_or(std::vector<uint8_t>{});
  }

  // --------------------------------------------------------------------------
  // Targeted per-line sample access (bypasses full-frame assembly)
  // --------------------------------------------------------------------------
  // Callers that need only a few lines per frame (burst analysis, SNR, etc.)
  // call this instead of get_line() to avoid the ~1.4 MB full-frame load.
  //
  // To keep reads sequential and allow the kernel's read-ahead to work, we
  // buffer the most recently loaded field: the first call for a given field
  // reads all 710 KB in one shot; subsequent calls for different lines of the
  // same field are served from the buffer with no disk I/O.  For analysis
  // sinks that process lines in ascending order (burst: 11, 163, 309; SNR:
  // 18, 331) this reduces 6 scattered 2.3 KB reads per frame to 2 sequential
  // 710 KB reads — the OS read-ahead works correctly and the page-cache
  // exhaustion / accelerating-slowdown pattern is eliminated.
  std::vector<sample_type> get_line_samples(FrameID id,
                                            size_t line) const override {
    if (!has_frame(id)) return {};
    if (line >= static_cast<size_t>(source_params_.frame_height)) return {};

    // Determine field geometry per video system.
    int32_t tbc_field_idx;
    int32_t field_line;
    int32_t stored_spl;
    int32_t stored_field_size;
    const int32_t frame_idx = static_cast<int32_t>(id);

    switch (video_params_.system) {
      case VideoSystem::PAL: {
        constexpr int32_t kF1Lines = kPalField1Lines;          // 313
        constexpr int32_t kLineW = kPalSamplesPerLineNominal;  // 1135
        if (line < static_cast<size_t>(kF1Lines)) {
          tbc_field_idx = frame_idx * 2;
          field_line = static_cast<int32_t>(line);
        } else {
          tbc_field_idx = frame_idx * 2 + 1;
          field_line = static_cast<int32_t>(line) - kF1Lines;
        }
        stored_spl = kLineW;
        stored_field_size = kF1Lines * kLineW;
        break;
      }
      case VideoSystem::NTSC:
      case VideoSystem::PAL_M: {
        // Both 525-line systems store fields at 263 lines; only SPL differs.
        const int32_t kF1Lines =
            static_cast<int32_t>(field1_lines(video_params_.system));
        const int32_t kLineW =
            samples_per_line_from_system(video_params_.system);
        if (line < static_cast<size_t>(kF1Lines)) {
          tbc_field_idx = frame_idx * 2;
          field_line = static_cast<int32_t>(line);
        } else {
          tbc_field_idx = frame_idx * 2 + 1;
          field_line = static_cast<int32_t>(line) - kF1Lines;
        }
        stored_spl = kLineW;
        stored_field_size = kF1Lines * kLineW;
        break;
      }
      default:
        return {};
    }

    const int32_t line_sample_offset = field_line * stored_spl;

    // Field-level buffer: the first call for a given tbc_field_idx loads the
    // full field in one sequential read; subsequent calls for other lines of
    // the same field are served from the buffer without re-opening the file.
    // This reduces 6 scattered ~2 KB reads per frame to 2 sequential ~710 KB
    // reads and eliminates the per-line file-open overhead.
    std::lock_guard<std::mutex> lock(line_buffer_mutex_);

    if (line_buffer_field_idx_ != tbc_field_idx) {
      std::string buf_err;
      line_buffer_ =
          deps_->read_field_samples(tbc_path_, tbc_field_idx, stored_field_size,
                                    stored_field_size, buf_err);
      if (line_buffer_.empty()) {
        line_buffer_field_idx_ = -1;
        return {};
      }
      line_buffer_field_idx_ = tbc_field_idx;
    }

    const size_t offset = static_cast<size_t>(line_sample_offset);
    if (offset + static_cast<size_t>(stored_spl) > line_buffer_.size()) {
      return {};
    }

    // Convert TBC 16-bit unsigned → CVBS_U10_4FSC int16_t.
    const double tbc_blank = static_cast<double>(video_params_.blanking_16b);
    const double scale = static_cast<double>(source_params_.white_level -
                                             source_params_.blanking_level) /
                         static_cast<double>(video_params_.white_16b -
                                             video_params_.blanking_16b);
    const double cvbs_blank =
        static_cast<double>(source_params_.blanking_level);

    std::vector<sample_type> result(static_cast<size_t>(stored_spl));
    const uint16_t* raw_line = line_buffer_.data() + offset;
    for (int32_t i = 0; i < stored_spl; ++i) {
      const double v =
          (static_cast<double>(raw_line[static_cast<size_t>(i)]) - tbc_blank) *
              scale +
          cvbs_blank;
      result[static_cast<size_t>(i)] = static_cast<sample_type>(std::lround(v));
    }
    return result;
  }

 private:
  struct CachedFrame {
    std::vector<sample_type> samples;  // assembled composite frame
    std::vector<sample_type> luma;     // YC: luma channel (empty for composite)
    std::vector<sample_type> chroma;   // YC: chroma channel
    int colour_frame_index = -1;
  };

  size_t frame_samples_total() const {
    return static_cast<size_t>(frame_samples_from_system(video_params_.system));
  }

  // Compute the colour_frame_index for a frame from its field metadata.
  // Uses TBC field 1 (even index = 2×id) which carries the frame phase.
  int compute_colour_frame_index(FrameID id) const {
    const size_t fld_idx = static_cast<size_t>(id) * 2;
    if (fld_idx >= field_meta_.size()) return -1;
    const auto phase = field_meta_[fld_idx].field_phase_id;
    switch (video_params_.system) {
      case VideoSystem::PAL:
        return PalTBCConverter::map_field_phase_to_colour_frame_index(phase);
      case VideoSystem::NTSC:
        return NtscTBCConverter::map_field_phase_to_colour_frame_index(phase);
      case VideoSystem::PAL_M:
        return PalMTBCConverter::map_field_phase_to_colour_frame_index(phase);
      default:
        return -1;
    }
  }

  void ensure_frame_cached(FrameID id) const {
    if (frame_cache_.contains(id)) return;
    CachedFrame frame = assemble_frame(id);
    // put_if_absent: two threads can race past the contains() check and both
    // assemble the frame; replacing the cached entry would free the buffer
    // that the first thread's get_frame() pointer still refers to.
    frame_cache_.put_if_absent(id, std::move(frame));
  }

  CachedFrame assemble_frame(FrameID id) const {
    switch (video_params_.system) {
      case VideoSystem::PAL:
        return assemble_pal_frame(id);
      case VideoSystem::NTSC:
        return assemble_ntsc_frame(id);
      case VideoSystem::PAL_M:
        return assemble_pal_m_frame(id);
      default:
        throw std::runtime_error(
            "TBC source: unsupported video system for frame assembly");
    }
  }

  CachedFrame assemble_pal_frame(FrameID id) const {
    // TBC field ordering (EBU Tech. 3280-E §1.3 / ld-decode PAL convention):
    //   Even field indices (0, 2, 4…) → TBC field 1 (313 lines,
    //   odd-scan/earlier) Odd field indices  (1, 3, 5…) → TBC field 2 (312
    //   lines, even-scan/later)
    const int32_t tbc_f1_idx = static_cast<int32_t>(id) * 2;
    const int32_t tbc_f2_idx = tbc_f1_idx + 1;

    constexpr int32_t kF1Lines = kPalField1Lines;                   // 313
    constexpr int32_t kF2Lines = kPalFrameLines - kPalField1Lines;  // 312
    constexpr int32_t kLineW = kPalSamplesPerLineNominal;           // 1135
    const int32_t stored_field_size = kF1Lines * kLineW;

    std::string err;

    const std::vector<uint16_t> raw_f1 = deps_->read_field_samples(
        tbc_path_, tbc_f1_idx, stored_field_size, kF1Lines * kLineW, err);
    if (raw_f1.empty()) {
      throw std::runtime_error("PAL TBC: failed to read field 1 for frame " +
                               std::to_string(id) + ": " + err);
    }
    const std::vector<uint16_t> raw_f2 = deps_->read_field_samples(
        tbc_path_, tbc_f2_idx, stored_field_size, kF2Lines * kLineW, err);
    if (raw_f2.empty()) {
      throw std::runtime_error("PAL TBC: failed to read field 2 for frame " +
                               std::to_string(id) + ": " + err);
    }

    CachedFrame result;
    result.samples = PalTBCConverter::assemble_frame(
        raw_f1, raw_f2, video_params_.blanking_16b, video_params_.white_16b);
    result.colour_frame_index = compute_colour_frame_index(id);

    if (is_yc_) {
      const std::vector<uint16_t> raw_c1 = deps_->read_field_samples(
          c_path_, tbc_f1_idx, stored_field_size, kF1Lines * kLineW, err);
      const std::vector<uint16_t> raw_c2 = deps_->read_field_samples(
          c_path_, tbc_f2_idx, stored_field_size, kF2Lines * kLineW, err);
      if (raw_c1.empty() || raw_c2.empty()) {
        throw std::runtime_error(
            "PAL TBC YC: failed to read chroma field for frame " +
            std::to_string(id) + ": " + err);
      }
      result.luma = result.samples;
      result.chroma = PalTBCConverter::assemble_frame(
          raw_c1, raw_c2, video_params_.blanking_16b, video_params_.white_16b);
    }
    return result;
  }

  CachedFrame assemble_ntsc_frame(FrameID id) const {
    // SMPTE 244M-2003 §4.1: NTSC frame assembly.
    // Both TBC fields stored at 263 lines on disk.
    // TBC field 1 (even index, isFirstField=true) = odd-scan/first temporal,
    //   263 real lines → VFR field 1 (top).
    // TBC field 2 (odd index) = even-scan/second temporal, 262 real lines →
    //   VFR field 2 (bottom); the last stored line is padding and is discarded.
    const int32_t tbc_f1_idx = static_cast<int32_t>(id) * 2;
    const int32_t tbc_f2_idx = tbc_f1_idx + 1;

    constexpr int32_t kF1Lines = kNtscField1Lines;  // 263 = TBC f1 / VFR top
    constexpr int32_t kF2Lines =
        kNtscFrameLines - kNtscField1Lines;  // 262 = TBC f2 / VFR bottom
    constexpr int32_t kLineW = kNtscSamplesPerLine;  // 910
    const int32_t stored_field_size =
        kF1Lines * kLineW;  // 263×910 stored per field

    std::string err;

    // TBC field 1 (263 real lines, odd-scan, VFR top); use all lines.
    const std::vector<uint16_t> raw_f1 = deps_->read_field_samples(
        tbc_path_, tbc_f1_idx, stored_field_size, kF1Lines * kLineW, err);
    if (raw_f1.empty()) {
      throw std::runtime_error("NTSC TBC: failed to read field 1 for frame " +
                               std::to_string(id) + ": " + err);
    }
    // TBC field 2 (262 real lines, even-scan, VFR bottom); discard padding.
    const std::vector<uint16_t> raw_f2 = deps_->read_field_samples(
        tbc_path_, tbc_f2_idx, stored_field_size, kF2Lines * kLineW, err);
    if (raw_f2.empty()) {
      throw std::runtime_error("NTSC TBC: failed to read field 2 for frame " +
                               std::to_string(id) + ": " + err);
    }

    CachedFrame result;
    result.samples = NtscTBCConverter::assemble_frame(
        raw_f1, raw_f2, video_params_.blanking_16b, video_params_.white_16b);
    result.colour_frame_index = compute_colour_frame_index(id);

    if (is_yc_) {
      const std::vector<uint16_t> raw_c1 = deps_->read_field_samples(
          c_path_, tbc_f1_idx, stored_field_size, kF1Lines * kLineW, err);
      const std::vector<uint16_t> raw_c2 = deps_->read_field_samples(
          c_path_, tbc_f2_idx, stored_field_size, kF2Lines * kLineW, err);
      if (raw_c1.empty() || raw_c2.empty()) {
        throw std::runtime_error(
            "NTSC TBC YC: failed to read chroma field for frame " +
            std::to_string(id) + ": " + err);
      }
      result.luma = result.samples;
      result.chroma = NtscTBCConverter::assemble_frame(
          raw_c1, raw_c2, video_params_.blanking_16b, video_params_.white_16b);
    }
    return result;
  }

  CachedFrame assemble_pal_m_frame(FrameID id) const {
    // ITU-R BT.1700-1 Annex 1 Part B: PAL_M frame assembly.
    // Identical field ordering to NTSC: TBC field 1 (even index, 263 lines)
    // → VFR field 1 (top). kPalMSamplesPerLine = 909 samples/line.
    const int32_t tbc_f1_idx = static_cast<int32_t>(id) * 2;
    const int32_t tbc_f2_idx = tbc_f1_idx + 1;

    constexpr int32_t kF1Lines = kPalMField1Lines;  // 263 = TBC f1 / VFR top
    constexpr int32_t kF2Lines =
        kPalMFrameLines - kPalMField1Lines;  // 262 = TBC f2 / VFR bottom
    constexpr int32_t kLineW = kPalMSamplesPerLine;  // 909
    const int32_t stored_field_size =
        kF1Lines * kLineW;  // 263×909 stored per field

    std::string err;

    // TBC field 1 (263 real lines, odd-scan, VFR top); use all lines.
    const std::vector<uint16_t> raw_f1 = deps_->read_field_samples(
        tbc_path_, tbc_f1_idx, stored_field_size, kF1Lines * kLineW, err);
    if (raw_f1.empty()) {
      throw std::runtime_error("PAL-M TBC: failed to read field 1 for frame " +
                               std::to_string(id) + ": " + err);
    }
    // TBC field 2 (262 real lines, even-scan, VFR bottom); discard padding.
    const std::vector<uint16_t> raw_f2 = deps_->read_field_samples(
        tbc_path_, tbc_f2_idx, stored_field_size, kF2Lines * kLineW, err);
    if (raw_f2.empty()) {
      throw std::runtime_error("PAL-M TBC: failed to read field 2 for frame " +
                               std::to_string(id) + ": " + err);
    }

    CachedFrame result;
    result.samples = PalMTBCConverter::assemble_frame(
        raw_f1, raw_f2, video_params_.blanking_16b, video_params_.white_16b);
    result.colour_frame_index = compute_colour_frame_index(id);

    if (is_yc_) {
      // PAL_M YC: same field geometry as NTSC.
      const std::vector<uint16_t> raw_c1 = deps_->read_field_samples(
          c_path_, tbc_f1_idx, stored_field_size, kF1Lines * kLineW, err);
      const std::vector<uint16_t> raw_c2 = deps_->read_field_samples(
          c_path_, tbc_f2_idx, stored_field_size, kF2Lines * kLineW, err);
      if (raw_c1.empty() || raw_c2.empty()) {
        throw std::runtime_error(
            "PAL-M TBC YC: failed to read chroma field for frame " +
            std::to_string(id) + ": " + err);
      }
      result.luma = result.samples;
      result.chroma = PalMTBCConverter::assemble_frame(
          raw_c1, raw_c2, video_params_.blanking_16b, video_params_.white_16b);
    }
    return result;
  }

  // Pre-compute cumulative audio stereo-pair offsets from per-field metadata.
  // PAL: these offsets are used directly for raw PCM access.
  // NTSC/PAL_M: the total pair count is used to read the full PCM for
  // resampling; per-frame offsets are not used for playback.
  void compute_audio_offsets() {
    if (!has_audio_) return;
    const size_t fc = frame_count();
    audio_frame_offsets_.resize(fc, 0);
    audio_frame_pair_counts_.resize(fc, 0);

    size_t cumulative = 0;
    for (size_t frame_idx = 0; frame_idx < fc; ++frame_idx) {
      const size_t fld1 = frame_idx * 2;
      const size_t fld2 = fld1 + 1;

      size_t pairs = 0;
      if (fld1 < field_meta_.size()) {
        if (const auto& cnt = field_meta_[fld1].audio_sample_count) {
          pairs += static_cast<size_t>(*cnt);
        }
      }
      if (fld2 < field_meta_.size()) {
        if (const auto& cnt = field_meta_[fld2].audio_sample_count) {
          pairs += static_cast<size_t>(*cnt);
        }
      }

      audio_frame_offsets_[frame_idx] = cumulative;
      audio_frame_pair_counts_[frame_idx] = pairs;
      cumulative += pairs;
    }
    audio_total_raw_pairs_ = cumulative;
  }

  // NTSC/PAL_M only: lazily read the entire raw PCM, resample to the
  // frame-locked rate (44100000/1001 Hz), and cache per-frame 1470-pair
  // blocks.  PAL audio (44100 Hz = locked rate) bypasses this entirely.
  //
  // Runs at most once, on the first audio access, rather than in the
  // constructor: it reads the whole PCM (hundreds of MB for a feature-length
  // disc) and runs a full SoXR HQ pass, so doing it eagerly stalled the render
  // worker on every source execute of a long NTSC source — even for video
  // preview, which never needs audio (issue #209).  Only the audio / video
  // (with embedded audio) sinks reach this.  Thread-safe: audio accessors are
  // const and may be called concurrently.
  void ensure_ntsc_palM_audio() const {
    std::call_once(ntsc_audio_once_, [this] {
      if (!has_audio_) return;
      if (video_params_.system == VideoSystem::PAL) return;  // no resampling
      if (audio_total_raw_pairs_ == 0) return;

      const std::vector<int16_t> raw =
          deps_->read_audio_samples_at(pcm_path_, 0, audio_total_raw_pairs_);
      if (raw.empty()) return;

      resampled_audio_frames_ =
          NtscPalMAudioResampler::resample_and_segment(raw, frame_count());
    });
  }

  // Pre-compute cumulative per-frame EFM byte offsets from the per-field
  // T-value counts in the TBC metadata.  Each frame's EFM payload is the two
  // constituent fields' T-values concatenated; the raw .efm sidecar stores one
  // byte per T-value in field order, so a frame's byte offset is the running
  // sum of all preceding fields' counts.  Mirrors compute_audio_offsets().
  void compute_efm_offsets() {
    if (!has_efm_) return;
    const size_t fc = frame_count();
    efm_frame_offsets_.resize(fc, 0);
    efm_frame_byte_counts_.resize(fc, 0);

    size_t cumulative = 0;
    for (size_t frame_idx = 0; frame_idx < fc; ++frame_idx) {
      const size_t fld1 = frame_idx * 2;
      const size_t fld2 = fld1 + 1;

      size_t bytes = 0;
      if (fld1 < field_meta_.size()) {
        if (const auto& cnt = field_meta_[fld1].efm_t_value_count) {
          bytes += static_cast<size_t>(*cnt);
        }
      }
      if (fld2 < field_meta_.size()) {
        if (const auto& cnt = field_meta_[fld2].efm_t_value_count) {
          bytes += static_cast<size_t>(*cnt);
        }
      }

      efm_frame_offsets_[frame_idx] = cumulative;
      efm_frame_byte_counts_[frame_idx] = bytes;
      cumulative += bytes;
    }
  }

  TBCVideoParams video_params_;
  SourceParameters source_params_;
  std::vector<TBCFieldMeta> field_meta_;
  std::shared_ptr<ITBCSourceStageDeps> deps_;
  std::string tbc_path_;
  std::string c_path_;
  std::string pcm_path_;
  std::string efm_bin_path_;
  std::string ac3_bin_path_;
  std::string ac3_meta_path_;
  bool has_audio_ = false;
  bool has_efm_ = false;
  bool has_ac3_ = false;
  bool is_yc_ = false;

  // Pre-computed audio layout (stereo pairs) — PAL raw PCM path.
  std::vector<size_t> audio_frame_offsets_;
  std::vector<size_t> audio_frame_pair_counts_;
  size_t audio_total_raw_pairs_ = 0;

  // Pre-computed EFM layout (bytes) — cumulative offsets into the raw .efm
  // sidecar, one entry per frame.
  std::vector<size_t> efm_frame_offsets_;
  std::vector<size_t> efm_frame_byte_counts_;

  // NTSC/PAL_M: per-frame resampled audio blocks (1470 stereo pairs each).
  // Populated lazily by ensure_ntsc_palM_audio() on first audio access.
  mutable std::once_flag ntsc_audio_once_;
  mutable std::vector<std::vector<int16_t>> resampled_audio_frames_;

  static constexpr size_t kFrameCacheSize = 150;
  mutable LRUCache<FrameID, CachedFrame> frame_cache_{kFrameCacheSize};

  mutable std::mutex line_buffer_mutex_;
  mutable int32_t line_buffer_field_idx_{-1};
  mutable std::vector<uint16_t> line_buffer_;
};

// ---------------------------------------------------------------------------
// Metadata helpers shared by TBCSourceStageDeps
// ---------------------------------------------------------------------------

// Derive the legacy JSON sidecar path from a .db sidecar path.
// "foo.tbc.db" → "foo.tbc.json"
std::string json_path_from_db(const std::string& db_path) {
  constexpr std::string_view kSuffix = ".db";
  if (db_path.size() > kSuffix.size() &&
      db_path.compare(db_path.size() - kSuffix.size(), kSuffix.size(), ".db") ==
          0) {
    return db_path.substr(0, db_path.size() - kSuffix.size()) + ".json";
  }
  return {};
}

// Build TBCVideoParams from any open ITBCMetadataReader.
std::optional<TBCVideoParams> build_tvp_from_reader(
    ITBCMetadataReader& reader, const std::string& path,
    std::string& error_message) {
  const auto sp = reader.read_video_parameters();
  if (!sp) {
    error_message = "No video parameters in '" + path + "'";
    return std::nullopt;
  }
  TBCVideoParams tvp;
  tvp.system = sp->system;
  tvp.decoder = sp->decoder;
  tvp.tape_format = sp->tape_format;
  tvp.git_branch = sp->git_branch;
  tvp.git_commit = sp->git_commit;
  tvp.is_widescreen = sp->is_widescreen;
  // Read actual ld-decode 16-bit domain levels from the metadata.
  // These are the 0 IRE blanking and 100 IRE white levels in the TBC 16-bit
  // domain (CVBS_U10_4FSC × 64); using them gives accurate CVBS_U10_4FSC
  // conversion.  The JSON reader derives blanking from black16bIre for
  // NTSC/PAL_M.
  const auto tbc_levels = reader.read_tbc_domain_levels();
  const bool is_ntsc_like =
      (tvp.system == VideoSystem::NTSC || tvp.system == VideoSystem::PAL_M);
  tvp.blanking_16b = tbc_levels
                         ? tbc_levels->blanking_16b
                         : (is_ntsc_like ? kTbcNtscBlanking : kTbcPalBlanking);
  tvp.white_16b = tbc_levels ? tbc_levels->white_16b
                             : (is_ntsc_like ? kTbcNtscWhite : kTbcPalWhite);
  tvp.number_of_fields = sp->number_of_sequential_frames * 2;
  tvp.field_width = sp->frame_width_nominal;
  // Field heights: both stored at max height in the TBC file.
  // PAL: TBC field 1 = 313 lines (odd-scan, isFirstField), TBC field 2 = 312.
  // NTSC/PAL_M: TBC field 1 = 263 lines (isFirstField), TBC field 2 = 262.
  // In all systems, field 1 (isFirstField=true) is the longer stored field.
  const int32_t padded_fh =
      static_cast<int32_t>(calculate_padded_field_height(sp->system));
  tvp.field1_height = padded_fh;
  tvp.field2_height = padded_fh - 1;
  tvp.active_video_start = sp->active_video_start;
  tvp.active_video_end = sp->active_video_end;
  tvp.first_active_frame_line = sp->first_active_frame_line;
  tvp.last_active_frame_line = sp->last_active_frame_line;
  return tvp;
}

// Build TBCFieldMeta list from any open ITBCMetadataReader.
std::vector<TBCFieldMeta> build_field_meta_from_reader(
    ITBCMetadataReader& reader) {
  const auto all = reader.read_all_field_metadata();
  reader.read_all_dropouts();
  std::vector<TBCFieldMeta> result;
  result.reserve(all.size());
  for (const auto& [fid, fm] : all) {
    TBCFieldMeta meta;
    meta.field_phase_id = fm.field_phase_id;
    meta.audio_sample_count = fm.audio_samples;
    meta.efm_t_value_count = fm.efm_t_values;
    meta.ac3rf_symbol_count = fm.ac3rf_symbols;
    meta.file_location = fm.file_location;
    meta.dropouts = reader.read_dropouts(fid);
    result.push_back(meta);
  }
  return result;
}

// ---------------------------------------------------------------------------
// TBCSourceStageDeps — production filesystem / SQLite implementation
// ---------------------------------------------------------------------------

class TBCSourceStageDeps final : public ITBCSourceStageDeps {
 public:
  bool validate_input_file(const std::string& path,
                           std::string& error_message) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(path, ec)) {
      error_message = "TBC file not found: '" + path + "'";
      return false;
    }
    if (!fs::is_regular_file(path, ec)) {
      error_message = "TBC path is not a regular file: '" + path + "'";
      return false;
    }
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) {
      error_message = "TBC file is not readable: '" + path + "'";
      return false;
    }
    return true;
  }

  std::optional<TBCVideoParams> load_video_params(
      const std::string& db_path, std::string& error_message) const override {
    namespace fs = std::filesystem;
    std::error_code ec;

    // Try SQLite (.tbc.db) first.
    if (fs::exists(db_path, ec)) {
      TBCMetadataSqliteReader reader;
      if (!reader.open(db_path)) {
        error_message = "Failed to open TBC metadata: '" + db_path + "'";
        return std::nullopt;
      }
      return build_tvp_from_reader(reader, db_path, error_message);
    }

    // Fall back to legacy JSON (.tbc.json) when the SQLite sidecar is absent.
    const std::string json_path = json_path_from_db(db_path);
    if (!json_path.empty() && fs::exists(json_path, ec)) {
      auto reader = open_json_reader_cached(json_path);
      if (!reader) {
        error_message =
            "Failed to open TBC legacy JSON metadata: '" + json_path + "'";
        return std::nullopt;
      }
      return build_tvp_from_reader(*reader, json_path, error_message);
    }

    error_message = "TBC metadata database not found: '" + db_path + "'";
    return std::nullopt;
  }

  std::vector<TBCFieldMeta> load_all_field_meta(
      const std::string& db_path, std::string& error_message) const override {
    namespace fs = std::filesystem;
    std::error_code ec;

    // Try SQLite (.tbc.db) first.
    if (fs::exists(db_path, ec)) {
      TBCMetadataSqliteReader reader;
      if (!reader.open(db_path)) {
        error_message =
            "Failed to open TBC metadata for field meta: '" + db_path + "'";
        return {};
      }
      return build_field_meta_from_reader(reader);
    }

    // Fall back to legacy JSON (.tbc.json).
    const std::string json_path = json_path_from_db(db_path);
    if (!json_path.empty() && fs::exists(json_path, ec)) {
      auto reader = open_json_reader_cached(json_path);
      if (!reader) {
        error_message = "Failed to open TBC legacy JSON for field meta: '" +
                        json_path + "'";
        return {};
      }
      return build_field_meta_from_reader(*reader);
    }

    error_message =
        "Failed to open TBC metadata for field meta: '" + db_path + "'";
    return {};
  }

  std::vector<uint16_t> read_field_samples(
      const std::string& tbc_path, int32_t field_index,
      int32_t stored_samples_per_field, int32_t use_sample_count,
      std::string& error_message) const override {
    std::ifstream ifs(tbc_path, std::ios::binary);
    if (!ifs.is_open()) {
      error_message = "Failed to open TBC data file: '" + tbc_path + "'";
      return {};
    }

    // Each field in the TBC file is stored at stored_samples_per_field×2 bytes.
    const std::streamoff byte_offset =
        static_cast<std::streamoff>(field_index) *
        static_cast<std::streamoff>(stored_samples_per_field) * 2LL;
    ifs.seekg(byte_offset, std::ios::beg);
    if (!ifs.good()) {
      error_message = "Seek failed for field " + std::to_string(field_index) +
                      " in '" + tbc_path + "'";
      return {};
    }

    std::vector<uint16_t> samples(static_cast<size_t>(use_sample_count));
    ifs.read(reinterpret_cast<char*>(samples.data()),
             static_cast<std::streamsize>(use_sample_count) * 2LL);
    const size_t words_read = static_cast<size_t>(ifs.gcount()) / 2;
    if (words_read < static_cast<size_t>(use_sample_count)) {
      error_message = "Short read for field " + std::to_string(field_index) +
                      " in '" + tbc_path + "'";
      return {};
    }
    return samples;
  }

  std::vector<uint16_t> read_field_samples_at(
      const std::string& tbc_path, int32_t field_index,
      int32_t stored_samples_per_field, int32_t sample_offset,
      int32_t use_sample_count, std::string& error_message) const override {
    std::ifstream ifs(tbc_path, std::ios::binary);
    if (!ifs.is_open()) {
      error_message = "Failed to open TBC data file: '" + tbc_path + "'";
      return {};
    }
    const std::streamoff byte_offset =
        static_cast<std::streamoff>(field_index) *
            static_cast<std::streamoff>(stored_samples_per_field) * 2LL +
        static_cast<std::streamoff>(sample_offset) * 2LL;
    ifs.seekg(byte_offset, std::ios::beg);
    if (!ifs.good()) {
      error_message = "Seek failed for field " + std::to_string(field_index) +
                      " at offset " + std::to_string(sample_offset) + " in '" +
                      tbc_path + "'";
      return {};
    }
    std::vector<uint16_t> samples(static_cast<size_t>(use_sample_count));
    ifs.read(reinterpret_cast<char*>(samples.data()),
             static_cast<std::streamsize>(use_sample_count) * 2LL);
    const size_t words_read = static_cast<size_t>(ifs.gcount()) / 2;
    if (words_read < static_cast<size_t>(use_sample_count)) {
      error_message = "Short read for field " + std::to_string(field_index) +
                      " at offset " + std::to_string(sample_offset) + " in '" +
                      tbc_path + "'";
      return {};
    }
    return samples;
  }

  bool has_audio_file(const std::string& pcm_path) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(pcm_path, ec) && fs::is_regular_file(pcm_path, ec);
  }

  std::vector<int16_t> read_audio_samples_at(
      const std::string& pcm_path, size_t stereo_pair_offset,
      size_t stereo_pair_count) const override {
    std::ifstream ifs(pcm_path, std::ios::binary);
    if (!ifs.is_open()) return {};
    ifs.seekg(static_cast<std::streamoff>(stereo_pair_offset) * 4LL,
              std::ios::beg);
    if (!ifs.good()) return {};
    std::vector<int16_t> samples(stereo_pair_count * 2);
    ifs.read(reinterpret_cast<char*>(samples.data()),
             static_cast<std::streamsize>(stereo_pair_count) * 4LL);
    const size_t words_read = static_cast<size_t>(ifs.gcount()) / 2;
    if (words_read < stereo_pair_count * 2) {
      samples.resize(words_read);
    }
    return samples;
  }

  bool has_efm_file(const std::string& efm_bin_path) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(efm_bin_path, ec) &&
           fs::is_regular_file(efm_bin_path, ec);
  }

  std::vector<uint8_t> read_efm_bytes_at(const std::string& efm_bin_path,
                                         size_t efm_byte_offset,
                                         size_t efm_byte_count) const override {
    std::ifstream ifs(efm_bin_path, std::ios::binary);
    if (!ifs.is_open()) return {};
    ifs.seekg(static_cast<std::streamoff>(efm_byte_offset), std::ios::beg);
    if (!ifs.good()) return {};
    std::vector<uint8_t> bytes(efm_byte_count);
    ifs.read(reinterpret_cast<char*>(bytes.data()),
             static_cast<std::streamsize>(efm_byte_count));
    const size_t bytes_read = static_cast<size_t>(ifs.gcount());
    if (bytes_read < efm_byte_count) bytes.resize(bytes_read);
    return bytes;
  }

  bool has_ac3_files(const std::string& ac3_bin_path,
                     const std::string& ac3_meta_path) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(ac3_bin_path, ec) && fs::exists(ac3_meta_path, ec);
  }

  std::optional<std::vector<uint8_t>> read_ac3_for_frame(
      const std::string& /*ac3_bin_path*/, const std::string& /*ac3_meta_path*/,
      int32_t /*field_seq_no_a*/, int32_t /*field_seq_no_b*/) const override {
    return std::nullopt;
  }

 private:
  // Opening a legacy .tbc.json parses the entire document, which is expensive
  // for large captures; video params, per-field meta, and configuration
  // validation all need it. Cache the most recently parsed file (keyed by
  // path, size, and modification time) so each sidecar is parsed once.
  // Returned readers are safe to share across threads: all reads are served
  // from immutable maps populated during open().
  std::shared_ptr<TBCMetadataJsonReader> open_json_reader_cached(
      const std::string& json_path) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    const uintmax_t size = fs::file_size(json_path, ec);
    const fs::file_time_type mtime = fs::last_write_time(json_path, ec);

    std::lock_guard<std::mutex> lock(json_cache_mutex_);
    if (json_cache_reader_ && json_cache_path_ == json_path &&
        json_cache_size_ == size && json_cache_mtime_ == mtime) {
      return json_cache_reader_;
    }

    ORC_LOG_INFO("tbc_source: using legacy JSON metadata: {}", json_path);
    auto reader = std::make_shared<TBCMetadataJsonReader>();
    if (!reader->open(json_path)) {
      return nullptr;
    }
    json_cache_reader_ = std::move(reader);
    json_cache_path_ = json_path;
    json_cache_size_ = size;
    json_cache_mtime_ = mtime;
    return json_cache_reader_;
  }

  mutable std::mutex json_cache_mutex_;
  mutable std::shared_ptr<TBCMetadataJsonReader> json_cache_reader_;
  mutable std::string json_cache_path_;
  mutable uintmax_t json_cache_size_ = 0;
  mutable std::filesystem::file_time_type json_cache_mtime_{};
};

}  // namespace

// ---------------------------------------------------------------------------
// TBCSourceStage
// ---------------------------------------------------------------------------

TBCSourceStage::TBCSourceStage(std::shared_ptr<ITBCSourceStageDeps> deps)
    : deps_(std::move(deps)) {
  if (!deps_) {
    deps_ = std::make_shared<TBCSourceStageDeps>();
  }
  set_configuration_status(orc::ConfigurationStatus::Red);
}

TBCSourceStage::SidecarPaths TBCSourceStage::resolve_sidecars(
    const std::string& tbc_path,
    const std::map<std::string, ParameterValue>& params) const {
  namespace fs = std::filesystem;

  auto get_str = [&](const std::string& key) -> std::string {
    const auto it = params.find(key);
    if (it != params.end() && std::holds_alternative<std::string>(it->second)) {
      return std::get<std::string>(it->second);
    }
    return {};
  };

  // For YC sources tbc_path is the .tbcy file, but metadata lives next to the
  // base .tbc file.  Strip the Y/C extension and restore ".tbc" so that the
  // derived db_path resolves to e.g. "foo.tbc.db".
  std::string meta_base = tbc_path;
  if (meta_base.size() > 5) {
    std::string ext = meta_base.substr(meta_base.size() - 5);
    for (auto& c : ext) {
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (ext == ".tbcy" || ext == ".tbcc") {
      meta_base = meta_base.substr(0, meta_base.size() - 5) + ".tbc";
    }
  }

  SidecarPaths sp;
  sp.db_path = get_str("db_path");
  if (sp.db_path.empty()) {
    sp.db_path = meta_base + ".db";
  }

  sp.pcm_path = get_str("pcm_path");
  sp.efm_path = get_str("efm_path");
  sp.ac3_path = get_str("ac3rf_path");
  return sp;
}

std::vector<ArtifactPtr> TBCSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  (void)observation_context;

  if (!inputs.empty()) {
    throw std::runtime_error("tbc_source stage must have no inputs");
  }

  // Determine mode: composite or YC.
  auto get_str = [&](const std::string& key) -> std::string {
    const auto it = parameters.find(key);
    if (it != parameters.end() &&
        std::holds_alternative<std::string>(it->second)) {
      return std::get<std::string>(it->second);
    }
    return {};
  };

  const std::string y_path = get_str("y_path");
  const std::string c_path = get_str("c_path");
  const std::string comp_path = get_str("input_path");
  const bool is_yc = (!y_path.empty() && !c_path.empty());
  const std::string tbc_path = is_yc ? y_path : comp_path;

  if (tbc_path.empty()) {
    ORC_LOG_DEBUG("tbc_source: No input path configured, returning empty.");
    return {};
  }

  // Cache key is the primary TBC path.
  {
    std::lock_guard<std::mutex> lock(execute_mutex_);
    if (cached_representation_ && cached_input_key_ == tbc_path) {
      return {cached_representation_};
    }
  }

  // Validate primary TBC file.
  std::string err;
  if (!deps_->validate_input_file(tbc_path, err)) {
    throw UserDataError("TBC source: " + err);
  }
  if (is_yc && !deps_->validate_input_file(c_path, err)) {
    throw UserDataError("TBC source (chroma): " + err);
  }

  // Resolve sidecar paths.
  const SidecarPaths sc = resolve_sidecars(tbc_path, parameters);

  // Load video parameters from metadata.
  auto tvp_opt = deps_->load_video_params(sc.db_path, err);
  if (!tvp_opt) {
    throw UserDataError("TBC source: failed to load metadata from '" +
                        sc.db_path + "': " + err);
  }
  TBCVideoParams tvp = std::move(*tvp_opt);

  if (tvp.system == VideoSystem::Unknown) {
    throw UserDataError("TBC source: unknown video system in '" + sc.db_path +
                        "'");
  }
  if (tvp.blanking_16b <= 0 || tvp.white_16b <= tvp.blanking_16b) {
    throw UserDataError("TBC source: invalid level values in '" + sc.db_path +
                        "' (blanking=" + std::to_string(tvp.blanking_16b) +
                        ", white=" + std::to_string(tvp.white_16b) + ")");
  }

  // Load all per-field metadata.
  auto field_meta = deps_->load_all_field_meta(sc.db_path, err);

  // YC phase alignment check (design §14.11): compare colour_frame_index at
  // frame 0 for luma and chroma when operating in YC mode.
  if (is_yc && !field_meta.empty()) {
    auto c_sc = resolve_sidecars(c_path, parameters);
    c_sc.db_path = c_path + ".db";
    std::vector<TBCFieldMeta> c_meta =
        deps_->load_all_field_meta(c_sc.db_path, err);

    if (!c_meta.empty()) {
      int luma_cfi = -1;
      int chroma_cfi = -1;

      switch (tvp.system) {
        case VideoSystem::PAL:
          luma_cfi = PalTBCConverter::map_field_phase_to_colour_frame_index(
              field_meta[0].field_phase_id);
          chroma_cfi = PalTBCConverter::map_field_phase_to_colour_frame_index(
              c_meta[0].field_phase_id);
          if (!PalTBCYCConverter::check_yc_phase_alignment(luma_cfi,
                                                           chroma_cfi)) {
            throw UserDataError(
                PalTBCYCConverter::yc_alignment_error(luma_cfi, chroma_cfi));
          }
          break;
        case VideoSystem::NTSC:
          luma_cfi = NtscTBCConverter::map_field_phase_to_colour_frame_index(
              field_meta[0].field_phase_id);
          chroma_cfi = NtscTBCConverter::map_field_phase_to_colour_frame_index(
              c_meta[0].field_phase_id);
          if (!NtscTBCYCConverter::check_yc_phase_alignment(luma_cfi,
                                                            chroma_cfi)) {
            throw UserDataError(
                NtscTBCYCConverter::yc_alignment_error(luma_cfi, chroma_cfi));
          }
          break;
        case VideoSystem::PAL_M:
          luma_cfi = PalMTBCConverter::map_field_phase_to_colour_frame_index(
              field_meta[0].field_phase_id);
          chroma_cfi = PalMTBCConverter::map_field_phase_to_colour_frame_index(
              c_meta[0].field_phase_id);
          if (!PalMTBCYCConverter::check_yc_phase_alignment(luma_cfi,
                                                            chroma_cfi)) {
            throw UserDataError(
                PalMTBCYCConverter::yc_alignment_error(luma_cfi, chroma_cfi));
          }
          break;
        default:
          break;
      }
    }
  }

  // Determine sidecar availability.
  const int32_t frame_count = tvp.number_of_fields / 2;
  const bool has_audio =
      !sc.pcm_path.empty() && deps_->has_audio_file(sc.pcm_path);
  // TBC EFM: the raw .efm sidecar holds one byte per T-value; per-field
  // T-value counts come from the TBC metadata (there is no .efm.meta index —
  // that sidecar is CVBS-only).  EFM is available only when the .efm file
  // exists and the metadata carries at least one field T-value count.
  const bool metadata_has_efm = std::any_of(
      field_meta.begin(), field_meta.end(), [](const TBCFieldMeta& fm) {
        return fm.efm_t_value_count.has_value() && *fm.efm_t_value_count > 0;
      });
  const bool has_efm = !sc.efm_path.empty() &&
                       deps_->has_efm_file(sc.efm_path) && metadata_has_efm;
  const std::string ac3_meta = sc.ac3_path.empty() ? "" : sc.ac3_path + ".meta";
  const bool has_ac3 =
      !sc.ac3_path.empty() && deps_->has_ac3_files(sc.ac3_path, ac3_meta);

  // Build SourceParameters.
  SourceParameters src_params = build_source_params(tvp, frame_count);

  // Create representation.
  auto repr = std::make_shared<TBCDecodedFrameRepresentation>(
      tvp, src_params, std::move(field_meta), deps_, tbc_path,
      is_yc ? c_path : std::string{}, has_audio ? sc.pcm_path : std::string{},
      has_efm ? sc.efm_path : std::string{},
      has_ac3 ? sc.ac3_path : std::string{}, has_ac3 ? ac3_meta : std::string{},
      has_audio, has_efm, has_ac3, ArtifactID{}, Provenance{});

  // Update display name.
  const std::string new_display = make_display_name(tvp.system, is_yc);
  {
    std::lock_guard<std::mutex> lock(execute_mutex_);
    display_name_ = new_display;
    cached_representation_ = repr;
    cached_input_key_ = tbc_path;
  }

  ORC_LOG_INFO("tbc_source: Loaded '{}' — {} ({} frames)", tbc_path,
               new_display, frame_count);
  return {repr};
}

// ---------------------------------------------------------------------------
// Parameter descriptors
// ---------------------------------------------------------------------------

std::vector<ParameterDescriptor> TBCSourceStage::get_parameter_descriptors(
    VideoSystem /*project_format*/, SourceType /*source_type*/) const {
  std::vector<ParameterDescriptor> descs;

  auto make_path = [](const std::string& name, const std::string& display,
                      const std::string& desc_text,
                      const std::string& ext) -> ParameterDescriptor {
    ParameterDescriptor d;
    d.name = name;
    d.display_name = display;
    d.description = desc_text;
    d.type = ParameterType::FILE_PATH;
    d.constraints.required = false;
    d.constraints.default_value = std::string{};
    d.file_extension_hint = ext;
    return d;
  };

  descs.push_back(make_path(
      "input_path", "TBC File Path",
      "Path to the composite .tbc file from ld-decode (YC: leave empty and "
      "use y_path/c_path instead)",
      ".tbc"));
  descs.push_back(make_path("y_path", "Luma TBC File Path (YC)",
                            "Path to the luma .tbcy file for YC sources",
                            ".tbcy"));
  descs.push_back(make_path("c_path", "Chroma TBC File Path (YC)",
                            "Path to the chroma .tbcc file for YC sources",
                            ".tbcc"));
  descs.push_back(make_path(
      "pcm_path", "PCM Audio File Path",
      "Path to the analogue audio .pcm sidecar (raw 16-bit stereo PCM at "
      "44.1 kHz)",
      ".pcm"));
  descs.push_back(make_path("efm_path", "EFM Data File Path",
                            "Path to the EFM t-value .efm sidecar", ".efm"));
  descs.push_back(make_path("ac3rf_path", "AC3 RF Symbols File Path",
                            "Path to the AC3 RF symbols .ac3sym sidecar",
                            ".ac3sym"));
  return descs;
}

std::map<std::string, ParameterValue> TBCSourceStage::get_parameters() const {
  return parameters_;
}

bool TBCSourceStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  for (const auto& [k, v] : params) {
    if (!std::holds_alternative<std::string>(v)) return false;
  }
  parameters_ = params;

  const auto get_str = [&](const std::string& key) -> std::string {
    const auto it = params.find(key);
    if (it != params.end() && std::holds_alternative<std::string>(it->second)) {
      return std::get<std::string>(it->second);
    }
    return {};
  };

  const std::string comp_path = get_str("input_path");
  const std::string y_path = get_str("y_path");
  const std::string c_path = get_str("c_path");
  const bool is_yc = (!y_path.empty() && !c_path.empty());

  if (comp_path.empty() && !is_yc) {
    set_configuration_status(orc::ConfigurationStatus::Red);
    return true;
  }

  const std::string tbc_path = is_yc ? y_path : comp_path;
  std::string err;
  if (!deps_->validate_input_file(tbc_path, err)) {
    ORC_LOG_WARN("tbc_source: source file not accessible: {}", err);
    // The configured path does not point to a usable source file, so the
    // stage cannot produce any output; report Red rather than Yellow.
    set_configuration_status(orc::ConfigurationStatus::Red);
    return true;
  }

  const std::string explicit_db = get_str("db_path");
  const std::string db_path = explicit_db.empty()
                                  ? resolve_sidecars(tbc_path, params).db_path
                                  : explicit_db;

  std::string meta_err;
  const auto tvp_opt = deps_->load_video_params(db_path, meta_err);
  if (!tvp_opt) {
    ORC_LOG_WARN("tbc_source: metadata not accessible: {}", meta_err);
    set_configuration_status(orc::ConfigurationStatus::Yellow);
    return true;
  }

  set_configuration_status(orc::ConfigurationStatus::Green);
  return true;
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------

StagePreviewCapability TBCSourceStage::get_preview_capability() const {
  auto vfr = std::dynamic_pointer_cast<const VideoFrameRepresentation>(
      cached_representation_);
  return PreviewHelpers::make_signal_preview_capability(vfr);
}

}  // namespace orc
