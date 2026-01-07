/*
 * File:        node_id.cpp
 * Module:      orc-core
 * Purpose:     NodeID implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "include/node_id.h"

namespace orc {

std::string NodeID::to_string() const {
    if (!is_valid()) {
        return "invalid";
    }
    return std::to_string(id_);
}

} // namespace orc
