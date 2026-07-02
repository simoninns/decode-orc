/*
 * File:        sha256_hash.h
 * Module:      orc-core
 * Purpose:     SHA-256 digest computation for plugin artifact integrity
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#if defined(ORC_GUI_BUILD)
#error \
    "GUI code cannot include core/include/sha256_hash.h. Use ProjectPresenter for plugin-aware stage access."
#endif
#if defined(ORC_CLI_BUILD)
#error \
    "CLI code cannot include core/include/sha256_hash.h. Use ProjectPresenter for plugin-aware stage access."
#endif

#include <string>
#include <string_view>

namespace orc {

// Compute the SHA-256 digest of an in-memory buffer.
// FIPS 180-4 SHA-256; returns 64 lowercase hexadecimal characters.
// Thread safety: pure function, safe to call concurrently.
std::string sha256_hex(std::string_view data);

// Compute the SHA-256 digest of a file's contents, streaming so large plugin
// binaries are not fully buffered in memory.
// Returns 64 lowercase hexadecimal characters, or an empty string on I/O
// failure (with a description in *error when provided).
// Thread safety: safe to call concurrently on distinct or identical paths.
std::string sha256_hex_of_file(const std::string& path,
                               std::string* error = nullptr);

}  // namespace orc
