/*
 * File:        chroma_sink_stage_defaults_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for ChromaSinkStage parameter defaults
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>

#include "../../../../orc/plugins/stages/sinks/common/chroma_sink_stage.h"

namespace orc_unit_test
{
    TEST(ChromaSinkStageDefaultsTest, runtimeDefaults_enableNtscPhaseCompensation)
    {
        orc::ChromaSinkStage stage;
        auto params = stage.get_parameters();

        auto it = params.find("ntsc_phase_comp");
        ASSERT_NE(it, params.end());
        ASSERT_TRUE(std::holds_alternative<bool>(it->second));
        EXPECT_TRUE(std::get<bool>(it->second));
    }

    TEST(ChromaSinkStageDefaultsTest, descriptorDefaults_enableNtscPhaseCompensationForNtscProjects)
    {
        orc::ChromaSinkStage stage;
        auto descriptors = stage.get_parameter_descriptors(orc::VideoSystem::NTSC, orc::SourceType::YC);

        auto it = std::find_if(descriptors.begin(), descriptors.end(), [](const orc::ParameterDescriptor& desc) {
            return desc.name == "ntsc_phase_comp";
        });

        ASSERT_NE(it, descriptors.end());
        ASSERT_TRUE(it->constraints.default_value.has_value());
        ASSERT_TRUE(std::holds_alternative<bool>(*it->constraints.default_value));
        EXPECT_TRUE(std::get<bool>(*it->constraints.default_value));
    }
}
