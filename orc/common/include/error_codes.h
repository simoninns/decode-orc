/*
 * File:        error_codes.h
 * Module:      orc-common
 * Purpose:     Common error codes and status enums
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <cstdint>

namespace orc {

/**
 * @brief Common result codes for API operations
 */
enum class ResultCode : int32_t {
    SUCCESS = 0,
    ERROR_INVALID_ARGUMENT = -1,
    ERROR_FILE_NOT_FOUND = -2,
    ERROR_IO_ERROR = -3,
    ERROR_INVALID_FORMAT = -4,
    ERROR_OUT_OF_MEMORY = -5,
    ERROR_NOT_IMPLEMENTED = -6,
    ERROR_INTERNAL = -7,
    ERROR_INVALID_STATE = -8,
    ERROR_TIMEOUT = -9,
    ERROR_CANCELLED = -10,
    ERROR_UNKNOWN = -99
};

/**
 * @brief Check if a result code indicates success
 */
inline bool is_success(ResultCode code) {
    return code == ResultCode::SUCCESS;
}

/**
 * @brief Check if a result code indicates an error
 */
inline bool is_error(ResultCode code) {
    return code != ResultCode::SUCCESS;
}

} // namespace orc
