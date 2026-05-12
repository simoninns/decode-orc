/*
 * File:        source_stage_descriptor_test_utils.h
 * Module:      orc-core-tests
 * Purpose:     Shared descriptor assertion helpers for source stage tests.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "../../../../orc/core/include/stage_parameter.h"

namespace orc_unit_test
{
    inline const orc::ParameterDescriptor* find_descriptor_by_name(
        const std::vector<orc::ParameterDescriptor>& descriptors,
        const std::string& name)
    {
        const auto it = std::find_if(
            descriptors.begin(),
            descriptors.end(),
            [&](const orc::ParameterDescriptor& descriptor) {
                return descriptor.name == name;
            });

        if (it == descriptors.end()) {
            return nullptr;
        }

        return &(*it);
    }

    inline void expect_file_path_descriptor(
        const std::vector<orc::ParameterDescriptor>& descriptors,
        const std::string& name,
        const std::string& extension_hint)
    {
        const auto* descriptor = find_descriptor_by_name(descriptors, name);
        ASSERT_NE(descriptor, nullptr);
        EXPECT_EQ(descriptor->type, orc::ParameterType::FILE_PATH);
        EXPECT_EQ(descriptor->file_extension_hint, extension_hint);
    }

    inline void expect_empty_string_default(
        const std::vector<orc::ParameterDescriptor>& descriptors,
        const std::string& name)
    {
        const auto* descriptor = find_descriptor_by_name(descriptors, name);
        ASSERT_NE(descriptor, nullptr);
        ASSERT_TRUE(descriptor->constraints.default_value.has_value());
        ASSERT_TRUE(std::holds_alternative<std::string>(*descriptor->constraints.default_value));
        EXPECT_EQ(std::get<std::string>(*descriptor->constraints.default_value), "");
    }

    inline void expect_all_descriptors_optional(
        const std::vector<orc::ParameterDescriptor>& descriptors)
    {
        for (const auto& descriptor : descriptors) {
            EXPECT_FALSE(descriptor.constraints.required)
                << "Parameter '" << descriptor.name << "' should be optional";
        }
    }
} // namespace orc_unit_test
