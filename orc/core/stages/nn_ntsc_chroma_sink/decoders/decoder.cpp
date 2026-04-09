/*
 * File:        decoder.cpp
 * Module:      orc-core
 * Purpose:     Abstract base decoder interface for NN NTSC chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2019-2021 Adam Sampson
 * SPDX-FileCopyrightText: 2026 Simon Inns (Modified for decode-orc)
 */

#include "decoder.h"

namespace orc::nn_ntsc {

int32_t Decoder::getLookBehind() const { return 0; }
int32_t Decoder::getLookAhead()  const { return 0; }

}  // namespace orc::nn_ntsc
