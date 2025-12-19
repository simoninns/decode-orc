/*
 * File:        field_id.cpp
 * Module:      orc-core
 * Purpose:     Field identifier implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "field_id.h"
#include <sstream>

namespace orc {

std::string FieldID::to_string() const {
    if (!is_valid()) {
        return "FieldID::INVALID";
    }
    std::ostringstream oss;
    oss << "FieldID(" << value_ << ")";
    return oss.str();
}

} // namespace orc
