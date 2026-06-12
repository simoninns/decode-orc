/*
 * File:        cvbs_source_stage.cpp
 * Module:      orc-core
 * Purpose:     CVBS (Composite Video Baseband Signal) source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "cvbs_source_stage.h"

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
#include <unordered_map>
#include <vector>

#include "cvbs_signal_constants.h"
#include "error_types.h"
#include "logging.h"

namespace orc {

namespace {

// ---------------------------------------------------------------------------
// Sidecar path derivation
// ---------------------------------------------------------------------------

// Return the path with the last extension replaced by suffix.
std::string derive_sidecar_path(const std::string& input_path,
                                const std::string& suffix) {
  namespace fs = std::filesystem;
  const fs::path p(input_path);
  return (p.parent_path() / p.stem()).string() + suffix;
}

// ---------------------------------------------------------------------------
// PAL non-orthogonal line offset table
// ---------------------------------------------------------------------------

// EBU Tech. 3280-E §1.3.1: build a lookup from 0-based frame-flat line index
// to sample offset within the flat frame buffer.  The four non-orthogonal
// lines (kPalExtraSampleLines) carry 1136 samples; all others carry 1135.
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

// Pre-computed once for the lifetime of the process.
const std::vector<size_t>& pal_line_offsets() {
  static const std::vector<size_t> kOffsets = compute_pal_line_offsets();
  return kOffsets;
}

// ---------------------------------------------------------------------------
// Sample encoding normalisation
// ---------------------------------------------------------------------------

// CVBS file format spec §3.1: normalise a raw 16-bit word from any of the
// four declared sample encodings to the CVBS_U10_4FSC domain (int16_t).
// No output clamping — headroom values outside [0, 1023] are preserved.
inline int16_t normalize_to_cvbs_u10(uint16_t raw, const std::string& encoding,
                                     int32_t blanking_10bit) {
  if (encoding == "CVBS_U10_4FSC") {
    // CVBS file format spec §3.1: int16_t stored bitwise as uint16_t.
    return static_cast<int16_t>(raw);
  }
  if (encoding == "CVBS_U16_4FSC") {
    // CVBS file format spec §3.1: unsigned value = 10-bit × 64.
    return static_cast<int16_t>(static_cast<int32_t>(raw) / 64);
  }
  if (encoding == "CVBS_TPG21_4FSC") {
    // CVBS file format spec §3.1: signed, device offset 508, ×64 scale.
    const int32_t decoded =
        static_cast<int32_t>(static_cast<int16_t>(raw)) / 64 + 508;
    return static_cast<int16_t>(decoded);
  }
  if (encoding == "CVBS_S16_FSC") {
    // CVBS file format spec §3.1: signed, blanking-centred, ×32 scale.
    const int32_t decoded =
        static_cast<int32_t>(static_cast<int16_t>(raw)) / 32 + blanking_10bit;
    return static_cast<int16_t>(decoded);
  }
  // Fallback — treat unknown encoding as CVBS_U16_4FSC.
  return static_cast<int16_t>(static_cast<int32_t>(raw) / 64);
}

// ---------------------------------------------------------------------------
// Colour burst phase measurement
// ---------------------------------------------------------------------------

// Measure the colour frame sequence index for a decoded CVBS_U10_4FSC frame.
//
// Demodulates the colour burst at a fixed reference position (line 9, burst
// window) and maps the measured carrier angle to the colour frame index.
//
// Returns -1 when the burst is absent or too weak to classify (e.g., blank
// tape or pre-programme leader).
//
// EBU Tech. 3280-E §1.1.1 (PAL); SMPTE 244M-2003 §3.2 (NTSC);
// ITU-R BT.1700-1 Annex 1 Part B (PAL_M).
int measure_colour_frame_index(const int16_t* frame_data,
                               VideoSystem system, int32_t blanking_10bit) {
  constexpr int kRefLine = 9;   // 0-based frame-flat line index
  constexpr int kBurstCount = 40;  // samples to demodulate

  // EBU Tech. 3280-E §1.2: PAL colour burst at samples 93..132.
  // SMPTE 244M-2003 §4.2.1: NTSC colour burst at samples 74..109.
  // ITU-R BT.1700-1 Annex 1 Part B: PAL_M uses same window as NTSC.
  int burst_start = 0;
  size_t line_start = 0;
  switch (system) {
    case VideoSystem::PAL:
      burst_start = 93;
      line_start = pal_line_offsets()[static_cast<size_t>(kRefLine)];
      break;
    case VideoSystem::NTSC:
      burst_start = 74;
      line_start = static_cast<size_t>(kRefLine) *
                   static_cast<size_t>(kNtscSamplesPerLine);
      break;
    case VideoSystem::PAL_M:
      burst_start = 74;
      line_start = static_cast<size_t>(kRefLine) *
                   static_cast<size_t>(kPalMSamplesPerLine);
      break;
    default:
      return -1;
  }

  const size_t abs_offset = line_start + static_cast<size_t>(burst_start);
  // Phase base: position of burst[0] within the 4-sample subcarrier cycle.
  const int phase_base = static_cast<int>(abs_offset % 4);
  const int16_t* burst_ptr = frame_data + abs_offset;

  // Quadrature demodulation at 4FSC: cos/sin values cycle {1,0,-1,0}/{0,1,0,-1}.
  double I = 0.0;
  double Q = 0.0;
  for (int n = 0; n < kBurstCount; ++n) {
    const double ac = static_cast<double>(burst_ptr[n]) - blanking_10bit;
    switch ((phase_base + n) % 4) {
      case 0: I += ac; break;
      case 1: Q += ac; break;
      case 2: I -= ac; break;
      case 3: Q -= ac; break;
      default: break;
    }
  }

  // Reject absent or corrupted bursts.  Threshold = ~2 ADU RMS amplitude,
  // which is far below the spec minimum burst amplitude (~50 ADU for PAL).
  const double amplitude = std::sqrt(I * I + Q * Q);
  constexpr double kMinBurstAmplitude = 20.0;
  if (amplitude < kMinBurstAmplitude) {
    return -1;
  }

  double angle_deg = std::atan2(Q, I) * (180.0 / M_PI);
  if (angle_deg < 0.0) {
    angle_deg += 360.0;
  }

  if (system == VideoSystem::NTSC) {
    // SMPTE 244M-2003 §3.2: Frame A (~180°) → index 0; Frame B (~0°) → index 1.
    return (angle_deg >= 90.0 && angle_deg < 270.0) ? 0 : 1;
  }

  const int sector = static_cast<int>(angle_deg / 90.0) % 4;

  if (system == VideoSystem::PAL) {
    // EBU Tech. 3280-E §1.1.1: PAL 4-frame sequence.
    // Measured angle = −burst_phase; consecutive frames: 45°,135°,225°,315°.
    // sector [0°,90°)→1, [90°,180°)→2, [180°,270°)→3, [270°,360°)→4.
    static constexpr int kPalMap[4] = {1, 2, 3, 4};
    return kPalMap[sector];
  }

  // ITU-R BT.1700-1 Annex 1 Part B: PAL_M 4-frame sequence (+90°/frame).
  // Consecutive measured angles: 45°,315°,225°,135°.
  // sector [0°,90°)→1, [90°,180°)→4, [180°,270°)→3, [270°,360°)→2.
  static constexpr int kPalMMap[4] = {1, 4, 3, 2};
  return kPalMMap[sector];
}

// ---------------------------------------------------------------------------
// SourceParameters population from spec constants
// ---------------------------------------------------------------------------

// Active video geometry constants (BT.601-5 §2 / EBU Tech. 3280-E §1.2 /
// SMPTE 170M §6.4).  These are the spec-defined values; they do not come from
// the .meta file.
constexpr int32_t kPalActiveVideoStart = 157;
constexpr int32_t kPalActiveVideoEnd = 157 + 948;  // = 1105
constexpr int32_t kPalFirstActiveFrameLine = 44;
constexpr int32_t kPalLastActiveFrameLine = 620;

constexpr int32_t kNtscActiveVideoStart = 126;
constexpr int32_t kNtscActiveVideoEnd = 126 + 768;  // = 894
constexpr int32_t kNtscFirstActiveFrameLine = 40;
constexpr int32_t kNtscLastActiveFrameLine = 519;

SourceParameters build_source_parameters(VideoSystem system,
                                         int32_t frame_count,
                                         int32_t ntsc_j_black_level = -1) {
  SourceParameters sp;
  sp.system = system;
  sp.number_of_sequential_frames = frame_count;
  sp.is_mapped = false;
  sp.tape_format = "cvbs";
  sp.decoder = "cvbs-source";

  switch (system) {
    case VideoSystem::PAL:
      // EBU Tech. 3280-E §1.1 level constants.
      sp.frame_width_nominal = kPalMaxSamplesPerLine - 1;  // 1135
      sp.frame_height = kPalFrameLines;                     // 625
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
      sp.frame_width_nominal = kNtscSamplesPerLine;  // 910
      sp.frame_height = kNtscFrameLines;             // 525
      sp.sync_tip_level = kNtscSyncTip;
      sp.blanking_level = kNtscBlanking;
      sp.black_level = (ntsc_j_black_level >= 0) ? ntsc_j_black_level
                                                  : kNtscBlack;
      sp.white_level = kNtscWhite;
      sp.peak_level = kNtscPeak;
      sp.active_video_start = kNtscActiveVideoStart;
      sp.active_video_end = kNtscActiveVideoEnd;
      sp.first_active_frame_line = kNtscFirstActiveFrameLine;
      sp.last_active_frame_line = kNtscLastActiveFrameLine;
      sp.has_nonstandard_values = (ntsc_j_black_level >= 0);
      break;

    case VideoSystem::PAL_M:
      // ITU-R BT.1700-1 Annex 1 Part B: PAL_M uses NTSC signal levels.
      sp.frame_width_nominal = kPalMSamplesPerLine;  // 909
      sp.frame_height = kPalMFrameLines;             // 525
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

// ---------------------------------------------------------------------------
// Inline frame preview rendering (VFR → RGB888 grayscale)
// ---------------------------------------------------------------------------

PreviewImage render_vfr_frame_as_grayscale(
    const VideoFrameRepresentation& vfr, FrameID frame_id,
    bool apply_level_scaling) {
  auto desc_opt = vfr.get_frame_descriptor(frame_id);
  auto params_opt = vfr.get_video_parameters();
  if (!desc_opt || !params_opt) {
    return PreviewImage{0, 0, {}, {}, {}};
  }

  const size_t height = desc_opt->height;
  const size_t width =
      static_cast<size_t>(params_opt->frame_width_nominal);
  if (height == 0 || width == 0) {
    return PreviewImage{0, 0, {}, {}, {}};
  }

  const int32_t blanking = params_opt->blanking_level;
  const int32_t white = params_opt->white_level;
  const int32_t range = (white > blanking) ? (white - blanking) : 1;

  PreviewImage img;
  img.width = static_cast<uint32_t>(width);
  img.height = static_cast<uint32_t>(height);
  img.rgb_data.reserve(width * height * 3);

  for (size_t line = 0; line < height; ++line) {
    const int16_t* line_ptr = vfr.get_line(frame_id, line);
    for (size_t s = 0; s < width; ++s) {
      const int32_t raw =
          line_ptr ? static_cast<int32_t>(line_ptr[s]) : blanking;
      uint8_t grey;
      if (apply_level_scaling) {
        const int32_t scaled = (raw - blanking) * 255 / range;
        grey = static_cast<uint8_t>(std::clamp(scaled, 0, 255));
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

// ---------------------------------------------------------------------------
// CVBSDecodedFrameRepresentation
// ---------------------------------------------------------------------------

// Immutable, lazily-decoded, cache-keyed-by-FrameID implementation of
// VideoFrameRepresentation for CVBS 4FSC sources.
//
// Inherits from both VideoFrameRepresentation (for consumers that need VFR)
// AND Artifact (so it can be returned as ArtifactPtr from execute()).
class CVBSDecodedFrameRepresentation final : public VideoFrameRepresentation,
                                             public Artifact {
 public:
  CVBSDecodedFrameRepresentation(
      VideoSystem system, int32_t frame_count, int32_t frame_samples,
      int32_t frame_height, int32_t spl_nominal,
      std::shared_ptr<ICVBSSourceStageDeps> deps, std::string input_path,
      std::string sample_encoding, SourceParameters video_params,
      std::optional<int32_t> ntsc_j_black_level,
      // Sidecars
      std::vector<DropoutRun> dropout_runs,
      bool has_audio, bool audio_locked_flag,
      std::string wav_path, uint32_t audio_pairs_per_frame,
      bool has_efm, std::string efm_data_path,
      std::vector<CVBSExtensionFrameRef> efm_table,
      bool has_ac3, std::string ac3_data_path,
      std::vector<CVBSExtensionFrameRef> ac3_table,
      ArtifactID artifact_id, Provenance provenance)
      : Artifact(std::move(artifact_id), std::move(provenance)),
        system_(system),
        frame_count_(static_cast<size_t>(frame_count)),
        frame_samples_(static_cast<size_t>(frame_samples)),
        frame_height_(static_cast<size_t>(frame_height)),
        spl_nominal_(static_cast<size_t>(spl_nominal)),
        deps_(std::move(deps)),
        input_path_(std::move(input_path)),
        sample_encoding_(std::move(sample_encoding)),
        video_params_(std::move(video_params)),
        ntsc_j_black_level_(ntsc_j_black_level),
        blanking_level_(video_params_.blanking_level),
        dropout_runs_(std::move(dropout_runs)),
        has_audio_(has_audio),
        audio_locked_flag_(audio_locked_flag),
        wav_path_(std::move(wav_path)),
        audio_pairs_per_frame_(audio_pairs_per_frame),
        has_efm_(has_efm),
        efm_data_path_(std::move(efm_data_path)),
        efm_table_(std::move(efm_table)),
        has_ac3_(has_ac3),
        ac3_data_path_(std::move(ac3_data_path)),
        ac3_table_(std::move(ac3_table)) {}

  // --------------------------------------------------------------------------
  // Artifact
  // --------------------------------------------------------------------------
  std::string type_name() const override {
    return "CVBSDecodedFrameRepresentation";
  }

  // --------------------------------------------------------------------------
  // Navigation
  // --------------------------------------------------------------------------
  FrameIDRange frame_range() const override {
    if (frame_count_ == 0) return FrameIDRange{0, 0};
    return FrameIDRange{0, static_cast<FrameID>(frame_count_)};
  }

  size_t frame_count() const override { return frame_count_; }

  bool has_frame(FrameID id) const override {
    return id < static_cast<FrameID>(frame_count_);
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
    desc.system = system_;
    desc.height = frame_height_;
    desc.samples_total = frame_samples_;
    desc.samples_per_line_nominal = spl_nominal_;
    desc.colour_frame_index = it->second.colour_frame_index;
    if (ntsc_j_black_level_.has_value()) {
      desc.black_level_override = ntsc_j_black_level_;
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
    if (!has_frame(id) || line >= frame_height_) return nullptr;
    const sample_type* frame = get_frame(id);
    if (!frame) return nullptr;
    const size_t line_offset = (system_ == VideoSystem::PAL)
                                   ? pal_line_offsets()[line]
                                   : line * spl_nominal_;
    return frame + line_offset;
  }

  std::vector<sample_type> get_frame_copy(FrameID id) const override {
    if (!has_frame(id)) return {};
    const sample_type* ptr = get_frame(id);
    if (!ptr) return {};
    return std::vector<sample_type>(ptr, ptr + frame_samples_);
  }

  // --------------------------------------------------------------------------
  // Hints
  // --------------------------------------------------------------------------
  std::vector<DropoutRun> get_dropout_hints(FrameID id) const override {
    std::vector<DropoutRun> result;
    for (const auto& run : dropout_runs_) {
      if (run.frame_id == id) result.push_back(run);
    }
    return result;
  }

  std::optional<int> get_frame_phase_hint(FrameID id) const override {
    if (!has_frame(id)) return std::nullopt;
    ensure_frame_cached(id);
    std::lock_guard<std::mutex> lock(cache_mutex_);
    const auto it = frame_cache_.find(id);
    if (it == frame_cache_.end()) return std::nullopt;
    const int idx = it->second.colour_frame_index;
    return (idx == -1) ? std::optional<int>{std::nullopt} : std::optional<int>{idx};
  }

  std::optional<ActiveLineHint> get_active_line_hint() const override {
    if (video_params_.first_active_frame_line < 0) return std::nullopt;
    ActiveLineHint hint;
    hint.first_active_frame_line = video_params_.first_active_frame_line;
    hint.last_active_frame_line = video_params_.last_active_frame_line;
    // Field-level active lines: approximately half the frame-level range.
    hint.first_active_field_line = video_params_.first_active_frame_line / 2;
    hint.last_active_field_line = video_params_.last_active_frame_line / 2;
    hint.source = HintSource::METADATA;
    return hint;
  }

  std::optional<SourceParameters> get_video_parameters() const override {
    return video_params_;
  }

  // --------------------------------------------------------------------------
  // Audio
  // --------------------------------------------------------------------------
  bool has_audio() const override { return has_audio_; }

  bool audio_locked() const override {
    return has_audio_ && audio_locked_flag_;
  }

  uint32_t get_audio_sample_count(FrameID id) const override {
    if (!has_audio_ || !audio_locked_flag_ || !has_frame(id)) return 0;
    return audio_pairs_per_frame_;
  }

  std::vector<int16_t> get_audio_samples(FrameID id) const override {
    if (!has_audio_ || !audio_locked_flag_ || !has_frame(id)) return {};
    const size_t pair_offset =
        static_cast<size_t>(id) * audio_pairs_per_frame_;
    return deps_->read_audio_samples_at(wav_path_, pair_offset,
                                        audio_pairs_per_frame_);
  }

  // --------------------------------------------------------------------------
  // EFM
  // --------------------------------------------------------------------------
  bool has_efm() const override { return has_efm_; }

  uint32_t get_efm_sample_count(FrameID id) const override {
    if (!has_efm_ || id >= efm_table_.size()) return 0;
    return efm_table_[static_cast<size_t>(id)].count;
  }

  std::vector<uint8_t> get_efm_samples(FrameID id) const override {
    if (!has_efm_ || id >= efm_table_.size()) return {};
    const auto& ref = efm_table_[static_cast<size_t>(id)];
    if (ref.count == 0) return {};
    return deps_->read_efm_bytes_at(efm_data_path_, ref.offset, ref.count);
  }

  // --------------------------------------------------------------------------
  // AC3 RF
  // --------------------------------------------------------------------------
  bool has_ac3_rf() const override { return has_ac3_; }

  uint32_t get_ac3_symbol_count(FrameID id) const override {
    if (!has_ac3_ || id >= ac3_table_.size()) return 0;
    return ac3_table_[static_cast<size_t>(id)].count;
  }

  std::vector<uint8_t> get_ac3_symbols(FrameID id) const override {
    if (!has_ac3_ || id >= ac3_table_.size()) return {};
    const auto& ref = ac3_table_[static_cast<size_t>(id)];
    if (ref.count == 0) return {};
    return deps_->read_ac3_bytes_at(ac3_data_path_, ref.offset, ref.count);
  }

 private:
  struct DecodedFrame {
    std::vector<sample_type> samples;
    int colour_frame_index = -1;
  };

  void ensure_frame_cached(FrameID id) const {
    {
      std::lock_guard<std::mutex> lock(cache_mutex_);
      if (frame_cache_.count(id)) return;
    }

    DecodedFrame decoded = decode_frame(id);

    std::lock_guard<std::mutex> lock(cache_mutex_);
    frame_cache_.try_emplace(id, std::move(decoded));
  }

  DecodedFrame decode_frame(FrameID id) const {
    const size_t word_offset = static_cast<size_t>(id) * frame_samples_;

    std::vector<uint16_t> raw_words;
    std::string err;
    if (!deps_->read_input_words_at(input_path_, word_offset, frame_samples_,
                                    raw_words, err)) {
      throw std::runtime_error("CVBS: failed to read frame " +
                               std::to_string(id) + " from '" + input_path_ +
                               "': " + err);
    }
    if (raw_words.size() < frame_samples_) {
      throw std::runtime_error("CVBS: short read at frame " +
                               std::to_string(id) + " in '" + input_path_ +
                               "'");
    }

    DecodedFrame result;
    result.samples.reserve(frame_samples_);
    for (size_t i = 0; i < frame_samples_; ++i) {
      result.samples.push_back(
          normalize_to_cvbs_u10(raw_words[i], sample_encoding_, blanking_level_));
    }

    result.colour_frame_index =
        measure_colour_frame_index(result.samples.data(), system_, blanking_level_);
    return result;
  }

  VideoSystem system_;
  size_t frame_count_;
  size_t frame_samples_;
  size_t frame_height_;
  size_t spl_nominal_;

  std::shared_ptr<ICVBSSourceStageDeps> deps_;
  std::string input_path_;
  std::string sample_encoding_;
  SourceParameters video_params_;
  std::optional<int32_t> ntsc_j_black_level_;
  int32_t blanking_level_;

  mutable std::mutex cache_mutex_;
  mutable std::unordered_map<FrameID, DecodedFrame> frame_cache_;

  std::vector<DropoutRun> dropout_runs_;

  bool has_audio_ = false;
  bool audio_locked_flag_ = false;
  std::string wav_path_;
  uint32_t audio_pairs_per_frame_ = 0;

  bool has_efm_ = false;
  std::string efm_data_path_;
  std::vector<CVBSExtensionFrameRef> efm_table_;

  bool has_ac3_ = false;
  std::string ac3_data_path_;
  std::vector<CVBSExtensionFrameRef> ac3_table_;
};

// ---------------------------------------------------------------------------
// CVBSSourceStageDeps — production filesystem / SQLite implementation
// ---------------------------------------------------------------------------

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
    std::ifstream ifs(input_path, std::ios::binary);
    if (!ifs.is_open()) {
      error_message =
          "CVBS source file is not readable: '" + input_path + "'";
      return false;
    }
    ifs.seekg(0, std::ios::end);
    if (!ifs.good() || ifs.tellg() <= 0) {
      error_message = "CVBS source file is empty: '" + input_path + "'";
      return false;
    }
    return true;
  }

  std::optional<CVBSMetadataRecord> load_metadata(
      const std::string& meta_path,
      std::string& error_message) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(meta_path, ec)) {
      error_message = "Metadata file not found: '" + meta_path + "'";
      return std::nullopt;
    }

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(meta_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr)
        != SQLITE_OK) {
      error_message = "Failed to open metadata '" + meta_path + "': " +
                      (db ? sqlite3_errmsg(db) : "unknown error");
      if (db) sqlite3_close(db);
      return std::nullopt;
    }

    constexpr const char* kSql =
        "SELECT preset, sample_encoding_preset, signal_state_preset, "
        "signal_type, number_of_sequential_frames, audio_locked, black_level "
        "FROM cvbs_file ORDER BY cvbs_file_id LIMIT 1";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
      error_message = "Failed to query cvbs_file from '" + meta_path +
                      "': " + sqlite3_errmsg(db);
      sqlite3_close(db);
      return std::nullopt;
    }

    if (sqlite3_step(stmt) != SQLITE_ROW) {
      error_message = "No cvbs_file row in '" + meta_path + "'";
      sqlite3_finalize(stmt);
      sqlite3_close(db);
      return std::nullopt;
    }

    auto col_str = [&](int col) -> std::string {
      const unsigned char* v = sqlite3_column_text(stmt, col);
      return v ? reinterpret_cast<const char*>(v) : "";
    };

    CVBSMetadataRecord rec;
    rec.preset = col_str(0);
    rec.sample_encoding_preset = col_str(1);
    rec.signal_state_preset = col_str(2);
    rec.signal_type = col_str(3);

    if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) {
      rec.number_of_sequential_frames = sqlite3_column_int(stmt, 4);
    }

    if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
      const std::string al = col_str(5);
      rec.audio_locked = (al == "TRUE" || al == "1");
    }

    if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) {
      rec.ntsc_j_black_level = sqlite3_column_int(stmt, 6);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return rec;
  }

  std::optional<size_t> get_input_word_count(
      const std::string& input_path,
      std::string& error_message) const override {
    std::ifstream ifs(input_path, std::ios::binary);
    if (!ifs.is_open()) {
      error_message = "Failed to open '" + input_path + "'";
      return std::nullopt;
    }
    ifs.seekg(0, std::ios::end);
    const std::streamoff sz = ifs.tellg();
    if (sz <= 0) {
      error_message = "Empty file: '" + input_path + "'";
      return std::nullopt;
    }
    if ((sz % 2) != 0) {
      error_message = "File size not 16-bit aligned: '" + input_path + "'";
      return std::nullopt;
    }
    return static_cast<size_t>(sz / 2);
  }

  bool read_input_words_at(const std::string& input_path, size_t word_offset,
                           size_t word_count, std::vector<uint16_t>& out_words,
                           std::string& error_message) const override {
    std::ifstream ifs(input_path, std::ios::binary);
    if (!ifs.is_open()) {
      error_message = "Failed to open '" + input_path + "'";
      return false;
    }
    ifs.seekg(static_cast<std::streamoff>(word_offset) * 2, std::ios::beg);
    if (!ifs.good()) {
      error_message = "Seek failed in '" + input_path + "'";
      return false;
    }
    out_words.resize(word_count);
    ifs.read(reinterpret_cast<char*>(out_words.data()),
             static_cast<std::streamsize>(word_count * 2));
    if (!ifs.good() && !ifs.eof()) {
      error_message = "Read failed from '" + input_path + "'";
      return false;
    }
    const size_t words_read = static_cast<size_t>(ifs.gcount()) / 2;
    if (words_read < word_count) out_words.resize(words_read);
    return true;
  }

  std::vector<DropoutRun> load_dropout_sidecar(
      const std::string& dropout_meta_path,
      std::string& error_message) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(dropout_meta_path, ec)) return {};

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(dropout_meta_path.c_str(), &db, SQLITE_OPEN_READONLY,
                        nullptr) != SQLITE_OK) {
      error_message = "Failed to open dropout sidecar '" +
                      dropout_meta_path + "'";
      if (db) sqlite3_close(db);
      return {};
    }

    constexpr const char* kSql =
        "SELECT frame_id, sample_start, sample_count, severity "
        "FROM dropout_run ORDER BY frame_id, sample_start";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, kSql, -1, &stmt, nullptr) != SQLITE_OK) {
      error_message = "Failed to query dropout_run from '" +
                      dropout_meta_path + "'";
      sqlite3_close(db);
      return {};
    }

    std::vector<DropoutRun> runs;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      DropoutRun run;
      run.frame_id =
          static_cast<FrameID>(sqlite3_column_int64(stmt, 0));
      run.sample_start =
          static_cast<uint64_t>(sqlite3_column_int64(stmt, 1));
      run.sample_count =
          static_cast<uint32_t>(sqlite3_column_int(stmt, 2));
      run.severity =
          static_cast<uint8_t>(sqlite3_column_int(stmt, 3));
      runs.push_back(run);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return runs;
  }

  std::optional<CVBSAudioSidecarInfo> get_audio_info(
      const std::string& wav_path) const override {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(wav_path, ec)) return std::nullopt;
    // The audio_locked flag comes from the .meta file, not the WAV.
    // This method simply confirms the WAV exists.
    return CVBSAudioSidecarInfo{false};
  }

  std::vector<int16_t> read_audio_samples_at(
      const std::string& wav_path, size_t stereo_pair_offset,
      size_t stereo_pair_count) const override {
    // WAV PCM: skip 44-byte RIFF header, then read interleaved int16_t pairs.
    constexpr size_t kWavHeaderBytes = 44;
    std::ifstream ifs(wav_path, std::ios::binary);
    if (!ifs.is_open()) return {};
    ifs.seekg(static_cast<std::streamoff>(kWavHeaderBytes +
                                         stereo_pair_offset * 4),
              std::ios::beg);
    if (!ifs.good()) return {};
    const size_t total_samples = stereo_pair_count * 2;
    std::vector<int16_t> buf(total_samples);
    ifs.read(reinterpret_cast<char*>(buf.data()),
             static_cast<std::streamsize>(total_samples * 2));
    const size_t words_read = static_cast<size_t>(ifs.gcount()) / 2;
    if (words_read < total_samples) buf.resize(words_read);
    return buf;
  }

  std::optional<std::vector<CVBSExtensionFrameRef>> load_efm_frame_table(
      const std::string& efm_meta_path,
      std::string& error_message) const override {
    return load_extension_frame_table(efm_meta_path, "efm_frame",
                                      error_message);
  }

  std::vector<uint8_t> read_efm_bytes_at(
      const std::string& efm_data_path, uint64_t byte_offset,
      uint32_t count) const override {
    return read_binary_at(efm_data_path, byte_offset, count);
  }

  std::optional<std::vector<CVBSExtensionFrameRef>> load_ac3_frame_table(
      const std::string& ac3_meta_path,
      std::string& error_message) const override {
    return load_extension_frame_table(ac3_meta_path, "ac3_frame",
                                      error_message);
  }

  std::vector<uint8_t> read_ac3_bytes_at(
      const std::string& ac3_data_path, uint64_t byte_offset,
      uint32_t count) const override {
    return read_binary_at(ac3_data_path, byte_offset, count);
  }

 private:
  // Shared table-loader for EFM and AC3 sidecars.
  // CVBS EFM extension format §3 / AC3 extension format §3.
  std::optional<std::vector<CVBSExtensionFrameRef>> load_extension_frame_table(
      const std::string& meta_path, const std::string& table_name,
      std::string& error_message) const {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(meta_path, ec)) return std::nullopt;

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(meta_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr)
        != SQLITE_OK) {
      error_message = "Failed to open '" + meta_path + "'";
      if (db) sqlite3_close(db);
      return std::nullopt;
    }

    const std::string sql =
        "SELECT t_value_offset, t_value_count FROM " + table_name +
        " ORDER BY frame_id";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
      error_message = "Failed to query " + table_name + " from '" +
                      meta_path + "'";
      sqlite3_close(db);
      return std::nullopt;
    }

    std::vector<CVBSExtensionFrameRef> table;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
      CVBSExtensionFrameRef ref;
      ref.offset = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
      ref.count = static_cast<uint32_t>(sqlite3_column_int(stmt, 1));
      table.push_back(ref);
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return table;
  }

  std::vector<uint8_t> read_binary_at(const std::string& path,
                                      uint64_t byte_offset,
                                      uint32_t count) const {
    if (count == 0) return {};
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs.is_open()) return {};
    ifs.seekg(static_cast<std::streamoff>(byte_offset), std::ios::beg);
    if (!ifs.good()) return {};
    std::vector<uint8_t> buf(count);
    ifs.read(reinterpret_cast<char*>(buf.data()),
             static_cast<std::streamsize>(count));
    const size_t bytes_read = static_cast<size_t>(ifs.gcount());
    if (bytes_read < count) buf.resize(bytes_read);
    return buf;
  }
};

// ---------------------------------------------------------------------------
// Stage identity constants
// ---------------------------------------------------------------------------

struct CVBSStageStaticInfo {
  const char* stage_name;
  const char* initial_display_name;  // set before any file is loaded
  const char* description;
  VideoFormatCompatibility compatible_formats;
  VideoSystem system;
};

constexpr CVBSStageStaticInfo kPALInfo{
    "PAL_CVBS_Source",
    "PAL CVBS Source",
    "PAL CVBS source - loads PAL 4FSC CVBS files",
    VideoFormatCompatibility::PAL_ONLY,
    VideoSystem::PAL,
};

constexpr CVBSStageStaticInfo kNTSCInfo{
    "NTSC_CVBS_Source",
    "NTSC CVBS Source",
    "NTSC CVBS source - loads NTSC 4FSC CVBS files",
    VideoFormatCompatibility::NTSC_ONLY,
    VideoSystem::NTSC,
};

// PAL_M uses PAL_ONLY compatibility: the VideoFormatCompatibility enum covers
// both PAL and PAL-M under the PAL_ONLY value per the enum definition.
constexpr CVBSStageStaticInfo kPALMInfo{
    "PALM_CVBS_Source",
    "PALM CVBS Source",
    "PAL-M CVBS source - loads PAL-M 4FSC CVBS files",
    VideoFormatCompatibility::PAL_ONLY,
    VideoSystem::PAL_M,
};

// Return the video-system name for logging and display.
const char* system_name(VideoSystem sys) {
  switch (sys) {
    case VideoSystem::PAL:   return "PAL";
    case VideoSystem::NTSC:  return "NTSC";
    case VideoSystem::PAL_M: return "PAL_M";
    default:                  return "Unknown";
  }
}

}  // namespace

// ---------------------------------------------------------------------------
// FixedFormatCVBSSourceStage
// ---------------------------------------------------------------------------

FixedFormatCVBSSourceStage::FixedFormatCVBSSourceStage(
    const char* stage_name, const char* fixed_display_name,
    const char* description, VideoFormatCompatibility compatible_formats,
    VideoSystem system, std::shared_ptr<ICVBSSourceStageDeps> deps)
    : system_(system),
      stage_name_(stage_name),
      display_name_(fixed_display_name),
      description_(description),
      compatible_formats_(compatible_formats),
      deps_(deps ? std::move(deps) : std::make_shared<CVBSSourceStageDeps>()) {}

std::vector<ArtifactPtr> FixedFormatCVBSSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context) {
  std::lock_guard<std::mutex> lock(execute_mutex_);
  (void)observation_context;

  if (!inputs.empty()) {
    throw std::runtime_error(std::string(stage_name_) +
                             ": source stage expects no inputs");
  }

  auto it = parameters.find("input_path");
  if (it == parameters.end() ||
      std::get<std::string>(it->second).empty()) {
    ORC_LOG_DEBUG("{}: no input_path configured", stage_name_);
    return {};
  }
  const std::string input_path = std::get<std::string>(it->second);

  if (cached_representation_ && cached_input_path_ == input_path) {
    return {cached_representation_};
  }

  // --- Validate input file ---
  std::string err;
  if (!deps_->validate_input_file(input_path, err)) {
    throw UserDataError(err);
  }

  // --- Load and validate metadata ---
  const std::string meta_path = derive_sidecar_path(input_path, ".meta");
  std::string meta_err;
  const auto meta_opt = deps_->load_metadata(meta_path, meta_err);
  if (!meta_opt) {
    throw UserDataError("Failed to load CVBS metadata from '" + meta_path +
                        "': " + meta_err);
  }
  const CVBSMetadataRecord& meta = *meta_opt;

  // Hard-reject non-STANDARD_TBC_LOCKED signal states.
  if (meta.signal_state_preset != "STANDARD_TBC_LOCKED") {
    throw UserDataError(
        "CVBS source '" + input_path +
        "' has signal_state_preset '" + meta.signal_state_preset +
        "'. Only STANDARD_TBC_LOCKED files are accepted.");
  }

  // Validate that the .meta video standard matches this stage's fixed system.
  if (meta.preset != std::string(system_name(system_))) {
    throw UserDataError(
        "CVBS metadata preset '" + meta.preset + "' in '" + meta_path +
        "' does not match this stage's system (" +
        system_name(system_) + ")");
  }

  // Accept all four declared sample encodings.
  const std::string& encoding = meta.sample_encoding_preset;
  if (encoding != "CVBS_U10_4FSC" && encoding != "CVBS_U16_4FSC" &&
      encoding != "CVBS_TPG21_4FSC" && encoding != "CVBS_S16_FSC") {
    throw UserDataError(
        "Unsupported sample_encoding_preset '" + encoding + "' in '" +
        meta_path + "'");
  }

  // --- Determine frame geometry ---
  const int32_t frame_samples = [&]() -> int32_t {
    switch (system_) {
      case VideoSystem::PAL:   return kPalFrameSamples;
      case VideoSystem::NTSC:  return kNtscFrameSamples;
      case VideoSystem::PAL_M: return kPalMFrameSamples;
      default: return 0;
    }
  }();

  if (frame_samples == 0) {
    throw std::runtime_error(std::string(stage_name_) +
                             ": unknown video system");
  }

  std::string wc_err;
  const auto wc_opt = deps_->get_input_word_count(input_path, wc_err);
  if (!wc_opt) {
    throw UserDataError("Failed to measure CVBS payload '" + input_path +
                        "': " + wc_err);
  }
  const size_t total_words = *wc_opt;

  // Prefer number_of_sequential_frames from metadata; fall back to measured.
  int32_t frame_count = 0;
  if (meta.number_of_sequential_frames > 0) {
    frame_count = meta.number_of_sequential_frames;
    // Sanity-check that the file is large enough.
    const size_t expected = static_cast<size_t>(frame_count) *
                            static_cast<size_t>(frame_samples);
    if (total_words < expected) {
      throw UserDataError(
          "CVBS payload '" + input_path + "' has " +
          std::to_string(total_words) +
          " words but metadata declares " + std::to_string(frame_count) +
          " frames (" + std::to_string(expected) + " words required)");
    }
  } else {
    frame_count = static_cast<int32_t>(total_words /
                                       static_cast<size_t>(frame_samples));
  }

  if (frame_count == 0) {
    throw UserDataError("CVBS payload '" + input_path +
                        "' is too short for one complete " +
                        system_name(system_) + " frame");
  }

  const size_t trailing =
      total_words -
      static_cast<size_t>(frame_count) * static_cast<size_t>(frame_samples);
  if (trailing > 0) {
    ORC_LOG_WARN("{}: {} trailing samples in '{}' (not a complete frame)",
                 stage_name_, trailing, input_path);
  }

  // --- Build SourceParameters from spec constants ---
  const int32_t ntsc_j =
      meta.ntsc_j_black_level.value_or(-1);
  const SourceParameters src_params =
      build_source_parameters(system_, frame_count, ntsc_j);

  // --- Load sidecars ---

  // Dropout
  std::string do_err;
  const std::string dropout_path =
      derive_sidecar_path(input_path, ".dropouts.meta");
  auto dropout_runs = deps_->load_dropout_sidecar(dropout_path, do_err);

  // Audio
  const std::string wav_path =
      derive_sidecar_path(input_path, "_audio_00.wav");
  const auto audio_info = deps_->get_audio_info(wav_path);
  const bool has_audio = audio_info.has_value();
  const bool audio_locked_flag =
      has_audio && meta.audio_locked.value_or(false);

  // Audio pairs per frame: PAL 44100/25=1764, NTSC/PAL_M 44100×1001/30000≈1470.
  uint32_t audio_pairs = 0;
  if (has_audio && audio_locked_flag) {
    audio_pairs = (system_ == VideoSystem::PAL) ? 1764u : 1470u;
  }

  // EFM
  const std::string efm_meta_path =
      derive_sidecar_path(input_path, ".efm.meta");
  const std::string efm_data_path =
      derive_sidecar_path(input_path, ".efm");
  std::string efm_err;
  const auto efm_table_opt =
      deps_->load_efm_frame_table(efm_meta_path, efm_err);
  const bool has_efm = efm_table_opt.has_value();
  std::vector<CVBSExtensionFrameRef> efm_table =
      has_efm ? *efm_table_opt : std::vector<CVBSExtensionFrameRef>{};

  // AC3
  const std::string ac3_meta_path =
      derive_sidecar_path(input_path, ".ac3.meta");
  const std::string ac3_data_path =
      derive_sidecar_path(input_path, ".ac3");
  std::string ac3_err;
  const auto ac3_table_opt =
      deps_->load_ac3_frame_table(ac3_meta_path, ac3_err);
  const bool has_ac3 = ac3_table_opt.has_value();
  std::vector<CVBSExtensionFrameRef> ac3_table =
      has_ac3 ? *ac3_table_opt : std::vector<CVBSExtensionFrameRef>{};

  // --- Display name update ---
  const std::string signal_type_display =
      (meta.signal_type == "yc") ? "YC" : "Composite";
  display_name_ = std::string(system_name(system_)) + " CVBS " +
                  signal_type_display;

  ORC_LOG_INFO("{}: loaded '{}' — {} {} frames, encoding {}, audio {}, "
               "EFM {}, AC3 {}",
               stage_name_, input_path, frame_count, system_name(system_),
               encoding, has_audio ? "yes" : "no",
               has_efm ? "yes" : "no", has_ac3 ? "yes" : "no");

  // --- Frame geometry parameters ---
  const int32_t frame_height_lines = [&]() -> int32_t {
    switch (system_) {
      case VideoSystem::PAL:   return kPalFrameLines;
      case VideoSystem::NTSC:  return kNtscFrameLines;
      case VideoSystem::PAL_M: return kPalMFrameLines;
      default: return 525;
    }
  }();

  // --- Provenance ---
  Provenance prov;
  prov.stage_name = stage_name_;
  prov.stage_version = version();
  prov.parameters = {
      {"input_path", input_path},
      {"video_system", system_name(system_)},
      {"sample_encoding", encoding},
  };

  auto representation = std::make_shared<CVBSDecodedFrameRepresentation>(
      system_, frame_count, frame_samples, frame_height_lines,
      src_params.frame_width_nominal, deps_, input_path, encoding,
      src_params, meta.ntsc_j_black_level,
      std::move(dropout_runs),
      has_audio, audio_locked_flag, wav_path, audio_pairs,
      has_efm, efm_data_path, std::move(efm_table),
      has_ac3, ac3_data_path, std::move(ac3_table),
      ArtifactID(std::string(stage_name_) + ":" + input_path),
      std::move(prov));

  cached_representation_ = representation;
  cached_input_path_ = input_path;
  input_path_ = input_path;
  return {representation};
}

std::vector<ParameterDescriptor> FixedFormatCVBSSourceStage::get_parameter_descriptors(
    VideoSystem /*project_format*/, SourceType /*source_type*/) const {
  std::vector<ParameterDescriptor> desc;

  ParameterDescriptor pd;
  pd.name = "input_path";
  pd.display_name = "CVBS File Path";
  pd.description =
      "Path to the CVBS data file (.composite or .y/.c). "
      "A <basename>.meta sidecar is required.";
  pd.type = ParameterType::FILE_PATH;
  pd.constraints.required = false;
  pd.constraints.default_value = std::string("");
  pd.file_extension_hint = ".composite";
  desc.push_back(pd);

  return desc;
}

