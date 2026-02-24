/*
 * File:        unified_cli.cpp
 * Module:      efm-decoder
 * Purpose:     Unified EFM to Audio/Data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "unified_cli.h"
#include <algorithm>
#include <cctype>
#include <iostream>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

std::string toLowerCopy(const std::string& value)
{
    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return lowered;
}

bool hasExtensionCaseInsensitive(const std::string& path, const std::string& extension)
{
    const std::string loweredPath = toLowerCopy(path);
    const std::string loweredExtension = toLowerCopy(extension);

    if (loweredPath.length() < loweredExtension.length()) {
        return false;
    }

    return loweredPath.compare(loweredPath.length() - loweredExtension.length(),
                               loweredExtension.length(), loweredExtension) == 0;
}

} // namespace

ConfigResult UnifiedCli::parse(int argc, char* argv[])
{
    if (argc <= 1) {
        return CliError{1, "Missing arguments. Expected: [Global options] [--mode <audio|data>] [Mode options] <input.efm> <output>"};
    }

    DecoderConfig config;

    const std::unordered_map<std::string, bool> supportedOptions = {
        {"mode", true},
        {"log-level", true},
        {"log-file", true},
        {"no-timecodes", false},
        {"timecodes", false},
        {"audacity-labels", false},
        {"no-audio-concealment", false},
        {"zero-pad", false},
        {"no-wav-header", false},
        {"output-metadata", false},
    };

    const std::unordered_set<std::string> audioOnlyOptions = {
        "audacity-labels",
        "no-audio-concealment",
        "zero-pad",
        "no-wav-header",
    };

    const std::unordered_set<std::string> dataOnlyOptions = {
        "output-metadata",
    };

    bool modeProvided = false;
    bool helpRequested = false;
    std::unordered_set<std::string> seenOptions;
    std::vector<std::string> positionalArguments;

    for (int i = 1; i < argc; ++i) {
        std::string argument = argv[i];

        if (argument == "-h" || argument == "--help") {
            helpRequested = true;
            continue;
        }

        if (argument.rfind("--", 0) == 0) {
            std::string optionName = argument.substr(2);
            std::string optionValue;

            const size_t equalPosition = optionName.find('=');
            if (equalPosition != std::string::npos) {
                optionValue = optionName.substr(equalPosition + 1);
                optionName = optionName.substr(0, equalPosition);
            }

            const auto optionIt = supportedOptions.find(optionName);
            if (optionIt == supportedOptions.end()) {
                if (optionName == "show-audio" || optionName == "show-rawsector") {
                    return CliError{1, "Legacy debug option --" + optionName + " is not supported. Use --log-level trace instead."};
                }
                return CliError{1, "Unknown option: --" + optionName};
            }

            const bool expectsValue = optionIt->second;
            if (expectsValue) {
                if (optionValue.empty()) {
                    if (i + 1 >= argc) {
                        return CliError{1, "Missing value for option: --" + optionName};
                    }

                    const std::string nextArgument = argv[i + 1];
                    if (nextArgument.rfind("--", 0) == 0 || nextArgument == "-h" || nextArgument == "--help") {
                        return CliError{1, "Missing value for option: --" + optionName};
                    }

                    optionValue = nextArgument;
                    ++i;
                }
            } else if (!optionValue.empty()) {
                return CliError{1, "Option --" + optionName + " does not accept a value"};
            }

            if (optionName == "mode") {
                if (modeProvided) {
                    return CliError{1, "Duplicate --mode option. Exactly one mode must be selected."};
                }

                const std::string loweredMode = toLowerCopy(optionValue);
                if (loweredMode == "audio") {
                    config.global.mode = DecoderMode::Audio;
                } else if (loweredMode == "data") {
                    config.global.mode = DecoderMode::Data;
                } else {
                    return CliError{1, "Invalid --mode value: " + optionValue + ". Expected: audio or data."};
                }

                modeProvided = true;
                seenOptions.insert(optionName);
                continue;
            }

            if (optionName == "log-level") {
                config.global.logLevel = optionValue;
            } else if (optionName == "log-file") {
                config.global.logFile = optionValue;
            } else if (optionName == "no-timecodes") {
                config.global.noTimecodes = true;
            } else if (optionName == "timecodes") {
                config.global.forceTimecodes = true;
            } else if (optionName == "audacity-labels") {
                config.audio.audacityLabels = true;
            } else if (optionName == "no-audio-concealment") {
                config.audio.noAudioConcealment = true;
            } else if (optionName == "zero-pad") {
                config.audio.zeroPad = true;
            } else if (optionName == "no-wav-header") {
                config.audio.noWavHeader = true;
            } else if (optionName == "output-metadata") {
                config.data.outputMetadata = true;
            }

            seenOptions.insert(optionName);
            continue;
        }

        if (argument != "-" && !argument.empty() && argument[0] == '-') {
            return CliError{1, "Unknown option: " + argument};
        }

        positionalArguments.push_back(argument);
    }

    if (helpRequested) {
        showHelp();
        return CliError{0, ""};
    }

    if (positionalArguments.size() < 2) {
        return CliError{1, "Not enough arguments. Expected: <input.efm> <output>"};
    }
    if (positionalArguments.size() > 2) {
        return CliError{1, "Too many arguments. Expected: <input.efm> <output>"};
    }

    config.global.inputPath = positionalArguments[0];
    config.global.outputPath = positionalArguments[1];

    if (config.global.inputPath == "-" || config.global.outputPath == "-") {
        return CliError{1, "stdin/stdout streaming is not supported. Provide file paths for both input and output."};
    }

    const std::string loweredInputPath = toLowerCopy(config.global.inputPath);
    if (loweredInputPath == "/dev/stdin") {
        return CliError{1, "stdin streaming is not supported. Provide an input file path."};
    }
    if (toLowerCopy(config.global.outputPath) == "/dev/stdout") {
        return CliError{1, "stdout streaming is not supported. Provide an output file path."};
    }

    if (hasExtensionCaseInsensitive(config.global.inputPath, ".f2") ||
        hasExtensionCaseInsensitive(config.global.inputPath, ".d24")) {
        return CliError{1, "Invalid input format: unified decoder accepts EFM input only (direct .f2/.d24 input is not supported)."};
    }

    if (!hasExtensionCaseInsensitive(config.global.inputPath, ".efm")) {
        return CliError{1, "Invalid input format: expected an .efm input file."};
    }

    if (config.global.mode == DecoderMode::Audio) {
        for (const std::string& optionName : dataOnlyOptions) {
            if (seenOptions.find(optionName) != seenOptions.end()) {
                return CliError{1, "Option --" + optionName + " is only valid with --mode data."};
            }
        }
    }

    if (config.global.mode == DecoderMode::Data) {
        for (const std::string& optionName : audioOnlyOptions) {
            if (seenOptions.find(optionName) != seenOptions.end()) {
                return CliError{1, "Option --" + optionName + " is only valid with --mode audio."};
            }
        }
    }

    return config;
}

void UnifiedCli::showHelp()
{
    std::cout << "efm-decoder - Unified EFM to Audio/Data decoder\n";
    std::cout << "(c) 2025-2026 Simon Inns\n";
    std::cout << "GPLv3 Open-Source - github: https://github.com/happycube/ld-decode\n\n";
    
    std::cout << "Usage:\n";
    std::cout << "  efm-decoder [Global options] [--mode <audio|data>] [Mode options] <input.efm> <output>\n\n";
    
    std::cout << "Arguments:\n";
    std::cout << "  input                     Input EFM file\n";
    std::cout << "  output                    Output file (mode-dependent)\n\n";
    
    std::cout << "Global options:\n";
    std::cout << "  -h, --help                Show this help message and exit\n";
    std::cout << "  --mode <audio|data>       Output mode selector (default: audio)\n";
    std::cout << "  --log-level <level>       Console log level: trace, debug, info, warn, error, critical, off\n";
    std::cout << "  --log-file <path>         Write full debug logging to file\n";
    std::cout << "  --no-timecodes            Force no-timecodes mode (disables auto-detection)\n";
    std::cout << "  --timecodes               Force timecode mode (disables auto-detection)\n\n";
    
    std::cout << "Audio mode options (valid with --mode audio):\n";
    std::cout << "  --audacity-labels         Output WAV metadata as Audacity labels\n";
    std::cout << "  --no-audio-concealment    Disable audio concealment\n";
    std::cout << "  --zero-pad                Zero pad audio from 00:00:00\n";
    std::cout << "  --no-wav-header           Output raw PCM audio without WAV header\n\n";
    
    std::cout << "Data mode options (valid with --mode data):\n";
    std::cout << "  --output-metadata         Output bad sector map metadata\n\n";
    
    std::cout << "Notes:\n";
    std::cout << "  - Default mode is audio when --mode is not provided.\n";
    std::cout << "  - Use --mode data to enable data decoding options.\n";
    std::cout << "  - In audio mode with --no-wav-header, output is raw PCM (use .pcm convention).\n";
    std::cout << "  - For frame-level debug use --log-level trace\n";
}
