/*
 * File:        unified_cli.h
 * Module:      efm-decoder
 * Purpose:     Unified EFM to Audio/Data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef UNIFIED_CLI_H
#define UNIFIED_CLI_H

#include <optional>
#include <string>
#include "decoder_config.h"

// Error code type for CLI validation
struct CliError {
    int exitCode;
    std::string message;
    
    operator int() const { return exitCode; }
};

// Result type: either valid config or error
template<typename T, typename E>
class Result {
private:
    std::optional<T> value_;
    std::optional<E> error_;
    
public:
    Result(T val) : value_(val) {}
    Result(E err) : error_(err) {}
    
    bool has_value() const { return value_.has_value(); }
    T value() const { return *value_; }
    E error() const { return *error_; }
};

using ConfigResult = Result<DecoderConfig, CliError>;

class UnifiedCli {
public:
    UnifiedCli() = default;
    
    // Parse and validate command line arguments
    // Returns either a valid DecoderConfig or a CliError
    ConfigResult parse(int argc, char* argv[]);
    
private:
    void showHelp();
};

#endif // UNIFIED_CLI_H
