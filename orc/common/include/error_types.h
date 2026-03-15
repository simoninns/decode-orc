/*
 * File:        error_types.h
 * Module:      orc-common
 * Purpose:     Shared exception types for error classification
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <stdexcept>
#include <string>

namespace orc {

/**
 * @brief Expected user/data error that should not be treated as an application crash.
 *
 * Use this for recoverable failures caused by invalid/missing user-provided inputs
 * (for example, missing files or incompatible source file formats).
 */
class UserDataError : public std::runtime_error {
public:
    explicit UserDataError(const std::string& message)
        : std::runtime_error(message)
    {}

    explicit UserDataError(const char* message)
        : std::runtime_error(message)
    {}
};

} // namespace orc
