/*
 * File:        tbc_source_stage.cpp
 * Module:      orc-stage-plugin-tbc-source
 * Purpose:     Unified TBC source stage — PAL/NTSC/PAL_M TBC file loading
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "tbc_source_stage.h"

#include <cvbs_signal_constants.h>
#include <tbc_metadata.h>
#include <tbc_reader.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

#include "error_types.h"
#include "logging.h"
#include "pal_tbc_converter.h"
#include "pal_tbc_yc_converter.h"

namespace orc {

namespace {

// ---------------------------------------------------------------------------
// PAL non-orthogonal line offset table (shared with CVBS source)
// ---------------------------------------------------------------------------
std::vector<size_t> compute_pal_line_offsets() {
  std::vector<size_t> offsets(static_cast<size_t>(kPalFrameLines));
  size_t offset = 0;
  for (int32_t i = 0; i < kPalFrameLines; ++i) {
    offsets[static_cast<size_t>(i)] = offset;
    const bool is_extra = (i == kPalExtraSampleLines[0] ||
                           i == kPalExtraSampleLines[1] ||
                           i == kPalExtraSampleLines[2] ||
                           i == kPalExtraSampleLines[3]);
    offset += is_extra ? static_cast<size_t>(kPalMaxSamplesPerLine)
                       : static_cast<size_t>(kPalMaxSamplesPerLine - 1);
  }
  return offsets;
}

const std::vector<size_t>& pal_line_offsets_tbc() {
  static const std::vector<size_t> kOffsets = compute_pal_line_offsets();
  return kOffsets;
}

// Build SourceParameters from TBCVideoParams using spec-defined CVBS levels.
SourceParameters build_source_params(const TBCVideoParams& tvp,
                                     int32_t frame_count) {
  // Active video geometry (BT.601-5 §2 / EBU Tech. 3280-E §1.2 /
  // SMPTE 170M §6.4).  These are the spec-defined values.
  constexpr int32_t kPalActiveVideoStart = 157;
  constexpr int32_t kPalActiveVideoEnd = 157 + 948;
  constexpr int32_t kPalFirstActiveFrameLine = 44;
  constexpr int32_t kPalLastActiveFrameLine = 620;
  constexpr int32_t kNtscActiveVideoStart = 126;
  constexpr int32_t kNtscActiveVideoEnd = 126 + 768;
  constexpr int32_t kNtscFirstActiveFrameLine = 40;
  constexpr int32_t kNtscLastActiveFrameLine = 519;

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
      sp.frame_width_nominal = kPalMaxSamplesPerLine - 1;
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
        const double n = static_cast<double>(tvp.ntsc_j_black_level_16b.value() -
                                              tvp.blanking_16b) /
                         static_cast<double>(tvp.white_16b - tvp.blanking_16b);
        const int32_t cvbs_j =
            static_cast<int32_t>(n * (kNtscWhite - kNtscBlanking) + kNtscBlanking);
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

  return sp;
}

// Resolve display name: "<System> TBC <Composite|YC>"
std::string make_display_name(VideoSystem system, bool is_yc) {
  std::string sys_str;
  switch (system) {
    case VideoSystem::PAL:   sys_str = "PAL";   break;
    case VideoSystem::NTSC:  sys_str = "NTSC";  break;
    case VideoSystem::PAL_M: sys_str = "PAL-M"; break;
    default:                 sys_str = "TBC";   break;
  }
  return sys_str + " TBC " + (is_yc ? "YC" : "Composite");
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
      TBCVideoParams video_params,
      SourceParameters source_params,
      std::vector<TBCFieldMeta> field_meta,
      std::shared_ptr<ITBCSourceStageDeps> deps,
      std::string tbc_path,  // composite .tbc (or Y .tbc for YC)
      std::string c_path,    // chroma .tbc for YC mode; empty for composite
      std::string pcm_path,
      std::string efm_bin_path,
      std::string efm_meta_path,
      std::string ac3_bin_path,
      std::string ac3_meta_path,
      bool has_audio,
      bool has_efm,
      bool has_ac3,
      ArtifactID artifact_id,
      Provenance provenance)
      : Artifact(std::move(artifact_id), std::move(provenance)),
        video_params_(std::move(video_params)),
        source_params_(std::move(source_params)),
        field_meta_(std::move(field_meta)),
        deps_(std::move(deps)),
        tbc_path_(std::move(tbc_path)),
        c_path_(std::move(c_path)),
        pcm_path_(std::move(pcm_path)),
        efm_bin_path_(std::move(efm_bin_path)),
        efm_meta_path_(std::move(efm_meta_path)),
        ac3_bin_path_(std::move(ac3_bin_path)),
        ac3_meta_path_(std::move(ac3_meta_path)),
        has_audio_(has_audio),
        has_efm_(has_efm),
        has_ac3_(has_ac3),
        is_yc_(!c_path_.empty()) {
    // Pre-compute per-frame audio sample offsets from field metadata.
    compute_audio_offsets();
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
    return FrameIDRange{0, static_cast<FrameID>(frame_count())};
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
    ensure_frame_cached(id);
    std::lock_guard<std::mutex> lock(cache_mutex_);
    const auto it = frame_cache_.find(id);
    if (it == frame_cache_.end()) return std::nullopt;

    FrameDescriptor desc;
    desc.frame_id = id;
    desc.system = video_params_.system;
    desc.height = static_cast<size_t>(source_params_.frame_height);
    desc.samples_total = static_cast<size_t>(frame_samples_total());
    desc.samples_per_line_nominal =
        static_cast<size_t>(source_params_.frame_width_nominal);
    desc.colour_frame_index = it->second.colour_frame_index;
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
    std::lock_guard<std::mutex> lock(cache_mutex_);
    const auto it = frame_cache_.find(id);
    return it != frame_cache_.end() ? it->second.samples.data() : nullptr;
  }

  const sample_type* get_line(FrameID id, size_t line) const override {
    if (!has_frame(id) ||
        line >= static_cast<size_t>(source_params_.frame_height)) {
      return nullptr;
    }
    const sample_type* frame = get_frame(id);
    if (!frame) return nullptr;
    if (video_params_.system == VideoSystem::PAL) {
      return frame + pal_line_offsets_tbc()[line];
    }
    return frame +
           line * static_cast<size_t>(source_params_.frame_width_nominal);
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
    std::lock_guard<std::mutex> lock(cache_mutex_);
    const auto it = frame_cache_.find(id);
    return (it != frame_cache_.end() && !it->second.luma.empty())
               ? it->second.luma.data()
               : nullptr;
  }

  const sample_type* get_frame_chroma(FrameID id) const override {
    if (!is_yc_ || !has_frame(id)) return nullptr;
    ensure_frame_cached(id);
    std::lock_guard<std::mutex> lock(cache_mutex_);
    const auto it = frame_cache_.find(id);
    return (it != frame_cache_.end() && !it->second.chroma.empty())
               ? it->second.chroma.data()
               : nullptr;
  }

  // --------------------------------------------------------------------------
  // Hints
  // --------------------------------------------------------------------------
  std::optional<int> get_frame_phase_hint(FrameID id) const override {
    if (!has_frame(id)) return std::nullopt;
    ensure_frame_cached(id);
    std::lock_guard<std::mutex> lock(cache_mutex_);
    const auto it = frame_cache_.find(id);
    if (it == frame_cache_.end()) return std::nullopt;
    const int idx = it->second.colour_frame_index;
    return (idx == -1) ? std::optional<int>{std::nullopt}
                       : std::optional<int>{idx};
  }

  std::optional<ActiveLineHint> get_active_line_hint() const override {
    if (source_params_.first_active_frame_line < 0) return std::nullopt;
    ActiveLineHint hint;
    hint.first_active_frame_line = source_params_.first_active_frame_line;
    hint.last_active_frame_line = source_params_.last_active_frame_line;
    hint.first_active_field_line = source_params_.first_active_frame_line / 2;
    hint.last_active_field_line = source_params_.last_active_frame_line / 2;
    hint.source = HintSource::METADATA;
    return hint;
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
    if (idx >= audio_frame_pair_counts_.size()) return 0;
    return static_cast<uint32_t>(audio_frame_pair_counts_[idx]);
  }

  std::vector<int16_t> get_audio_samples(FrameID id) const override {
    if (!has_audio_ || !has_frame(id)) return {};
    const size_t idx = static_cast<size_t>(id);
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

  std::vector<uint8_t> get_efm_samples(FrameID id) const override {
    if (!has_efm_ || !has_frame(id)) return {};
    const int32_t fld1 = static_cast<int32_t>(id) * 2;
    const int32_t fld2 = fld1 + 1;
    auto result = deps_->read_efm_for_frame(efm_bin_path_, efm_meta_path_,
                                            fld1, fld2);
    return result.value_or(std::vector<uint8_t>{});
  }

  // --------------------------------------------------------------------------
  // AC3 RF
  // --------------------------------------------------------------------------
  bool has_ac3_rf() const override { return has_ac3_; }

  std::vector<uint8_t> get_ac3_symbols(FrameID id) const override {
    if (!has_ac3_ || !has_frame(id)) return {};
    const int32_t fld1 = static_cast<int32_t>(id) * 2;
    const int32_t fld2 = fld1 + 1;
    auto result = deps_->read_ac3_for_frame(ac3_bin_path_, ac3_meta_path_,
                                            fld1, fld2);
    return result.value_or(std::vector<uint8_t>{});
  }

 private:
  struct CachedFrame {
    std::vector<sample_type> samples;  // assembled composite frame
    std::vector<sample_type> luma;     // YC: luma channel (empty for composite)
    std::vector<sample_type> chroma;   // YC: chroma channel
    int colour_frame_index = -1;
  };

  size_t frame_samples_total() const {
    // PAL: kPalFrameSamples; NTSC: kNtscFrameSamples; PAL_M: kPalMFrameSamples
    switch (video_params_.system) {
      case VideoSystem::PAL:   return static_cast<size_t>(kPalFrameSamples);
      case VideoSystem::NTSC:  return static_cast<size_t>(kNtscFrameSamples);
      case VideoSystem::PAL_M: return static_cast<size_t>(kPalMFrameSamples);
      default:                 return 0;
    }
  }

  // Compute the colour_frame_index for a frame from its two field metadata entries.
  int compute_colour_frame_index(FrameID id) const {
    // Use the phase from the first field (TBC field 1 = odd field = index 2×id).
    const size_t fld_idx = static_cast<size_t>(id) * 2;
    if (fld_idx >= field_meta_.size()) return -1;
    return PalTBCConverter::map_field_phase_to_colour_frame_index(
        field_meta_[fld_idx].field_phase_id);
  }

  void ensure_frame_cached(FrameID id) const {
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      if (frame_cache_.count(id)) return;
    }
    CachedFrame frame = assemble_frame(id);
    std::lock_guard<std::mutex> lock(cache_mutex_);
    frame_cache_.try_emplace(id, std::move(frame));
  }

  CachedFrame assemble_frame(FrameID id) const {
    // Only PAL is implemented in Phase 4; NTSC and PAL_M are Phase 5.
    if (video_params_.system != VideoSystem::PAL) {
      throw std::runtime_error(
          "TBC source: NTSC and PAL_M converters are not yet implemented "
          "(Phase 5). Only PAL TBC sources are supported in this version.");
    }

    // TBC field ordering (design §5.2.3):
    //   Even field indices (0, 2, 4…) → TBC field 1 (312 lines, odd/earlier)
    //   Odd field indices  (1, 3, 5…) → TBC field 2 (313 lines, even/later)
    const int32_t tbc_f1_idx = static_cast<int32_t>(id) * 2;
    const int32_t tbc_f2_idx = tbc_f1_idx + 1;

    // PAL field dimensions.
    constexpr int32_t kF1Lines = kPalFrameLines - kPalField1Lines;  // 312
    constexpr int32_t kF2Lines = kPalField1Lines;                   // 313
    constexpr int32_t kLineW   = kPalMaxSamplesPerLine - 1;         // 1135
    const int32_t stored_field_size = kF2Lines * kLineW;  // 313×1135 stored

    std::string err;

    // TBC field 1 (312-line field, odd/earlier temporal).
    const std::vector<uint16_t> raw_f1 = deps_->read_field_samples(
        tbc_path_, tbc_f1_idx, stored_field_size, kF1Lines * kLineW, err);
    if (raw_f1.empty()) {
      throw std::runtime_error("TBC: failed to read field 1 for frame " +
                               std::to_string(id) + ": " + err);
    }

    // TBC field 2 (313-line field, even/later temporal).
    const std::vector<uint16_t> raw_f2 = deps_->read_field_samples(
        tbc_path_, tbc_f2_idx, stored_field_size, kF2Lines * kLineW, err);
    if (raw_f2.empty()) {
      throw std::runtime_error("TBC: failed to read field 2 for frame " +
                               std::to_string(id) + ": " + err);
    }

    CachedFrame result;
    result.samples = PalTBCConverter::assemble_frame(
        raw_f1, raw_f2, video_params_.blanking_16b, video_params_.white_16b);
    result.colour_frame_index = compute_colour_frame_index(id);

    if (is_yc_) {
      // Luma channel (Y .tbc): same assembly, passed as composite.
      // Chroma channel (C .tbc): read from c_path_.
      const std::vector<uint16_t> raw_c1 = deps_->read_field_samples(
          c_path_, tbc_f1_idx, stored_field_size, kF1Lines * kLineW, err);
      const std::vector<uint16_t> raw_c2 = deps_->read_field_samples(
          c_path_, tbc_f2_idx, stored_field_size, kF2Lines * kLineW, err);

      if (raw_c1.empty() || raw_c2.empty()) {
        throw std::runtime_error(
            "TBC YC: failed to read chroma field for frame " +
            std::to_string(id) + ": " + err);
      }
      // Luma = samples already assembled above; copy to luma buffer.
      result.luma = result.samples;
      result.chroma = PalTBCConverter::assemble_frame(
          raw_c1, raw_c2, video_params_.blanking_16b, video_params_.white_16b);
    }

    return result;
  }

  // Pre-compute cumulative audio stereo-pair offsets from per-field metadata.
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
  }

  TBCVideoParams video_params_;
  SourceParameters source_params_;
  std::vector<TBCFieldMeta> field_meta_;
  std::shared_ptr<ITBCSourceStageDeps> deps_;
  std::string tbc_path_;
  std::string c_path_;
  std::string pcm_path_;
  std::string efm_bin_path_;
  std::string efm_meta_path_;
  std::string ac3_bin_path_;
  std::string ac3_meta_path_;
  bool has_audio_ = false;
  bool has_efm_ = false;
  bool has_ac3_ = false;
  bool is_yc_ = false;

  // Pre-computed audio layout (stereo pairs).
  std::vector<size_t> audio_frame_offsets_;
  std::vector<size_t> audio_frame_pair_counts_;

  mutable std::mutex cache_mutex_;
  mutable std::unordered_map<FrameID, CachedFrame> frame_cache_;
};

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
    if (!fs::exists(db_path, ec)) {
      error_message = "TBC metadata database not found: '" + db_path + "'";
      return std::nullopt;
    }

    TBCMetadataSqliteReader reader;
    if (!reader.open(db_path)) {
      error_message = "Failed to open TBC metadata: '" + db_path + "'";
      return std::nullopt;
    }

    const auto sp = reader.read_video_parameters();
    if (!sp) {
      error_message = "No video parameters in '" + db_path + "'";
      return std::nullopt;
    }

    TBCVideoParams tvp;
    tvp.system = sp->system;
    tvp.decoder = sp->decoder;
    tvp.tape_format = sp->tape_format;
    tvp.git_branch = sp->git_branch;
    tvp.git_commit = sp->git_commit;
    tvp.is_widescreen = sp->is_widescreen;
    tvp.blanking_16b = sp->blanking_16b_ire;
    tvp.white_16b = sp->white_16b_ire;
    tvp.number_of_fields = sp->number_of_sequential_fields;
    tvp.field_width = sp->field_width;

    // Field heights: both stored at max height in the TBC file.
    // PAL: max = 313 lines; shorter field = 312 lines.
    // NTSC/PAL_M: max = 263 lines; shorter field = 262 lines.
    tvp.field2_height = sp->field_height;               // max (stored) height
    tvp.field1_height = sp->field_height - 1;           // shorter field

    tvp.active_video_start = sp->active_video_start;
    tvp.active_video_end = sp->active_video_end;
    tvp.first_active_frame_line = sp->first_active_frame_line;
    tvp.last_active_frame_line = sp->last_active_frame_line;

    return tvp;
  }

  std::vector<TBCFieldMeta> load_all_field_meta(
      const std::string& db_path, std::string& error_message) const override {
    TBCMetadataSqliteReader reader;
    if (!reader.open(db_path)) {
      error_message = "Failed to open TBC metadata for field meta: '" +
                      db_path + "'";
      return {};
    }

    const auto all = reader.read_all_field_metadata();
    std::vector<TBCFieldMeta> result;
    result.reserve(all.size());
    for (const auto& [fid, fm] : all) {
      TBCFieldMeta meta;
      meta.field_phase_id = fm.field_phase_id;
      meta.audio_sample_count = fm.audio_samples;
      meta.efm_t_value_count = fm.efm_t_values;
      meta.ac3rf_symbol_count = fm.ac3rf_symbols;
      meta.file_location = fm.file_location;
      result.push_back(meta);
    }
    return result;
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
    const size_t words_read =
        static_cast<size_t>(ifs.gcount()) / 2;
    if (words_read < static_cast<size_t>(use_sample_count)) {
      error_message = "Short read for field " + std::to_string(field_index) +
                      " in '" + tbc_path + "'";
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

  bool has_efm_files(const std::string& efm_bin_path,
                     const std::string& efm_meta_path) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(efm_bin_path, ec) && fs::exists(efm_meta_path, ec);
  }

  std::optional<std::vector<uint8_t>> read_efm_for_frame(
      const std::string& /*efm_bin_path*/,
      const std::string& /*efm_meta_path*/,
      int32_t /*field_seq_no_a*/,
      int32_t /*field_seq_no_b*/) const override {
    // EFM reading from TBC is deferred; the TBC field-level EFM data is
    // stored per-field in the existing metadata.  Full implementation uses
    // the TBCAudioEFMHandler; returning empty for now to satisfy the interface.
    return std::nullopt;
  }

  bool has_ac3_files(const std::string& ac3_bin_path,
                     const std::string& ac3_meta_path) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(ac3_bin_path, ec) && fs::exists(ac3_meta_path, ec);
  }

  std::optional<std::vector<uint8_t>> read_ac3_for_frame(
      const std::string& /*ac3_bin_path*/,
      const std::string& /*ac3_meta_path*/,
      int32_t /*field_seq_no_a*/,
      int32_t /*field_seq_no_b*/) const override {
    return std::nullopt;
  }
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
}

