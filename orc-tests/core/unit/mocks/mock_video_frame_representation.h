/*
 * File:        mock_video_frame_representation.h
 * Module:      orc-core-tests
 * Purpose:     GoogleMock implementation of VideoFrameRepresentation for tests
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <gmock/gmock.h>
#include <orc/stage/video_frame_representation.h>

// Using a distinct namespace to allow test files to use the same type names as
// the module under test without collision.
namespace orc_unit_test {

class MockVideoFrameRepresentation : public orc::VideoFrameRepresentation {
 public:
  using sample_type = orc::VideoFrameRepresentation::sample_type;

  // Navigation
  MOCK_METHOD(orc::FrameIDRange, frame_range, (), (const, override));
  MOCK_METHOD(size_t, frame_count, (), (const, override));
  MOCK_METHOD(bool, has_frame, (orc::FrameID), (const, override));
  MOCK_METHOD(std::optional<orc::FrameDescriptor>, get_frame_descriptor,
              (orc::FrameID), (const, override));

  // Flat access
  MOCK_METHOD(const sample_type*, get_frame, (orc::FrameID), (const, override));
  MOCK_METHOD(const sample_type*, get_line, (orc::FrameID, size_t),
              (const, override));
  MOCK_METHOD((std::vector<sample_type>), get_frame_copy, (orc::FrameID),
              (const, override));

  // YC
  MOCK_METHOD(bool, has_separate_channels, (), (const, override));
  MOCK_METHOD(const sample_type*, get_frame_luma, (orc::FrameID),
              (const, override));
  MOCK_METHOD(const sample_type*, get_frame_chroma, (orc::FrameID),
              (const, override));
  MOCK_METHOD(const sample_type*, get_line_luma, (orc::FrameID, size_t),
              (const, override));
  MOCK_METHOD(const sample_type*, get_line_chroma, (orc::FrameID, size_t),
              (const, override));

  // Hints
  MOCK_METHOD((std::vector<orc::DropoutRun>), get_dropout_hints, (orc::FrameID),
              (const, override));
  MOCK_METHOD((std::optional<orc::SourceParameters>), get_video_parameters, (),
              (const, override));

  // Audio tracks
  MOCK_METHOD(size_t, audio_track_count, (), (const, override));
  MOCK_METHOD((std::optional<orc::AudioTrackDescriptor>),
              get_audio_track_descriptor, (size_t), (const, override));
  MOCK_METHOD(uint32_t, get_audio_sample_count, (size_t, orc::FrameID),
              (const, override));
  MOCK_METHOD((std::vector<int16_t>), get_audio_samples, (size_t, orc::FrameID),
              (const, override));
  MOCK_METHOD(uint64_t, get_audio_stream_pair_count, (size_t),
              (const, override));
  MOCK_METHOD((std::vector<int16_t>), get_audio_stream_samples,
              (size_t, uint64_t, uint32_t), (const, override));

  // EFM
  MOCK_METHOD(bool, has_efm, (), (const, override));
  MOCK_METHOD(uint32_t, get_efm_sample_count, (orc::FrameID),
              (const, override));
  MOCK_METHOD((std::vector<uint8_t>), get_efm_samples, (orc::FrameID),
              (const, override));

  // AC3 RF
  MOCK_METHOD(bool, has_ac3_rf, (), (const, override));
  MOCK_METHOD(uint32_t, get_ac3_symbol_count, (orc::FrameID),
              (const, override));
  MOCK_METHOD((std::vector<uint8_t>), get_ac3_symbols, (orc::FrameID),
              (const, override));
};

}  // namespace orc_unit_test
