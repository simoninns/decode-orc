/*
 * File:        decoders.h
 * Purpose:     efm-decoder - Unified EFM decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DECODERS_H
#define DECODERS_H

#include <cstdint>
#include <deque>
#include <queue>
#include <string>
#include <vector>

#include "frame.h"
#include "logging.h"
#include "section.h"
#include "sector.h"

class Decoder {
 public:
  Decoder() = default;
  virtual void showStatistics() const {
    ORC_LOG_INFO("Decoder::showStatistics(): No statistics available");
  };
};

#endif  // DECODERS_H