TBCSourceStage::SidecarPaths TBCSourceStage::resolve_sidecars(
    const std::string& tbc_path,
    const std::map<std::string, ParameterValue>& params) const {
  namespace fs = std::filesystem;

  auto get_str = [&](const std::string& key) -> std::string {
    const auto it = params.find(key);
    if (it != params.end() &&
        std::holds_alternative<std::string>(it->second)) {
      return std::get<std::string>(it->second);
    }
    return {};
  };

  SidecarPaths sp;
  sp.db_path = get_str("db_path");
  if (sp.db_path.empty()) {
    sp.db_path = tbc_path + ".json.db";
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

  const std::string y_path   = get_str("y_path");
  const std::string c_path   = get_str("c_path");
  const std::string comp_path = get_str("input_path");
  const bool is_yc = (!y_path.empty() && !c_path.empty());
  const std::string tbc_path  = is_yc ? y_path : comp_path;

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
    throw UserDataError(
        "TBC source: unknown video system in '" + sc.db_path + "'");
  }
  if (tvp.blanking_16b <= 0 || tvp.white_16b <= tvp.blanking_16b) {
    throw UserDataError(
        "TBC source: invalid level values in '" + sc.db_path +
        "' (blanking=" + std::to_string(tvp.blanking_16b) +
        ", white=" + std::to_string(tvp.white_16b) + ")");
  }

  // Load all per-field metadata.
  auto field_meta = deps_->load_all_field_meta(sc.db_path, err);

  // PAL YC phase alignment check (design §14.11).
  if (is_yc && tvp.system == VideoSystem::PAL && field_meta.size() >= 2) {
    const int luma_cfi = PalTBCConverter::map_field_phase_to_colour_frame_index(
        field_meta[0].field_phase_id);
    // Chroma phase: load chroma metadata separately.
    auto c_sc = resolve_sidecars(c_path, parameters);
    c_sc.db_path = c_path + ".json.db";
    std::vector<TBCFieldMeta> c_meta =
        deps_->load_all_field_meta(c_sc.db_path, err);
    if (!c_meta.empty()) {
      const int chroma_cfi =
          PalTBCConverter::map_field_phase_to_colour_frame_index(
              c_meta[0].field_phase_id);
      if (!PalTBCYCConverter::check_yc_phase_alignment(luma_cfi, chroma_cfi)) {
        throw UserDataError(
            PalTBCYCConverter::yc_alignment_error(luma_cfi, chroma_cfi));
      }
    }
  }

  // Determine sidecar availability.
  const int32_t frame_count = tvp.number_of_fields / 2;
  const bool has_audio =
      !sc.pcm_path.empty() && deps_->has_audio_file(sc.pcm_path);
  const std::string efm_meta =
      sc.efm_path.empty() ? "" : sc.efm_path + ".meta";
  const bool has_efm =
      !sc.efm_path.empty() && deps_->has_efm_files(sc.efm_path, efm_meta);
  const std::string ac3_meta =
      sc.ac3_path.empty() ? "" : sc.ac3_path + ".meta";
  const bool has_ac3 =
      !sc.ac3_path.empty() && deps_->has_ac3_files(sc.ac3_path, ac3_meta);

  // Build SourceParameters.
  SourceParameters src_params =
      build_source_params(tvp, frame_count);

  // Create representation.
  auto repr = std::make_shared<TBCDecodedFrameRepresentation>(
      tvp, src_params, std::move(field_meta), deps_,
      tbc_path, is_yc ? c_path : std::string{},
      has_audio ? sc.pcm_path : std::string{},
      has_efm ? sc.efm_path : std::string{},
      has_efm ? efm_meta : std::string{},
      has_ac3 ? sc.ac3_path : std::string{},
      has_ac3 ? ac3_meta : std::string{},
      has_audio, has_efm, has_ac3,
      ArtifactID{}, Provenance{});

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
  descs.push_back(make_path(
      "y_path", "Luma TBC File Path (YC)",
      "Path to the luma .tbc file for YC sources", ".tbc"));
  descs.push_back(make_path(
      "c_path", "Chroma TBC File Path (YC)",
      "Path to the chroma .tbc file for YC sources", ".tbc"));
  descs.push_back(make_path(
      "pcm_path", "PCM Audio File Path",
      "Path to the analogue audio .pcm sidecar (raw 16-bit stereo PCM at "
      "44.1 kHz)",
      ".pcm"));
  descs.push_back(make_path(
      "efm_path", "EFM Data File Path",
      "Path to the EFM t-value .efm sidecar", ".efm"));
  descs.push_back(make_path(
      "ac3rf_path", "AC3 RF Symbols File Path",
      "Path to the AC3 RF symbols .ac3sym sidecar", ".ac3sym"));
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
  return true;
}

