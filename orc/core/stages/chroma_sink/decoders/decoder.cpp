/*
 * File:        decoder.cpp
 * Module:      orc-core
 * Purpose:     Base decoder interface
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2018-2025 Simon Inns
 * SPDX-FileCopyrightText: 2019-2020 Adam Sampson
 */


#include "decoder.h"

int32_t Decoder::getLookBehind() const
{
    return 0;
}

int32_t Decoder::getLookAhead() const
{
    return 0;
}
