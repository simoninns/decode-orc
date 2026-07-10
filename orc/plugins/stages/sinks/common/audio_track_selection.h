/*
 * File:        audio_track_selection.h
 * Module:      orc-core
 * Purpose:     Parses the video sink's audio_tracks parameter and derives
 *              per-track integer sample rates for embedded audio streams
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_CORE_AUDIO_TRACK_SELECTION_H
#define ORC_CORE_AUDIO_TRACK_SELECTION_H

#include <orc/stage/audio_track.h>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace orc {

// Parse an audio_tracks parameter value: "all" (every pipeline track) or a
// comma-separated list of 0-based track indices, e.g. "0,2". Whitespace
// around entries is ignored and duplicates are collapsed (first occurrence
// wins). Returns nullopt and sets error on malformed input, an index outside
// [0, track_count), or an empty selection.
inline std::optional<std::vector<size_t>> parse_audio_track_selection(
    const std::string& value, size_t track_count, std::string& error) {
  const auto trim = [](const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return std::string();
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
  };

  const std::string trimmed = trim(value);
  if (trimmed.empty() || trimmed == "all") {
    std::vector<size_t> all(track_count);
    for (size_t i = 0; i < track_count; ++i) all[i] = i;
    if (all.empty()) {
      error = "The input carries no audio tracks";
      return std::nullopt;
    }
    return all;
  }

  std::vector<size_t> selection;
  size_t pos = 0;
  while (pos <= trimmed.size()) {
    const size_t comma = trimmed.find(',', pos);
    const std::string token = trim(trimmed.substr(
        pos, (comma == std::string::npos) ? std::string::npos : comma - pos));
    pos = (comma == std::string::npos) ? trimmed.size() + 1 : comma + 1;

    if (token.empty() ||
        token.find_first_not_of("0123456789") != std::string::npos) {
      error = "Invalid audio_tracks value '" + value +
              "': expected 'all' or comma-separated track indices, e.g. "
              "'0,2'";
      return std::nullopt;
    }
    const unsigned long index = std::stoul(token);
    if (index >= track_count) {
      error = "audio_tracks index " + token +
              " is out of range: the input carries " +
              std::to_string(track_count) + " track(s)";
      return std::nullopt;
    }
    const size_t idx = static_cast<size_t>(index);
    bool duplicate = false;
    for (const size_t existing : selection) {
      if (existing == idx) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) selection.push_back(idx);
  }

  if (selection.empty()) {
    error = "audio_tracks selects no tracks";
    return std::nullopt;
  }
  return selection;
}

// Nearest-integer sample rate for a track's exact rational rate, used for
// the encoder/container declaration (containers carry integer rates):
//   locked PAL / free-running   44100/1        -> 44100
//   locked NTSC / PAL-M         44100000/1001  -> 44056 (1.3 ppm error,
//                               vs 999 ppm if 44100 were declared)
inline int audio_track_declared_rate(const AudioTrackDescriptor& descriptor) {
  if (descriptor.sample_rate.num == 0 || descriptor.sample_rate.den == 0) {
    return 44100;
  }
  const uint64_t num = descriptor.sample_rate.num;
  const uint64_t den = descriptor.sample_rate.den;
  return static_cast<int>((num + den / 2) / den);
}

}  // namespace orc

#endif  // ORC_CORE_AUDIO_TRACK_SELECTION_H
