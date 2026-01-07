/*
 * File:        stage_parameter.cpp
 * Module:      orc-core
 * Purpose:     Stage Parameter
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "stage_parameter.h"
#include <sstream>
#include <iomanip>

namespace orc {
namespace parameter_util {

std::string value_to_string(const ParameterValue& value) {
    return std::visit([](auto&& arg) -> std::string {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            return arg ? "true" : "false";
        } else if constexpr (std::is_same_v<T, std::string>) {
            return arg;
        } else {
            return std::to_string(arg);
        }
    }, value);
}

std::optional<ParameterValue> string_to_value(const std::string& str, ParameterType type) {
    try {
        switch (type) {
            case ParameterType::INT32:
                return static_cast<int32_t>(std::stoi(str));
            case ParameterType::UINT32:
                return static_cast<uint32_t>(std::stoul(str));
            case ParameterType::DOUBLE:
                return std::stod(str);
            case ParameterType::BOOL:
                if (str == "true" || str == "1" || str == "yes") return true;
                if (str == "false" || str == "0" || str == "no") return false;
                return std::nullopt;
            case ParameterType::STRING:
            case ParameterType::FILE_PATH:
                return str;
        }
    } catch (const std::exception&) {
        return std::nullopt;
    }
    return std::nullopt;
}

const char* type_name(ParameterType type) {
    switch (type) {
        case ParameterType::INT32: return "int32";
        case ParameterType::UINT32: return "uint32";
        case ParameterType::DOUBLE: return "double";
        case ParameterType::BOOL: return "bool";
        case ParameterType::STRING: return "string";
        case ParameterType::FILE_PATH: return "file_path";
    }
    return "unknown";
}

} // namespace parameter_util
} // namespace orc
