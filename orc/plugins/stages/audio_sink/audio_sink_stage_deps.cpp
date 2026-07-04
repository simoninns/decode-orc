/*
 * File:        audio_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     AudioSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "audio_sink_stage_deps.h"

#include <orc/stage/logging.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <utility>

#include "free_running_resampler.h"

namespace orc {
namespace {

// Standard free-running audio rate.
constexpr uint32_t kStandardRateHz = 44100;

// NTSC/PAL-M frame-locked rate: 44100000/1001 Hz ≈ 44055.944 Hz
// (30000/1001 fps × 1470 stereo pairs per frame).
constexpr double kNtscLockedRate = 44100000.0 / 1001.0;

// Nearest integer rate for the WAV header (fmt chunk rates are uint32).
constexpr uint32_t kNtscLockedHeaderRateHz = 44056;

// True when the pipeline's frame-locked audio rate differs from the standard
// free-running rate; only NTSC/PAL-M (30000/1001 fps) systems are affected.
// PAL locked audio (25 fps × 1764 pairs) is already at 44100 Hz.
bool locked_rate_differs(const VideoFrameRepresentation* representation) {
  const auto params = representation->get_video_parameters();
  if (!params) return false;
  return params->system == VideoSystem::NTSC ||
         params->system == VideoSystem::PAL_M;
}

uint32_t clamp_to_uint32(uint64_t value) {
  return value > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
             ? std::numeric_limits<uint32_t>::max()
             : static_cast<uint32_t>(value);
}

}  // namespace

void AudioSinkStageDeps::init(TriggerProgressCallback progress_callback,
                              std::atomic<bool>* is_processing,
                              std::atomic<bool>* cancel_requested) {
  progress_callback_ = std::move(progress_callback);
  is_processing_ = is_processing;
  cancel_requested_ = cancel_requested;
}

std::vector<uint8_t> AudioSinkStageDeps::build_wav_header(
    uint32_t num_samples, uint32_t sample_rate, uint16_t num_channels,
    uint16_t bits_per_sample) const {
  uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
  uint16_t block_align = num_channels * (bits_per_sample / 8);
  uint32_t data_size = num_samples * num_channels * (bits_per_sample / 8);
  uint32_t file_size = 36 + data_size;

  std::vector<uint8_t> header;
  header.reserve(44);
  auto append = [&header](const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    header.insert(header.end(), bytes, bytes + size);
  };

  append("RIFF", 4);
  append(&file_size, 4);
  append("WAVE", 4);

  append("fmt ", 4);
  uint32_t fmt_size = 16;
  append(&fmt_size, 4);

  uint16_t audio_format = 1;
  append(&audio_format, 2);
  append(&num_channels, 2);
  append(&sample_rate, 4);
  append(&byte_rate, 4);
  append(&block_align, 2);
  append(&bits_per_sample, 2);

  append("data", 4);
  append(&data_size, 4);

  return header;
}

AudioSinkWriteResult AudioSinkStageDeps::write_audio_wav(
    const VideoFrameRepresentation* representation,
    const std::string& output_path, AudioSinkSampleRateMode sample_rate_mode) {
  // Free-running audio is not accessible per-frame; only locked audio is
  // available via get_audio_samples(FrameID).
  if (!representation->audio_locked()) {
    return {false, 0, "Audio is not locked to frames; cannot export"};
  }

  auto frame_rng = representation->frame_range();
  uint64_t total_samples = 0;
  for (FrameID fid = frame_rng.first; fid <= frame_rng.last; ++fid) {
    total_samples += representation->get_audio_sample_count(fid);
  }

  if (total_samples == 0) {
    return {false, 0, "No audio samples found in frame range"};
  }

  std::shared_ptr<IFileWriterInt16> writer;
  if (stage_services_) {
    writer = stage_services_->create_buffered_file_writer_int16(
        static_cast<size_t>(4 * 1024 * 1024));
  }
  if (!writer) {
    return {false, 0, "File writer service unavailable"};
  }
  if (!writer->open(output_path)) {
    return {false, 0, "Failed to open output file: " + output_path};
  }

  const bool ntsc_rate = locked_rate_differs(representation);

  if (ntsc_rate && sample_rate_mode == AudioSinkSampleRateMode::kFreeRunning) {
    return write_free_running(representation, frame_rng, writer.get());
  }

  const uint32_t header_rate =
      ntsc_rate ? kNtscLockedHeaderRateHz : kStandardRateHz;
  return write_locked(representation, frame_rng, writer.get(), header_rate,
                      total_samples);
}

AudioSinkWriteResult AudioSinkStageDeps::write_locked(
    const VideoFrameRepresentation* representation, FrameIDRange frame_rng,
    IFileWriterInt16* writer, uint32_t header_rate, uint64_t total_samples) {
  const uint64_t total_frames = frame_rng.count();

  {
    const uint16_t num_channels = 2;
    const uint16_t bits_per_sample = 16;

    // The RIFF header is 44 bytes (an even count), so it can be streamed
    // through the int16 sample writer without padding; WAV headers and
    // 16-bit PCM payloads are both little-endian byte sequences.
    const std::vector<uint8_t> header =
        build_wav_header(clamp_to_uint32(total_samples), header_rate,
                         num_channels, bits_per_sample);
    std::vector<int16_t> header_words(header.size() / 2);
    std::memcpy(header_words.data(), header.data(), header.size());
    writer->write(header_words);
  }

  uint64_t frames_written = 0;
  uint64_t current_frame = 0;
  for (FrameID fid = frame_rng.first; fid <= frame_rng.last; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      writer->close();
      if (is_processing_) {
        is_processing_->store(false);
      }
      return {false, 0, "Cancelled by user"};
    }

    auto samples = representation->get_audio_samples(fid);
    if (!samples.empty()) {
      writer->write(samples);
      frames_written += samples.size() / 2;
    }

    ++current_frame;
    if (progress_callback_ && current_frame % 10 == 0) {
      progress_callback_(current_frame, total_frames,
                         "Writing audio frame " +
                             std::to_string(current_frame) + "/" +
                             std::to_string(total_frames));
    }
  }

  writer->close();
  ORC_LOG_DEBUG("AudioSinkDeps: Wrote {} audio frames", frames_written);
  return {true, frames_written, ""};
}

AudioSinkWriteResult AudioSinkStageDeps::write_free_running(
    const VideoFrameRepresentation* representation, FrameIDRange frame_rng,
    IFileWriterInt16* writer) {
  const uint64_t total_frames = frame_rng.count();

  // Collect the frame-locked stream so a single SoXR pass produces the exact
  // output length the RIFF header must declare up front (the buffered writer
  // is stream-only, so the header cannot be patched afterwards).
  std::vector<int16_t> locked_stream;
  uint64_t current_frame = 0;
  for (FrameID fid = frame_rng.first; fid <= frame_rng.last; ++fid) {
    if (cancel_requested_ && cancel_requested_->load()) {
      writer->close();
      if (is_processing_) {
        is_processing_->store(false);
      }
      return {false, 0, "Cancelled by user"};
    }

    auto samples = representation->get_audio_samples(fid);
    locked_stream.insert(locked_stream.end(), samples.begin(), samples.end());

    ++current_frame;
    if (progress_callback_ && current_frame % 10 == 0) {
      progress_callback_(current_frame, total_frames,
                         "Reading audio frame " +
                             std::to_string(current_frame) + "/" +
                             std::to_string(total_frames));
    }
  }

  if (progress_callback_) {
    progress_callback_(total_frames, total_frames,
                       "Resampling audio to 44100 Hz");
  }

  const std::vector<int16_t> resampled = FreeRunningAudioResampler::resample(
      locked_stream, kNtscLockedRate, static_cast<double>(kStandardRateHz));
  if (resampled.empty()) {
    writer->close();
    return {false, 0, "Audio resampling to 44100 Hz failed"};
  }

  const uint64_t resampled_pairs = resampled.size() / 2;

  {
    const uint16_t num_channels = 2;
    const uint16_t bits_per_sample = 16;
    const std::vector<uint8_t> header =
        build_wav_header(clamp_to_uint32(resampled_pairs), kStandardRateHz,
                         num_channels, bits_per_sample);
    std::vector<int16_t> header_words(header.size() / 2);
    std::memcpy(header_words.data(), header.data(), header.size());
    writer->write(header_words);
  }

  writer->write(resampled);
  writer->close();
  ORC_LOG_DEBUG("AudioSinkDeps: Wrote {} resampled stereo pairs at 44100 Hz",
                resampled_pairs);
  return {true, resampled_pairs, ""};
}
}  // namespace orc