// ---------------------------------------------------------------------------
// Preview
// ---------------------------------------------------------------------------

bool TBCSourceStage::supports_preview() const {
  return cached_representation_ != nullptr;
}

std::vector<PreviewOption> TBCSourceStage::get_preview_options() const {
  if (!cached_representation_) return {};

  const auto* repr =
      dynamic_cast<const TBCDecodedFrameRepresentation*>(
          cached_representation_.get());
  if (!repr) return {};

  const size_t fc = repr->frame_count();
  if (fc == 0) return {};

  auto params_opt = repr->get_video_parameters();
  if (!params_opt) return {};

  const uint32_t width =
      static_cast<uint32_t>(params_opt->frame_width_nominal);
  const uint32_t height = static_cast<uint32_t>(params_opt->frame_height);
  constexpr double dar = 0.7;

  return {
      PreviewOption{"frame", "Frame (Clamped)", false, width, height, fc, dar},
      PreviewOption{"frame_raw", "Frame (Raw)", false, width, height, fc, dar},
  };
}

PreviewImage TBCSourceStage::render_preview(const std::string& /*option_id*/,
                                            uint64_t index,
                                            PreviewNavigationHint /*hint*/) const {
  if (!cached_representation_) return PreviewImage{0, 0, {}, {}, {}};
  const auto* repr = dynamic_cast<const TBCDecodedFrameRepresentation*>(
      cached_representation_.get());
  if (!repr) return PreviewImage{0, 0, {}, {}, {}};

  const FrameID fid = static_cast<FrameID>(index);
  if (!repr->has_frame(fid)) return PreviewImage{0, 0, {}, {}, {}};

  auto params_opt = repr->get_video_parameters();
  if (!params_opt) return PreviewImage{0, 0, {}, {}, {}};

  const size_t height = static_cast<size_t>(params_opt->frame_height);
  const size_t width = static_cast<size_t>(params_opt->frame_width_nominal);
  const int32_t blanking = params_opt->blanking_level;
  const int32_t white = params_opt->white_level;
  const int32_t range = (white > blanking) ? (white - blanking) : 1;

  PreviewImage img;
  img.width = static_cast<uint32_t>(width);
  img.height = static_cast<uint32_t>(height);
  img.rgb_data.reserve(width * height * 3);

  for (size_t line = 0; line < height; ++line) {
    const VideoFrameRepresentation::sample_type* row = repr->get_line(fid, line);
    for (size_t s = 0; s < width; ++s) {
      const int32_t raw = row ? static_cast<int32_t>(row[s]) : blanking;
      const int32_t scaled = (raw - blanking) * 255 / range;
      const uint8_t grey =
          static_cast<uint8_t>(std::clamp(scaled, 0, 255));
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
      img.rgb_data.push_back(grey);
    }
  }
  return img;
}

