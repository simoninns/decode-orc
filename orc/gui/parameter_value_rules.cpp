/*
 * File:        parameter_value_rules.cpp
 * Module:      orc-gui
 * Purpose:     Pure helper that reports parameter values whose relation to
 *              each other is wrong
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "parameter_value_rules.h"

#include <optional>

namespace orc::gui {

namespace {

// One parameter that must stay below another, and what to say when it does
// not. The names are the stage's, not the labels': see the test that holds
// them to the descriptors the stages publish.
struct OrderedPair {
  const char* lower;
  const char* upper;
  const char* message;
};

// video_params: the active window's ends bound a picture, and the signal
// levels bound a contrast range. Both pairs come from the same stage, whose
// set_parameters() refuses the same combinations.
constexpr OrderedPair kOrderedPairs[] = {
    {"activeVideoStart", "activeVideoEnd",
     "Active Video Start must be less than Active Video End."},
    {"firstActiveFrameLine", "lastActiveFrameLine",
     "First Active Frame Line must be less than Last Active Frame Line."},
    {"blackLevel", "whiteLevel", "Black Level must be less than White Level."},
};

// The value that leaves a video parameter alone, taking the source's instead.
constexpr int32_t kInheritFromSource = -1;

// The whole number a parameter carries, whichever integer type holds it;
// nothing when the parameter is absent or is not a whole number at all.
std::optional<int32_t> whole_number(
    const std::map<std::string, orc::ParameterValue>& values,
    const std::string& name) {
  const auto it = values.find(name);
  if (it == values.end()) {
    return std::nullopt;
  }
  if (const auto* signed_value = std::get_if<int32_t>(&it->second)) {
    return *signed_value;
  }
  if (const auto* unsigned_value = std::get_if<uint32_t>(&it->second)) {
    return static_cast<int32_t>(*unsigned_value);
  }
  return std::nullopt;
}

}  // namespace

std::vector<std::string> crossParameterErrors(
    const std::map<std::string, orc::ParameterValue>& values) {
  std::vector<std::string> errors;

  for (const auto& pair : kOrderedPairs) {
    const auto lower = whole_number(values, pair.lower);
    const auto upper = whole_number(values, pair.upper);
    if (!lower.has_value() || !upper.has_value()) {
      continue;  // This stage does not have the pair.
    }
    if (*lower == kInheritFromSource || *upper == kInheritFromSource) {
      continue;  // One end is the source's; the relation is its business.
    }
    if (*lower >= *upper) {
      errors.emplace_back(pair.message);
    }
  }

  return errors;
}

}  // namespace orc::gui
