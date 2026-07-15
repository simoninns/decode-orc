/*
 * File:        efm_audio_decode_stage_deps.cpp
 * Module:      orc-stage-plugin-efm_audio_decode
 * Purpose:     Default EFMAudioDecodeStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "efm_audio_decode_stage_deps.h"

#include <orc/stage/logging.h>

#include <chrono>
#include <fstream>
#include <system_error>

#include "efm-decode/efm-lib/efm_exception.h"
#include "efm-decode/efm_processor.h"

namespace orc {

namespace {

constexpr uint64_t kBytesPerStereoPair = 4;  // 2 channels x int16_t

// 2 channels x int32_t in the converted synchronous cache.
constexpr uint64_t kBytesPerSynchronousStereoPair = 8;

// Unique scratch path for a decoded-audio cache file. Uniqueness comes from
// the steady-clock tick count plus the owning object's address, which is
// enough to keep concurrent pipelines in the same process apart.
std::filesystem::path make_cache_path(const void* owner,
                                      const std::string& extension) {
  const auto ticks =
      std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("orc-efm-audio-" + std::to_string(ticks) + "-" +
          std::to_string(reinterpret_cast<uintptr_t>(owner)) + extension);
}

}  // namespace

EFMAudioDecodeDeps::~EFMAudioDecodeDeps() {
  std::error_code ec;
  if (!cache_path_.empty()) {
    std::filesystem::remove(cache_path_, ec);
  }
  if (!sync_cache_path_.empty()) {
    std::filesystem::remove(sync_cache_path_, ec);
  }
}

EFMAudioDecodeResult EFMAudioDecodeDeps::decode_to_cache(
    const VideoFrameRepresentation& representation,
    const EFMAudioDecodeOptions& options) {
  const auto frame_rng = representation.frame_range();
  const FrameID start_fid = frame_rng.first;
  const FrameID end_fid = frame_rng.last;

  ORC_LOG_DEBUG("EFMAudioDecodeDeps: counting EFM t-values across {} frames",
                frame_rng.count());

  uint64_t total_tvalues = 0;
  for (FrameID fid = start_fid; fid <= end_fid; ++fid) {
    total_tvalues += representation.get_efm_sample_count(fid);
  }
  if (total_tvalues == 0) {
    return {false, "no EFM t-values found in frame range", 0};
  }

  std::vector<uint8_t> efm_buffer;
  efm_buffer.reserve(total_tvalues);
  for (FrameID fid = start_fid; fid <= end_fid; ++fid) {
    auto samples = representation.get_efm_samples(fid);
    efm_buffer.insert(efm_buffer.end(), samples.begin(), samples.end());
  }
  ORC_LOG_DEBUG("EFMAudioDecodeDeps: buffered {} t-values", efm_buffer.size());

  const std::filesystem::path cache_path = make_cache_path(this, ".pcm");

  // Audio-mode decode to headerless PCM: the cache holds exactly the decoded
  // 44.1 kHz interleaved stereo int16_t stream, so pair offsets map directly
  // to byte offsets.
  EfmProcessor processor;
  processor.setAudioMode(true);
  processor.setNoWavHeader(true);
  processor.setNoTimecodes(options.no_timecodes);
  processor.setNoAudioConcealment(options.no_audio_concealment);

  // The decode targets a scratch cache, so point the report at the
  // user-chosen path rather than letting it default to "<cache>.txt".
  if (!options.report_path.empty()) {
    processor.setReportOutput(true);
    processor.setReportFilename(options.report_path);
  }

  // The EFM decoder throws efm::EfmDecodeError on unrecoverable conditions;
  // catch it so a bad decode fails this stage rather than killing the process.
  try {
    if (!processor.processFromBuffer(efm_buffer, cache_path.string())) {
      std::error_code ec;
      std::filesystem::remove(cache_path, ec);
      std::string reason = processor.lastError();
      if (reason.empty()) {
        reason = "EFM decoding did not complete successfully";
      }
      return {false, reason, 0};
    }
  } catch (const efm::EfmDecodeError& e) {
    std::error_code ec;
    std::filesystem::remove(cache_path, ec);
    ORC_LOG_ERROR("EFMAudioDecodeDeps: {}", e.what());
    return {false, e.what(), 0};
  } catch (const std::exception& e) {
    // Backstop: any other escape from the decoder must fail this stage (and
    // clean up the partial cache file) rather than kill the host process.
    std::error_code ec;
    std::filesystem::remove(cache_path, ec);
    ORC_LOG_ERROR("EFMAudioDecodeDeps: unexpected exception: {}", e.what());
    return {false, e.what(), 0};
  }

  std::error_code ec;
  const uint64_t cache_bytes = std::filesystem::file_size(cache_path, ec);
  if (ec) {
    std::filesystem::remove(cache_path, ec);
    return {false, "failed to stat decoded audio cache file", 0};
  }

  cache_path_ = cache_path;
  return {true, "", cache_bytes / kBytesPerStereoPair};
}

std::vector<int16_t> EFMAudioDecodeDeps::read_cache_pairs(
    uint64_t first_pair, uint32_t pair_count) const {
  if (cache_path_.empty() || pair_count == 0) return {};

  std::ifstream file(cache_path_, std::ios::binary);
  if (!file) {
    ORC_LOG_ERROR("EFMAudioDecodeDeps: cannot open audio cache file {}",
                  cache_path_.string());
    return {};
  }

  file.seekg(static_cast<std::streamoff>(first_pair * kBytesPerStereoPair));
  std::vector<int16_t> samples(static_cast<size_t>(pair_count) * 2);
  file.read(reinterpret_cast<char*>(samples.data()),
            static_cast<std::streamsize>(samples.size() * sizeof(int16_t)));

  // Truncate a short read to whole stereo pairs.
  const uint64_t pairs_read =
      static_cast<uint64_t>(file.gcount()) / kBytesPerStereoPair;
  samples.resize(static_cast<size_t>(pairs_read) * 2);
  return samples;
}

bool EFMAudioDecodeDeps::write_synchronous_cache(
    const std::vector<int32_t>& samples) {
  const std::filesystem::path sync_path = make_cache_path(this, ".s32");

  {
    std::ofstream file(sync_path, std::ios::binary | std::ios::trunc);
    if (!file) {
      ORC_LOG_ERROR(
          "EFMAudioDecodeDeps: cannot create synchronous audio cache file {}",
          sync_path.string());
      return false;
    }
    file.write(reinterpret_cast<const char*>(samples.data()),
               static_cast<std::streamsize>(samples.size() * sizeof(int32_t)));
    if (!file) {
      ORC_LOG_ERROR(
          "EFMAudioDecodeDeps: failed writing synchronous audio cache file {}",
          sync_path.string());
      std::error_code ec;
      std::filesystem::remove(sync_path, ec);
      return false;
    }
  }

  sync_cache_path_ = sync_path;

  // The raw 44.1 kHz cache is no longer needed once the converted stream is
  // on disk; drop it to halve the scratch footprint.
  if (!cache_path_.empty()) {
    std::error_code ec;
    std::filesystem::remove(cache_path_, ec);
    cache_path_.clear();
  }
  return true;
}

std::vector<int32_t> EFMAudioDecodeDeps::read_synchronous_pairs(
    uint64_t first_pair, uint32_t pair_count) const {
  if (sync_cache_path_.empty() || pair_count == 0) return {};

  std::ifstream file(sync_cache_path_, std::ios::binary);
  if (!file) {
    ORC_LOG_ERROR(
        "EFMAudioDecodeDeps: cannot open synchronous audio cache file {}",
        sync_cache_path_.string());
    return {};
  }

  file.seekg(
      static_cast<std::streamoff>(first_pair * kBytesPerSynchronousStereoPair));
  std::vector<int32_t> samples(static_cast<size_t>(pair_count) * 2);
  file.read(reinterpret_cast<char*>(samples.data()),
            static_cast<std::streamsize>(samples.size() * sizeof(int32_t)));

  // Truncate a short read to whole stereo pairs.
  const uint64_t pairs_read =
      static_cast<uint64_t>(file.gcount()) / kBytesPerSynchronousStereoPair;
  samples.resize(static_cast<size_t>(pairs_read) * 2);
  return samples;
}

}  // namespace orc