std::optional<StageReport> TBCSourceStage::generate_report() const {
  StageReport report;
  report.summary = "TBC Source Status";

  const auto get_str = [&](const std::string& key) -> std::string {
    const auto it = parameters_.find(key);
    if (it != parameters_.end() &&
        std::holds_alternative<std::string>(it->second)) {
      return std::get<std::string>(it->second);
    }
    return {};
  };

  const std::string comp_path = get_str("input_path");
  const std::string y_path    = get_str("y_path");
  const std::string c_path    = get_str("c_path");
  const bool is_yc = (!y_path.empty() && !c_path.empty());
  const std::string tbc_path  = is_yc ? y_path : comp_path;

  if (tbc_path.empty()) {
    report.items.push_back({"Source File", "Not configured"});
    return report;
  }

  report.items.push_back({"Source File", tbc_path});
  report.items.push_back({"Mode", is_yc ? "YC" : "Composite"});

  if (cached_representation_) {
    const auto* repr = dynamic_cast<const TBCDecodedFrameRepresentation*>(
        cached_representation_.get());
    if (repr) {
      auto params = repr->get_video_parameters();
      if (params) {
        report.items.push_back({"Video System",
                                [&]() -> std::string {
                                  switch (params->system) {
                                    case VideoSystem::PAL:   return "PAL";
                                    case VideoSystem::NTSC:  return "NTSC";
                                    case VideoSystem::PAL_M: return "PAL-M";
                                    default:                 return "Unknown";
                                  }
                                }()});
        report.items.push_back(
            {"Frame Count",
             std::to_string(params->number_of_sequential_frames)});
      }
    }
  }
  return report;
}

}  // namespace orc
