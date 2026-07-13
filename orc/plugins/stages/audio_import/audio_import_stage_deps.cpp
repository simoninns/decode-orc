/*
 * File:        audio_import_stage_deps.cpp
 * Module:      orc-stage-plugin-audio_import
 * Purpose:     Production dependencies for AudioImportStage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "audio_import_stage_deps.h"

#include <array>
#include <cstring>
#include <fstream>

namespace orc {

namespace {

uint16_t read_u16_le(const uint8_t* p) {
  return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

uint32_t read_u32_le(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

// 3-byte little-endian two's-complement sample, sign-extended into the
// 24-bit-in-int32 carrier (−8388608 … 8388607).
int32_t read_s24_le(const uint8_t* p) {
  uint32_t u = static_cast<uint32_t>(p[0]) |
               (static_cast<uint32_t>(p[1]) << 8) |
               (static_cast<uint32_t>(p[2]) << 16);
  if (u & 0x00800000u) u |= 0xFF000000u;
  return static_cast<int32_t>(u);
}

}  // namespace

WavProbeResult AudioImportDeps::open(const std::string& wav_path) {
  WavProbeResult result;

  std::ifstream file(wav_path, std::ios::binary);
  if (!file) {
    result.error = "cannot open file";
    return result;
  }

  // RIFF header: "RIFF" <riff size> "WAVE".
  std::array<uint8_t, 12> riff{};
  file.read(reinterpret_cast<char*>(riff.data()),
            static_cast<std::streamsize>(riff.size()));
  if (file.gcount() != static_cast<std::streamsize>(riff.size()) ||
      std::memcmp(riff.data(), "RIFF", 4) != 0 ||
      std::memcmp(riff.data() + 8, "WAVE", 4) != 0) {
    result.error = "not a RIFF/WAVE file";
    return result;
  }

  // Walk the chunk list for "fmt " and "data".
  bool have_fmt = false;
  bool have_data = false;
  uint16_t audio_format = 0;
  uint16_t channels = 0;
  uint16_t bits_per_sample = 0;
  uint32_t sample_rate = 0;
  uint64_t data_offset = 0;
  uint64_t data_size = 0;

  while (!(have_fmt && have_data)) {
    std::array<uint8_t, 8> chunk_header{};
    file.read(reinterpret_cast<char*>(chunk_header.data()),
              static_cast<std::streamsize>(chunk_header.size()));
    if (file.gcount() != static_cast<std::streamsize>(chunk_header.size())) {
      break;  // end of file
    }
    const uint32_t chunk_size = read_u32_le(chunk_header.data() + 4);
    const auto chunk_payload = static_cast<uint64_t>(file.tellg());

    if (std::memcmp(chunk_header.data(), "fmt ", 4) == 0) {
      std::array<uint8_t, 16> fmt{};
      if (chunk_size < fmt.size()) {
        result.error = "malformed fmt chunk";
        return result;
      }
      file.read(reinterpret_cast<char*>(fmt.data()),
                static_cast<std::streamsize>(fmt.size()));
      if (file.gcount() != static_cast<std::streamsize>(fmt.size())) {
        result.error = "malformed fmt chunk";
        return result;
      }
      audio_format = read_u16_le(fmt.data());
      channels = read_u16_le(fmt.data() + 2);
      sample_rate = read_u32_le(fmt.data() + 4);
      bits_per_sample = read_u16_le(fmt.data() + 14);
      have_fmt = true;
    } else if (std::memcmp(chunk_header.data(), "data", 4) == 0) {
      data_offset = chunk_payload;
      data_size = chunk_size;
      have_data = true;
    }

    // Chunks are word-aligned: odd sizes carry one pad byte.
    file.seekg(static_cast<std::streamoff>(chunk_payload + chunk_size +
                                           (chunk_size & 1u)),
               std::ios::beg);
    if (!file) break;
  }

  if (!have_fmt || !have_data) {
    result.error = "missing fmt or data chunk";
    return result;
  }
  if (audio_format != 1) {  // WAVE_FORMAT_PCM
    result.error = "not PCM encoded";
    return result;
  }
  if (channels != 2) {
    result.error = "not stereo (" + std::to_string(channels) + " channel(s))";
    return result;
  }
  if (bits_per_sample != 16 && bits_per_sample != 24) {
    result.error = "not 16- or 24-bit (" + std::to_string(bits_per_sample) +
                   " bits/sample)";
    return result;
  }
  if (sample_rate == 0) {
    result.error = "invalid sample rate (0 Hz)";
    return result;
  }

  // Bytes per stereo pair: 16-bit = 2 × 2 bytes; 24-bit = 2 × 3 bytes.
  const uint64_t bytes_per_pair = (bits_per_sample == 16) ? 4 : 6;

  wav_path_ = wav_path;
  bits_per_sample_ = bits_per_sample;
  data_offset_ = data_offset;
  data_pairs_ = data_size / bytes_per_pair;

  result.valid = true;
  result.sample_rate = sample_rate;
  result.bits_per_sample = bits_per_sample;
  result.pair_count = data_pairs_;
  return result;
}

std::vector<int16_t> AudioImportDeps::read_all_pairs_16() const {
  if (wav_path_.empty() || bits_per_sample_ != 16 || data_pairs_ == 0) {
    return {};
  }

  std::ifstream file(wav_path_, std::ios::binary);
  if (!file) return {};
  file.seekg(static_cast<std::streamoff>(data_offset_), std::ios::beg);
  std::vector<int16_t> samples(static_cast<size_t>(data_pairs_) * 2);
  file.read(reinterpret_cast<char*>(samples.data()),
            static_cast<std::streamsize>(samples.size() * sizeof(int16_t)));
  const auto words_read = static_cast<size_t>(file.gcount()) / sizeof(int16_t);
  samples.resize((words_read / 2) * 2);  // whole pairs only
  return samples;
}

std::vector<int32_t> AudioImportDeps::read_all_pairs_24() const {
  if (wav_path_.empty() || bits_per_sample_ != 24 || data_pairs_ == 0) {
    return {};
  }

  std::ifstream file(wav_path_, std::ios::binary);
  if (!file) return {};
  file.seekg(static_cast<std::streamoff>(data_offset_), std::ios::beg);
  std::vector<uint8_t> bytes(static_cast<size_t>(data_pairs_) * 6);
  file.read(reinterpret_cast<char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  const auto pairs_read = static_cast<size_t>(file.gcount()) / 6;

  std::vector<int32_t> samples(pairs_read * 2);
  for (size_t i = 0; i < samples.size(); ++i) {
    samples[i] = read_s24_le(bytes.data() + i * 3);
  }
  return samples;
}

}  // namespace orc
