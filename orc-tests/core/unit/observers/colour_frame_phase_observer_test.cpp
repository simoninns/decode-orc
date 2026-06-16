/*
 * File:        colour_frame_phase_observer_test.cpp
 * Module:      orc-tests/core/unit/observers
 * Purpose:     Unit tests for ColourFramePhaseObserver
 *
 * Tests: PAL/NTSC/PAL_M phase classification, absent-burst handling, and
 * observation-context storage.
 * Frame data is synthesised in memory; no I/O is performed.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../../../../orc/core/observers/colour_frame_phase_observer.h"

#include <cvbs_signal_constants.h>
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "../../../../orc/common/include/common_types.h"
#include "../../../../orc/core/include/field_id.h"
#include "../../../../orc/core/include/frame_descriptor.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/core/include/video_frame_representation.h"

namespace orc {
namespace tests {

namespace {

// ---------------------------------------------------------------------------
// Minimal in-memory VFR backed by a flat sample buffer
// ---------------------------------------------------------------------------

class FlatBufferVFR : public VideoFrameRepresentation {
 public:
  FlatBufferVFR(VideoSystem system, std::vector<int16_t> samples,
                SourceParameters params)
      : system_(system),
        samples_(std::move(samples)),
        params_(std::move(params)) {}

  FrameIDRange frame_range() const override { return {0, 1}; }
  size_t frame_count() const override { return 1; }
  bool has_frame(FrameID id) const override { return id == FrameID{0}; }

  std::optional<FrameDescriptor> get_frame_descriptor(
      FrameID id) const override {
    if (id != FrameID{0}) return std::nullopt;
    FrameDescriptor d;
    d.frame_id = id;
    d.system = system_;
    d.height = static_cast<size_t>(params_.frame_height);
    d.samples_total = samples_.size();
    d.samples_per_line_nominal =
        static_cast<size_t>(params_.frame_width_nominal);
    return d;
  }

  const sample_type* get_frame(FrameID id) const override {
    return (id == FrameID{0}) ? samples_.data() : nullptr;
  }

  std::vector<sample_type> get_frame_copy(FrameID id) const override {
    return (id == FrameID{0}) ? samples_ : std::vector<sample_type>{};
  }

  std::optional<SourceParameters> get_video_parameters() const override {
    return params_;
  }

 private:
  VideoSystem system_;
  std::vector<int16_t> samples_;
  SourceParameters params_;
};

// ---------------------------------------------------------------------------
// Frame builders
// ---------------------------------------------------------------------------

SourceParameters make_pal_params() {
  SourceParameters p{};
  p.system = VideoSystem::PAL;
  p.frame_width_nominal = kPalSamplesPerLineNominal;
  p.frame_height = kPalFrameLines;
  p.blanking_level = kPalBlanking;
  p.white_level = kPalWhite;
  return p;
}

SourceParameters make_ntsc_params() {
  SourceParameters p{};
  p.system = VideoSystem::NTSC;
  p.frame_width_nominal = kNtscSamplesPerLine;
  p.frame_height = kNtscFrameLines;
  p.blanking_level = kNtscBlanking;
  p.white_level = kNtscWhite;
  return p;
}

SourceParameters make_palm_params() {
  SourceParameters p{};
  p.system = VideoSystem::PAL_M;
  p.frame_width_nominal = kPalMSamplesPerLine;
  p.frame_height = kPalMFrameLines;
  p.blanking_level = kNtscBlanking;
  p.white_level = kNtscWhite;
  return p;
}

// Build a PAL frame with a colour burst at line 9, sample 93.
// burst_phase_deg: carrier phase injected at the burst position.
// amp: burst amplitude in ADU.
std::vector<int16_t> make_pal_frame(double burst_phase_deg, int32_t amp) {
  const size_t n_words = static_cast<size_t>(kPalFrameSamples);
  std::vector<int16_t> buf(n_words, static_cast<int16_t>(kPalBlanking));

  const size_t line9_offset =
      static_cast<size_t>(9 * (kPalSamplesPerLineNominal));
  const size_t burst_abs = line9_offset + 93;
  const double phi_rad = burst_phase_deg * (M_PI / 180.0);

  for (int n = 0; n < 40; ++n) {
    const double carrier_phase =
        static_cast<double>((burst_abs + static_cast<size_t>(n)) % 4) *
        (M_PI / 2.0);
    const double sample =
        static_cast<double>(kPalBlanking) +
        static_cast<double>(amp) * std::cos(carrier_phase + phi_rad);
    buf[burst_abs + static_cast<size_t>(n)] =
        static_cast<int16_t>(std::lround(sample));
  }
  return buf;
}

// Build an NTSC frame with a colour burst at line 9, sample 74.
std::vector<int16_t> make_ntsc_frame(double burst_phase_deg, int32_t amp) {
  const size_t n_words = static_cast<size_t>(kNtscFrameSamples);
  std::vector<int16_t> buf(n_words, static_cast<int16_t>(kNtscBlanking));

  const size_t line9_offset = static_cast<size_t>(9 * kNtscSamplesPerLine);
  const size_t burst_abs = line9_offset + 74;
  const double phi_rad = burst_phase_deg * (M_PI / 180.0);

  for (int n = 0; n < 40; ++n) {
    const double carrier_phase =
        static_cast<double>((burst_abs + static_cast<size_t>(n)) % 4) *
        (M_PI / 2.0);
    const double sample =
        static_cast<double>(kNtscBlanking) +
        static_cast<double>(amp) * std::cos(carrier_phase + phi_rad);
    buf[burst_abs + static_cast<size_t>(n)] =
        static_cast<int16_t>(std::lround(sample));
  }
  return buf;
}

// Build a PAL_M frame with a colour burst at line 9, sample 74.
std::vector<int16_t> make_palm_frame(double burst_phase_deg, int32_t amp) {
  const size_t n_words = static_cast<size_t>(kPalMFrameSamples);
  std::vector<int16_t> buf(n_words, static_cast<int16_t>(kNtscBlanking));

  const size_t line9_offset = static_cast<size_t>(9 * kPalMSamplesPerLine);
  const size_t burst_abs = line9_offset + 74;
  const double phi_rad = burst_phase_deg * (M_PI / 180.0);

  for (int n = 0; n < 40; ++n) {
    const double carrier_phase =
        static_cast<double>((burst_abs + static_cast<size_t>(n)) % 4) *
        (M_PI / 2.0);
    const double sample =
        static_cast<double>(kNtscBlanking) +
        static_cast<double>(amp) * std::cos(carrier_phase + phi_rad);
    buf[burst_abs + static_cast<size_t>(n)] =
        static_cast<int16_t>(std::lround(sample));
  }
  return buf;
}

// Helper: run the observer on one frame and return the stored
// colour_frame_index.
std::optional<int32_t> observe_frame(const VideoFrameRepresentation& vfr) {
  ObservationContext ctx;
  ColourFramePhaseObserver obs;
  obs.process_frame(vfr, FrameID{0}, ctx);

  const auto val =
      ctx.get(FieldID(0), "colour_frame_phase", "colour_frame_index");
  if (!val || !std::holds_alternative<int32_t>(*val)) {
    return std::nullopt;
  }
  return std::get<int32_t>(*val);
}

}  // namespace

// ===========================================================================
// Observer identity
// ===========================================================================

TEST(ColourFramePhaseObserverTest, ObserverName) {
  ColourFramePhaseObserver obs;
  EXPECT_EQ(obs.observer_name(), "ColourFramePhaseObserver");
}

TEST(ColourFramePhaseObserverTest, ProvidesColourFrameIndexObservation) {
  ColourFramePhaseObserver obs;
  const auto keys = obs.get_provided_observations();
  ASSERT_EQ(keys.size(), 1u);
  EXPECT_EQ(keys[0].namespace_, "colour_frame_phase");
  EXPECT_EQ(keys[0].name, "colour_frame_index");
  EXPECT_EQ(keys[0].type, ObservationType::INT32);
}

// ===========================================================================
// PAL phase classification
// ===========================================================================
// PAL line 9 burst abs_offset = 9*1135 + 93 = 10308, phase_base = 0.
// burst[n] = A * cos(n*90° + φ).  Measured angle = −φ (mod 360°).
// sector [0°,90°)→1, [90°,180°)→2, [180°,270°)→3, [270°,360°)→4.

TEST(ColourFramePhaseObserverTest, PAL_BurstAt315Deg_ColourFrameIndex1) {
  // φ=315° → measured = 45° → sector 0 → index 1.
  FlatBufferVFR vfr(VideoSystem::PAL, make_pal_frame(315.0, 100),
                    make_pal_params());
  const auto idx = observe_frame(vfr);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 1);
}

TEST(ColourFramePhaseObserverTest, PAL_BurstAt225Deg_ColourFrameIndex2) {
  // φ=225° → measured = 135° → sector 1 → index 2.
  FlatBufferVFR vfr(VideoSystem::PAL, make_pal_frame(225.0, 100),
                    make_pal_params());
  const auto idx = observe_frame(vfr);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 2);
}

TEST(ColourFramePhaseObserverTest, PAL_BurstAt135Deg_ColourFrameIndex3) {
  // φ=135° → measured = 225° → sector 2 → index 3.
  FlatBufferVFR vfr(VideoSystem::PAL, make_pal_frame(135.0, 100),
                    make_pal_params());
  const auto idx = observe_frame(vfr);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 3);
}

TEST(ColourFramePhaseObserverTest, PAL_BurstAt45Deg_ColourFrameIndex4) {
  // φ=45° → measured = 315° → sector 3 → index 4.
  FlatBufferVFR vfr(VideoSystem::PAL, make_pal_frame(45.0, 100),
                    make_pal_params());
  const auto idx = observe_frame(vfr);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 4);
}

TEST(ColourFramePhaseObserverTest, PAL_BurstAbsent_StoresMinusOne) {
  // All-blanking frame: amplitude below threshold → colour_frame_index = -1.
  std::vector<int16_t> blank(static_cast<size_t>(kPalFrameSamples),
                             static_cast<int16_t>(kPalBlanking));
  FlatBufferVFR vfr(VideoSystem::PAL, std::move(blank), make_pal_params());
  const auto idx = observe_frame(vfr);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, -1);
}

// ===========================================================================
// NTSC phase classification
// ===========================================================================
// SMPTE 244M-2003 §3.2: Frame A (~180°) → index 0; Frame B (~0°) → index 1.

TEST(ColourFramePhaseObserverTest, NTSC_BurstAt180Deg_ColourFrameIndex0) {
  FlatBufferVFR vfr(VideoSystem::NTSC, make_ntsc_frame(180.0, 100),
                    make_ntsc_params());
  const auto idx = observe_frame(vfr);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 0);
}

TEST(ColourFramePhaseObserverTest, NTSC_BurstAt0Deg_ColourFrameIndex1) {
  FlatBufferVFR vfr(VideoSystem::NTSC, make_ntsc_frame(0.0, 100),
                    make_ntsc_params());
  const auto idx = observe_frame(vfr);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 1);
}

// ===========================================================================
// PAL_M phase classification
// ===========================================================================
// ITU-R BT.1700-1 Annex 1 Part B: sector [0°,90°)→1, [90°,180°)→4,
// [180°,270°)→3, [270°,360°)→2.

TEST(ColourFramePhaseObserverTest, PALM_BurstAt315Deg_ColourFrameIndex1) {
  // φ=315° → measured = 45° → sector 0 → index 1.
  FlatBufferVFR vfr(VideoSystem::PAL_M, make_palm_frame(315.0, 100),
                    make_palm_params());
  const auto idx = observe_frame(vfr);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 1);
}

TEST(ColourFramePhaseObserverTest, PALM_BurstAt225Deg_ColourFrameIndex4) {
  // φ=225° → measured = 135° → sector 1 → index 4.
  FlatBufferVFR vfr(VideoSystem::PAL_M, make_palm_frame(225.0, 100),
                    make_palm_params());
  const auto idx = observe_frame(vfr);
  ASSERT_TRUE(idx.has_value());
  EXPECT_EQ(*idx, 4);
}

// ===========================================================================
// Context storage
// ===========================================================================

TEST(ColourFramePhaseObserverTest, StoresAtFirstFieldOfFrame) {
  // Observation must be keyed by FieldID(frame_id * 2).
  FlatBufferVFR vfr(VideoSystem::PAL, make_pal_frame(315.0, 100),
                    make_pal_params());
  ObservationContext ctx;
  ColourFramePhaseObserver obs;
  obs.process_frame(vfr, FrameID{0}, ctx);

  // First field of frame 0 → FieldID(0).
  EXPECT_TRUE(ctx.has(FieldID(0), "colour_frame_phase", "colour_frame_index"));
  // Second field key is not written.
  EXPECT_FALSE(ctx.has(FieldID(1), "colour_frame_phase", "colour_frame_index"));
}

}  // namespace tests
}  // namespace orc
