/*
 * File:        audio_pair_selection.h
 * Module:      orc-core
 * Purpose:     Parses the video sink's audio_channel_pairs parameter selecting
 *              the audio channel pairs to embed
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_CORE_AUDIO_PAIR_SELECTION_H
#define ORC_CORE_AUDIO_PAIR_SELECTION_H

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace orc {

// Parse an audio_channel_pairs parameter value: "all" (every pipeline audio
// channel pair) or a comma-separated list of 0-based channel pair indices,
// e.g. "0,2". Whitespace around entries is ignored and duplicates are
// collapsed (first occurrence wins). Returns nullopt and sets error on
// malformed input, an index outside [0, pair_count), or an empty selection.
inline std::optional<std::vector<size_t>> parse_audio_pair_selection(
    const std::string& value, size_t pair_count, std::string& error) {
  const auto trim = [](const std::string& s) {
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return std::string();
    const auto last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
  };

  const std::string trimmed = trim(value);
  if (trimmed.empty() || trimmed == "all") {
    std::vector<size_t> all(pair_count);
    for (size_t i = 0; i < pair_count; ++i) all[i] = i;
    if (all.empty()) {
      error = "The input carries no audio channel pairs";
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
      error = "Invalid audio_channel_pairs value '" + value +
              "': expected 'all' or comma-separated channel pair indices, "
              "e.g. '0,2'";
      return std::nullopt;
    }
    const unsigned long index = std::stoul(token);
    if (index >= pair_count) {
      error = "audio_channel_pairs index " + token +
              " is out of range: the input carries " +
              std::to_string(pair_count) + " channel pair(s)";
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
    error = "audio_channel_pairs selects no channel pairs";
    return std::nullopt;
  }
  return selection;
}

}  // namespace orc

#endif  // ORC_CORE_AUDIO_PAIR_SELECTION_H
