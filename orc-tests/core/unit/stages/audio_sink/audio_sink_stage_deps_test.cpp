/*
 * File:        audio_sink_stage_deps_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for audio sink stage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "audio_sink_stage_deps.h"

#include <gtest/gtest.h>

#include <cstring>

#include "../../include/video_frame_representation_artifact_mock.h"
#include "../../stage_services_mock.h"

using testing::A;
using testing::Return;
using testing::StrictMock;

namespace orc_unit_test {

namespace {

orc::SourceParameters make_system_params(orc::VideoSystem system) {
  orc::SourceParameters params;
  params.system = system;
  return params;
}

// Descriptor for a frame-locked analogue track.
std::optional<orc::AudioTrackDescriptor> locked_track_descriptor() {
  orc::AudioTrackDescriptor desc;
  desc.name = "Analogue";
  desc.origin = orc::AudioTrackOrigin::ANALOGUE;
  desc.locked = true;
  desc.sample_rate = {44100, 1};
  return desc;
}

// Descriptor for a free-running track (e.g. decoded EFM digital audio).
std::optional<orc::AudioTrackDescriptor> free_running_track_descriptor() {
  orc::AudioTrackDescriptor desc;
  desc.name = "EFM digital audio";
  desc.origin = orc::AudioTrackOrigin::EFM;
  desc.locked = false;
  desc.sample_rate = {44100, 1};
  return desc;
}

// Reads a little-endian uint32 at the given byte offset from the int16-word
// stream the deps use to write the RIFF header.
uint32_t header_u32_at(const std::vector<int16_t>& words, size_t byte_offset) {
  uint32_t value = 0;
  std::memcpy(&value,
              reinterpret_cast<const uint8_t*>(words.data()) + byte_offset, 4);
  return value;
}

constexpr size_t kWavSampleRateOffset = 24;
constexpr size_t kWavDataSizeOffset = 40;

}  // namespace

class AudioSinkStageDeps : public ::testing::Test {
 public:
  void SetUp() override {
    pMockFileWriterInt16_ = std::make_shared<StrictMock<MockFileWriterInt16>>();

    instance_ = std::make_unique<orc::AudioSinkStageDeps>(&mockStageServices_);
    instance_->init({}, &isProcessing_, &cancelRequested_);

    cancelRequested_.store(false);
    isProcessing_.store(true);
  }

 protected:
  void expect_writer_created_and_opened() {
    EXPECT_CALL(mockStageServices_,
                create_buffered_file_writer_int16(4UL * 1024 * 1024))
        .Times(1)
        .WillOnce(Return(pMockFileWriterInt16_));
    EXPECT_CALL(*pMockFileWriterInt16_, open("out_path.wav"))
        .Times(1)
        .WillOnce(Return(true));
  }

  void capture_writes(std::vector<std::vector<int16_t>>& writes, int count) {
    EXPECT_CALL(*pMockFileWriterInt16_, write(A<const std::vector<int16_t>&>()))
        .Times(count)
        .WillRepeatedly([&writes](const std::vector<int16_t>& data) {
          writes.push_back(data);
        });
    EXPECT_CALL(*pMockFileWriterInt16_, close()).Times(1);
  }

  MockStageServices mockStageServices_;
  std::shared_ptr<StrictMock<MockFileWriterInt16>> pMockFileWriterInt16_;
  StrictMock<MockVideoFrameRepresentationArtifact> mockRepresentation_;

  std::atomic<bool> cancelRequested_{};
  std::atomic<bool> isProcessing_{};
  std::unique_ptr<orc::AudioSinkStageDeps> instance_;
};

TEST_F(AudioSinkStageDeps,
       WriteAudioWav_StreamsHeaderAndSamplesThroughInt16WriterService) {
  EXPECT_CALL(mockRepresentation_, get_audio_track_descriptor(0))
      .Times(1)
      .WillOnce(Return(locked_track_descriptor()));
  // Single-frame range with four locked stereo samples.
  EXPECT_CALL(mockRepresentation_, frame_range())
      .Times(1)
      .WillOnce(Return(orc::FrameIDRange{0, 0}));
  EXPECT_CALL(mockRepresentation_, get_audio_sample_count(0, 0))
      .Times(1)
      .WillOnce(Return(4));
  EXPECT_CALL(mockRepresentation_, get_audio_samples(0, 0))
      .Times(1)
      .WillOnce(Return(std::vector<int16_t>{1, -2, 3, -4}));
  // No video parameters available: treated as the standard 44100 Hz rate.
  EXPECT_CALL(mockRepresentation_, get_video_parameters())
      .Times(1)
      .WillOnce(Return(std::nullopt));

  expect_writer_created_and_opened();

  // One write for the 44-byte RIFF header (22 int16 words) and one for the
  // sample payload.
  std::vector<std::vector<int16_t>> writes;
  capture_writes(writes, 2);

  const auto result =
      instance_->write_audio_wav(&mockRepresentation_, "out_path.wav", 0,
                                 orc::AudioSinkSampleRateMode::kLocked);

  EXPECT_TRUE(result.success);
  // Four interleaved stereo samples = two audio frames.
  EXPECT_EQ(result.frames_written, 2U);
  ASSERT_EQ(writes.size(), 2U);
  EXPECT_EQ(header_u32_at(writes[0], kWavSampleRateOffset), 44100U);
  EXPECT_EQ(writes[1], (std::vector<int16_t>{1, -2, 3, -4}));
}

TEST_F(AudioSinkStageDeps,
       WriteAudioWav_LockedNtsc_DeclaresLockedRateAndPassesSamplesThrough) {
  EXPECT_CALL(mockRepresentation_, get_audio_track_descriptor(0))
      .Times(1)
      .WillOnce(Return(locked_track_descriptor()));
  EXPECT_CALL(mockRepresentation_, frame_range())
      .Times(1)
      .WillOnce(Return(orc::FrameIDRange{0, 0}));
  EXPECT_CALL(mockRepresentation_, get_audio_sample_count(0, 0))
      .Times(1)
      .WillOnce(Return(4));
  EXPECT_CALL(mockRepresentation_, get_audio_samples(0, 0))
      .Times(1)
      .WillOnce(Return(std::vector<int16_t>{1, -2, 3, -4}));
  EXPECT_CALL(mockRepresentation_, get_video_parameters())
      .Times(1)
      .WillOnce(Return(make_system_params(orc::VideoSystem::NTSC)));

  expect_writer_created_and_opened();

  std::vector<std::vector<int16_t>> writes;
  capture_writes(writes, 2);

  const auto result =
      instance_->write_audio_wav(&mockRepresentation_, "out_path.wav", 0,
                                 orc::AudioSinkSampleRateMode::kLocked);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.frames_written, 2U);
  ASSERT_EQ(writes.size(), 2U);
  // NTSC frame-locked rate: nearest integer to 44100000/1001 Hz.
  EXPECT_EQ(header_u32_at(writes[0], kWavSampleRateOffset), 44056U);
  // Locked mode never resamples: payload is bit-identical to the input.
  EXPECT_EQ(writes[1], (std::vector<int16_t>{1, -2, 3, -4}));
}

TEST_F(AudioSinkStageDeps,
       WriteAudioWav_FreeRunningNtsc_ResamplesTo44100AndDeclaresExactSize) {
  // Two NTSC frames of locked audio (1470 stereo pairs per frame).
  constexpr uint32_t kPairsPerFrame = 1470;
  std::vector<int16_t> frame_block(kPairsPerFrame * 2, 100);

  EXPECT_CALL(mockRepresentation_, get_audio_track_descriptor(0))
      .Times(1)
      .WillOnce(Return(locked_track_descriptor()));
  EXPECT_CALL(mockRepresentation_, frame_range())
      .Times(1)
      .WillOnce(Return(orc::FrameIDRange{0, 1}));
  EXPECT_CALL(mockRepresentation_, get_audio_sample_count(0, 0))
      .Times(1)
      .WillOnce(Return(kPairsPerFrame));
  EXPECT_CALL(mockRepresentation_, get_audio_sample_count(0, 1))
      .Times(1)
      .WillOnce(Return(kPairsPerFrame));
  EXPECT_CALL(mockRepresentation_, get_audio_samples(0, 0))
      .Times(1)
      .WillOnce(Return(frame_block));
  EXPECT_CALL(mockRepresentation_, get_audio_samples(0, 1))
      .Times(1)
      .WillOnce(Return(frame_block));
  EXPECT_CALL(mockRepresentation_, get_video_parameters())
      .Times(1)
      .WillOnce(Return(make_system_params(orc::VideoSystem::NTSC)));

  expect_writer_created_and_opened();

  std::vector<std::vector<int16_t>> writes;
  capture_writes(writes, 2);

  const auto result =
      instance_->write_audio_wav(&mockRepresentation_, "out_path.wav", 0,
                                 orc::AudioSinkSampleRateMode::kFreeRunning);

  EXPECT_TRUE(result.success);
  ASSERT_EQ(writes.size(), 2U);
  EXPECT_EQ(header_u32_at(writes[0], kWavSampleRateOffset), 44100U);

  // Resampling 44100000/1001 -> 44100 Hz stretches the stream by a factor of
  // 1.001, so more pairs come out than went in.
  const uint64_t input_pairs = 2ULL * kPairsPerFrame;
  EXPECT_GT(result.frames_written, input_pairs);
  EXPECT_EQ(result.frames_written, writes[1].size() / 2);

  // The declared data size must match the payload actually written.
  EXPECT_EQ(header_u32_at(writes[0], kWavDataSizeOffset),
            writes[1].size() * sizeof(int16_t));
}

TEST_F(AudioSinkStageDeps,
       WriteAudioWav_FreeRunningPal_IsPassthroughAtStandardRate) {
  EXPECT_CALL(mockRepresentation_, get_audio_track_descriptor(0))
      .Times(1)
      .WillOnce(Return(locked_track_descriptor()));
  EXPECT_CALL(mockRepresentation_, frame_range())
      .Times(1)
      .WillOnce(Return(orc::FrameIDRange{0, 0}));
  EXPECT_CALL(mockRepresentation_, get_audio_sample_count(0, 0))
      .Times(1)
      .WillOnce(Return(4));
  EXPECT_CALL(mockRepresentation_, get_audio_samples(0, 0))
      .Times(1)
      .WillOnce(Return(std::vector<int16_t>{1, -2, 3, -4}));
  EXPECT_CALL(mockRepresentation_, get_video_parameters())
      .Times(1)
      .WillOnce(Return(make_system_params(orc::VideoSystem::PAL)));

  expect_writer_created_and_opened();

  std::vector<std::vector<int16_t>> writes;
  capture_writes(writes, 2);

  // PAL locked audio is already 44100 Hz; free-running mode must not
  // resample.
  const auto result =
      instance_->write_audio_wav(&mockRepresentation_, "out_path.wav", 0,
                                 orc::AudioSinkSampleRateMode::kFreeRunning);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.frames_written, 2U);
  ASSERT_EQ(writes.size(), 2U);
  EXPECT_EQ(header_u32_at(writes[0], kWavSampleRateOffset), 44100U);
  EXPECT_EQ(writes[1], (std::vector<int16_t>{1, -2, 3, -4}));
}

TEST_F(AudioSinkStageDeps,
       WriteAudioWav_FreeRunningTrack_StreamsVerbatimAt44100) {
  // Free-running tracks are served via the stream accessors and written
  // verbatim; sample_rate_mode is ignored.
  EXPECT_CALL(mockRepresentation_, get_audio_track_descriptor(0))
      .Times(1)
      .WillOnce(Return(free_running_track_descriptor()));
  EXPECT_CALL(mockRepresentation_, get_audio_stream_pair_count(0))
      .Times(2)
      .WillRepeatedly(Return(3));
  EXPECT_CALL(mockRepresentation_, frame_range())
      .Times(1)
      .WillOnce(Return(orc::FrameIDRange{0, 0}));
  EXPECT_CALL(mockRepresentation_, get_audio_stream_samples(0, 0, 3))
      .Times(1)
      .WillOnce(Return(std::vector<int16_t>{10, -10, 20, -20, 30, -30}));

  expect_writer_created_and_opened();

  std::vector<std::vector<int16_t>> writes;
  capture_writes(writes, 2);

  const auto result =
      instance_->write_audio_wav(&mockRepresentation_, "out_path.wav", 0,
                                 orc::AudioSinkSampleRateMode::kFreeRunning);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.frames_written, 3U);
  ASSERT_EQ(writes.size(), 2U);
  EXPECT_EQ(header_u32_at(writes[0], kWavSampleRateOffset), 44100U);
  // Three stereo pairs = 12 bytes of payload declared in the header.
  EXPECT_EQ(header_u32_at(writes[0], kWavDataSizeOffset), 12U);
  EXPECT_EQ(writes[1], (std::vector<int16_t>{10, -10, 20, -20, 30, -30}));
}

TEST_F(AudioSinkStageDeps, WriteAudioWav_SelectedTrack_ReadsThatTrackOnly) {
  EXPECT_CALL(mockRepresentation_, get_audio_track_descriptor(1))
      .Times(1)
      .WillOnce(Return(locked_track_descriptor()));
  EXPECT_CALL(mockRepresentation_, frame_range())
      .Times(1)
      .WillOnce(Return(orc::FrameIDRange{0, 0}));
  EXPECT_CALL(mockRepresentation_, get_audio_sample_count(1, 0))
      .Times(1)
      .WillOnce(Return(2));
  EXPECT_CALL(mockRepresentation_, get_audio_samples(1, 0))
      .Times(1)
      .WillOnce(Return(std::vector<int16_t>{7, -7}));
  EXPECT_CALL(mockRepresentation_, get_video_parameters())
      .Times(1)
      .WillOnce(Return(std::nullopt));

  expect_writer_created_and_opened();

  std::vector<std::vector<int16_t>> writes;
  capture_writes(writes, 2);

  const auto result =
      instance_->write_audio_wav(&mockRepresentation_, "out_path.wav", 1,
                                 orc::AudioSinkSampleRateMode::kLocked);

  EXPECT_TRUE(result.success);
  EXPECT_EQ(result.frames_written, 1U);
  ASSERT_EQ(writes.size(), 2U);
  EXPECT_EQ(writes[1], (std::vector<int16_t>{7, -7}));
}

TEST_F(AudioSinkStageDeps, WriteAudioWav_FailsWhenTrackDoesNotExist) {
  EXPECT_CALL(mockRepresentation_, get_audio_track_descriptor(3))
      .Times(1)
      .WillOnce(Return(std::nullopt));
  EXPECT_CALL(mockRepresentation_, audio_track_count())
      .Times(1)
      .WillOnce(Return(1));

  const auto result =
      instance_->write_audio_wav(&mockRepresentation_, "out_path.wav", 3,
                                 orc::AudioSinkSampleRateMode::kLocked);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_message,
            "Audio track 3 does not exist in the input (1 track(s) available)");
}

TEST_F(AudioSinkStageDeps,
       WriteAudioWav_FailsWithDiagnostic_WhenWriterServiceUnavailable) {
  orc::AudioSinkStageDeps deps_without_services(nullptr);
  deps_without_services.init({}, &isProcessing_, &cancelRequested_);

  EXPECT_CALL(mockRepresentation_, get_audio_track_descriptor(0))
      .Times(1)
      .WillOnce(Return(locked_track_descriptor()));
  EXPECT_CALL(mockRepresentation_, frame_range())
      .Times(1)
      .WillOnce(Return(orc::FrameIDRange{0, 0}));
  EXPECT_CALL(mockRepresentation_, get_audio_sample_count(0, 0))
      .Times(1)
      .WillOnce(Return(4));

  const auto result = deps_without_services.write_audio_wav(
      &mockRepresentation_, "out_path.wav", 0,
      orc::AudioSinkSampleRateMode::kLocked);

  EXPECT_FALSE(result.success);
  EXPECT_EQ(result.error_message, "File writer service unavailable");
}

TEST_F(AudioSinkStageDeps, WriteAudioWav_Fails_WhenWriterCannotOpenFile) {
  EXPECT_CALL(mockRepresentation_, get_audio_track_descriptor(0))
      .Times(1)
      .WillOnce(Return(locked_track_descriptor()));
  EXPECT_CALL(mockRepresentation_, frame_range())
      .Times(1)
      .WillOnce(Return(orc::FrameIDRange{0, 0}));
  EXPECT_CALL(mockRepresentation_, get_audio_sample_count(0, 0))
      .Times(1)
      .WillOnce(Return(4));

  EXPECT_CALL(mockStageServices_,
              create_buffered_file_writer_int16(4UL * 1024 * 1024))
      .Times(1)
      .WillOnce(Return(pMockFileWriterInt16_));
  EXPECT_CALL(*pMockFileWriterInt16_, open("out_path.wav"))
      .Times(1)
      .WillOnce(Return(false));

  const auto result =
      instance_->write_audio_wav(&mockRepresentation_, "out_path.wav", 0,
                                 orc::AudioSinkSampleRateMode::kLocked);

  EXPECT_FALSE(result.success);
}

}  // namespace orc_unit_test
