/*
 * File:        orc_cvbs_source_parameters.h
 * Module:      orc-view-types
 * Purpose:     CVBS source metadata and parameter types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <common_types.h>  // For VideoSystem enum

#include <cstdint>
#include <string>

namespace orc {

/**
 * @brief Sample encoding preset for CVBS sources
 *
 * Defines how composite sample values are encoded in CVBS files.
 */
enum class CVBSEncodingPreset {
  UNKNOWN = 0,
  // 16-bit unsigned direct encoding: all samples in 0-65535 range
  CVBS_U16_4FSC = 1,
  // Device-encoded composite: requires offset/scale reversal (typically 0-255
  // mapped to wide range)
  CVBS_TPG21_4FSC = 2
};

/**
 * @brief Convert CVBS encoding preset to string representation
 */
inline std::string cvbs_encoding_to_string(CVBSEncodingPreset preset) {
  switch (preset) {
    case CVBSEncodingPreset::CVBS_U16_4FSC:
      return "CVBS_U16_4FSC";
    case CVBSEncodingPreset::CVBS_TPG21_4FSC:
      return "CVBS_TPG21_4FSC";
    default:
      return "UNKNOWN";
  }
}

/**
 * @brief Convert string to CVBS encoding preset
 */
inline CVBSEncodingPreset string_to_cvbs_encoding(const std::string& str) {
  if (str == "CVBS_U16_4FSC") return CVBSEncodingPreset::CVBS_U16_4FSC;
  if (str == "CVBS_TPG21_4FSC") return CVBSEncodingPreset::CVBS_TPG21_4FSC;
  return CVBSEncodingPreset::UNKNOWN;
}

/**
 * @brief Signal state preset constraint for CVBS sources
 *
 * CVBS source stage only accepts STANDARD_TBC_LOCKED signal state.
 * This constraint is enforced at load time and documented here for reference.
 */
enum class CVBSSignalState {
  STANDARD_TBC_LOCKED = 1  // ONLY allowed state for CVBS sources
};

/**
 * @brief CVBS source metadata from .meta sidecar file
 *
 * Used in metadata-driven mode to determine video standard,
 * sample encoding, and validate signal state constraints.
 */
struct CVBSSourceMetadata {
  // From metadata file core CVBS fields (no extension metadata used)
  VideoSystem video_standard = VideoSystem::Unknown;
  CVBSEncodingPreset sample_encoding = CVBSEncodingPreset::UNKNOWN;
  std::string signal_state_preset;  // Must be "STANDARD_TBC_LOCKED"
  std::string signal_type;          // Must be "composite"

  // Validation
  bool is_valid_for_cvbs_source() const {
    // Check video standard
    if (video_standard != VideoSystem::PAL &&
        video_standard != VideoSystem::NTSC) {
      return false;
    }

    // Check encoding
    if (sample_encoding == CVBSEncodingPreset::UNKNOWN) {
      return false;
    }

    // Check signal state (CVBS stage constraint)
    if (signal_state_preset != "STANDARD_TBC_LOCKED") {
      return false;
    }

    // Check signal type
    if (signal_type != "composite") {
      return false;
    }

    return true;
  }
};

/**
 * @brief CVBS source parameters (view-layer DTO)
 *
 * Used by presenters to expose CVBS source stage parameters to GUI/CLI.
 * Maps between external parameter names and internal types.
 */
struct CVBSSourceParameters {
  // User-facing parameters
  std::string input_path;              // Path to CVBS data file
  bool use_metadata = false;           // Enable metadata-driven mode
  std::string video_standard = "PAL";  // Manual mode: "PAL" or "NTSC"
  std::string sample_encoding =
      "CVBS_U16_4FSC";  // Manual mode: encoding preset

  // Resolved parameters (filled during execution)
  VideoSystem resolved_system = VideoSystem::Unknown;
  CVBSEncodingPreset resolved_encoding = CVBSEncodingPreset::UNKNOWN;

  // Metadata (populated in metadata mode)
  CVBSSourceMetadata metadata;
};

}  // namespace orc