std::map<std::string, ParameterValue>
FixedFormatCVBSSourceStage::get_parameters() const {
  return {{"input_path", input_path_}};
}

bool FixedFormatCVBSSourceStage::set_parameters(
    const std::map<std::string, ParameterValue>& params) {
  for (const auto& [key, value] : params) {
    if (key == "input_path") {
      input_path_ = std::get<std::string>(value);
    } else {
      ORC_LOG_WARN("{}: unknown parameter '{}'", stage_name_, key);
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
  if (!cached_representation_) return {};

  auto vfr = std::dynamic_pointer_cast<VideoFrameRepresentation>(
      cached_representation_);
  if (!vfr) return {};

  const size_t fc = vfr->frame_count();
  if (fc == 0) return {};

  auto params_opt = vfr->get_video_parameters();
  if (!params_opt) return {};

  const uint32_t w =
      static_cast<uint32_t>(params_opt->frame_width_nominal);
  const uint32_t h =
      static_cast<uint32_t>(params_opt->frame_height);

  // Display aspect ratio from active video region (target 4:3).
  double dar_correction = 0.7;
  if (params_opt->active_video_start >= 0 &&
      params_opt->active_video_end > params_opt->active_video_start &&
      params_opt->first_active_frame_line >= 0 &&
      params_opt->last_active_frame_line >
          params_opt->first_active_frame_line) {
    const double aw = static_cast<double>(params_opt->active_video_end -
                                          params_opt->active_video_start);
    const double ah = static_cast<double>(params_opt->last_active_frame_line -
                                          params_opt->first_active_frame_line);
    dar_correction = (4.0 / 3.0) / (aw / ah);
  }

  return {
      PreviewOption{"frame", "Frame (Scaled)", false, w, h,
                    static_cast<uint64_t>(fc), dar_correction},
      PreviewOption{"frame_raw", "Frame (Raw)", false, w, h,
                    static_cast<uint64_t>(fc), dar_correction},
  };
}

PreviewImage FixedFormatCVBSSourceStage::render_preview(
    const std::string& option_id, uint64_t index,
    PreviewNavigationHint /*hint*/) const {
  if (!cached_representation_) {
    return PreviewImage{0, 0, {}, {}, {}};
  }

  auto vfr = std::dynamic_pointer_cast<VideoFrameRepresentation>(
      cached_representation_);
  if (!vfr || !vfr->has_frame(static_cast<FrameID>(index))) {
    return PreviewImage{0, 0, {}, {}, {}};
  }

  const bool apply_scaling = (option_id != "frame_raw");
  return render_vfr_frame_as_grayscale(*vfr, static_cast<FrameID>(index),
                                       apply_scaling);
}

std::optional<StageReport> FixedFormatCVBSSourceStage::generate_report() const {
  StageReport report;
  report.summary = display_name_ + " Status";

  if (input_path_.empty()) {
    report.items.push_back({"Source File", "Not configured"});
    report.items.push_back({"Status", "No input file path set"});
    return report;
  }

  report.items.push_back({"Source File", input_path_});
  report.items.push_back({"Video System", system_name(system_)});

  if (cached_representation_) {
    auto vfr = std::dynamic_pointer_cast<VideoFrameRepresentation>(
        cached_representation_);
    if (vfr) {
      auto params = vfr->get_video_parameters();
      if (params) {
        report.items.push_back(
            {"Frame Count",
             std::to_string(params->number_of_sequential_frames)});
        report.items.push_back(
            {"Frame Size",
             std::to_string(params->frame_width_nominal) + " × " +
                 std::to_string(params->frame_height)});
        report.items.push_back({"Blanking Level",
                                 std::to_string(params->blanking_level)});
        report.items.push_back({"White Level",
                                 std::to_string(params->white_level)});
        report.items.push_back(
            {"Audio", vfr->has_audio()
                          ? (vfr->audio_locked() ? "Frame-locked" : "Free-running")
                          : "None"});
        report.items.push_back(
            {"EFM", vfr->has_efm() ? "Present" : "None"});
        report.items.push_back(
            {"AC3 RF", vfr->has_ac3_rf() ? "Present" : "None"});

        report.metrics["frame_count"] =
            static_cast<int64_t>(params->number_of_sequential_frames);
        report.metrics["frame_width"] =
            static_cast<int64_t>(params->frame_width_nominal);
        report.metrics["frame_height"] =
            static_cast<int64_t>(params->frame_height);
      }
      report.items.push_back({"Status", "Loaded"});
    }
  } else {
    report.items.push_back({"Status", "Not yet loaded"});
  }

  return report;
}

// ---------------------------------------------------------------------------
// Concrete stage constructors
// ---------------------------------------------------------------------------

PALCVBSSourceStage::PALCVBSSourceStage(
    std::shared_ptr<ICVBSSourceStageDeps> deps)
    : FixedFormatCVBSSourceStage(kPALInfo.stage_name,
                                  kPALInfo.initial_display_name,
                                  kPALInfo.description,
                                  kPALInfo.compatible_formats,
                                  kPALInfo.system,
                                  std::move(deps)) {}

NTSCCVBSSourceStage::NTSCCVBSSourceStage(
    std::shared_ptr<ICVBSSourceStageDeps> deps)
    : FixedFormatCVBSSourceStage(kNTSCInfo.stage_name,
                                  kNTSCInfo.initial_display_name,
                                  kNTSCInfo.description,
                                  kNTSCInfo.compatible_formats,
                                  kNTSCInfo.system,
                                  std::move(deps)) {}

PALMCVBSSourceStage::PALMCVBSSourceStage(
    std::shared_ptr<ICVBSSourceStageDeps> deps)
    : FixedFormatCVBSSourceStage(kPALMInfo.stage_name,
                                  kPALMInfo.initial_display_name,
                                  kPALMInfo.description,
                                  kPALMInfo.compatible_formats,
                                  kPALMInfo.system,
                                  std::move(deps)) {}

}  // namespace orc
