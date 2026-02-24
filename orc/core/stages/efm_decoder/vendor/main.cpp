/*
 * File:        main.cpp
 * Module:      efm-decoder
 * Purpose:     Unified EFM to Audio/Data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include <iostream>
#include <string>

#include "logging.h"
#include "unified_cli.h"
#include "decoder_config.h"
#include "unified_decoder.h"

int main(int argc, char *argv[])
{
    // Set 'binary mode' for stdin and stdout on Windows
    setBinaryMode();

    // Parse and validate command line arguments
    UnifiedCli cli;
    auto configResult = cli.parse(argc, argv);
    
    if (!configResult.has_value()) {
        // Validation error or help requested
        if (!configResult.error().message.empty()) {
            std::cerr << "Error: " << configResult.error().message << "\n";
        }
        return configResult.error();
    }

    // Get validated configuration
    DecoderConfig config = configResult.value();

    // Initialize logging
    if (!configureLogging(config.global.logLevel, false, config.global.logFile)) {
        std::cerr << "Error: Invalid log level: " << config.global.logLevel << "\n";
        return 1;
    }

    // Run the unified decoder
    UnifiedDecoder decoder(config);
    int exitCode = decoder.run();

    return exitCode;
}
