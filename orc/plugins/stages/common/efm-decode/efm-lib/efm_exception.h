/*
 * File:        efm_exception.h
 * Purpose:     EFM-library - Exception type for unrecoverable decode errors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef EFM_EXCEPTION_H
#define EFM_EXCEPTION_H

#include <stdexcept>
#include <string>

namespace efm {

// Thrown when the EFM decoder/encoder hits an unrecoverable condition
// (invariant violation, corrupt or unsupported data). Previously these
// sites called std::exit(1), which terminated the whole host process -
// unacceptable for a library. Callers should catch this at the pipeline
// boundary and report the failure instead of crashing.
class EfmDecodeError : public std::runtime_error {
 public:
  explicit EfmDecodeError(const std::string& where)
      : std::runtime_error("EFM decode aborted in " + where + "()") {}
};

}  // namespace efm

#endif  // EFM_EXCEPTION_H
