/*
 * File:        decoders.h
 * Module:      ld-efm-decoder
 * Purpose:     EFM data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DECODERS_H
#define DECODERS_H

#include <vector>
#include <deque>
#include <cstdint>

#include <logging.h>
#include "frame.h"
#include "section.h"

class Decoder
{
public:
    Decoder() = default;
    virtual void showStatistics() const
    {
        LOG_INFO("Decoder::showStatistics(): No statistics available");
    };
};

#endif // DECODERS_H