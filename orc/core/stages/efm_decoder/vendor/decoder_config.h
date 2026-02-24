/*
 * File:        decoder_config.h
 * Module:      efm-decoder
 * Purpose:     Unified EFM to Audio/Data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DECODER_CONFIG_H
#define DECODER_CONFIG_H

#include <string>

// Processing mode enumeration
enum class DecoderMode {
    Audio,
    Data
};

// Global options (valid in all modes)
struct GlobalOptions {
    std::string inputPath;
    std::string outputPath;
    DecoderMode mode = DecoderMode::Audio;
    std::string logLevel = "info";
    std::string logFile;
    bool noTimecodes = false;
    bool forceTimecodes = false;  // User explicitly passed --timecodes
};

// Audio-mode specific options
struct AudioOptions {
    bool audacityLabels = false;
    bool noAudioConcealment = false;
    bool zeroPad = false;
    bool noWavHeader = false;
};

// Data-mode specific options
struct DataOptions {
    bool outputMetadata = false;
};

// Note: Frame-level debug output uses --log-level trace
// (replaces legacy --show-audio/--show-rawsector switches)

// Unified decoder configuration
struct DecoderConfig {
    GlobalOptions global;
    AudioOptions audio;
    DataOptions data;
};

#endif // DECODER_CONFIG_H
