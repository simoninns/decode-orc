/*
 * File:        ntsc_pal_decoder_wrapper_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for NTSC/PAL decoder wrapper configuration
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include "../../../../orc/core/stages/chroma_sink/decoders/ntscdecoder.h"
#include "../../../../orc/core/stages/chroma_sink/decoders/paldecoder.h"

namespace orc_unit_test
{
    namespace
    {
        class TestableNtscDecoder : public NtscDecoder
        {
        public:
            explicit TestableNtscDecoder(const Comb::Configuration& config)
                : NtscDecoder(config)
            {
            }

            void decodeFrames(const std::vector<SourceField>&,
                              int32_t,
                              int32_t,
                              std::vector<ComponentFrame>&) override
            {
            }
        };

        class TestablePalDecoder : public PalDecoder
        {
        public:
            explicit TestablePalDecoder(const PalColour::Configuration& config)
                : PalDecoder(config)
            {
            }

            void decodeFrames(const std::vector<SourceField>&,
                              int32_t,
                              int32_t,
                              std::vector<ComponentFrame>&) override
            {
            }
        };

        orc::SourceParameters make_ntsc_params()
        {
            orc::SourceParameters p;
            p.system = orc::VideoSystem::NTSC;
            p.field_width = 32;
            p.field_height = 4;
            p.sample_rate = 4.0;
            p.fsc = 1.0;
            return p;
        }

        orc::SourceParameters make_pal_params()
        {
            orc::SourceParameters p;
            p.system = orc::VideoSystem::PAL;
            p.field_width = 32;
            p.field_height = 4;
            p.sample_rate = 4.0;
            p.fsc = 1.0;
            return p;
        }
    }

    TEST(NtscDecoderWrapperTest, configure_acceptsNtscAndRejectsPal)
    {
        Comb::Configuration config;
        TestableNtscDecoder decoder(config);

        EXPECT_TRUE(decoder.configure(make_ntsc_params()));
        EXPECT_FALSE(decoder.configure(make_pal_params()));
    }

    TEST(NtscDecoderWrapperTest, lookAround_followsCombConfiguration)
    {
        Comb::Configuration config;
        config.dimensions = 3;
        TestableNtscDecoder decoder(config);

        EXPECT_EQ(decoder.getLookBehind(), 1);
        EXPECT_EQ(decoder.getLookAhead(), 1);
    }

    TEST(PalDecoderWrapperTest, configure_acceptsPalAndRejectsNtsc)
    {
        PalColour::Configuration config;
        TestablePalDecoder decoder(config);

        EXPECT_TRUE(decoder.configure(make_pal_params()));
        EXPECT_FALSE(decoder.configure(make_ntsc_params()));
    }

    TEST(PalDecoderWrapperTest, lookAround_dependsOnPalFilterMode)
    {
        PalColour::Configuration config_2d;
        config_2d.chromaFilter = PalColour::transform2DFilter;
        TestablePalDecoder decoder_2d(config_2d);

        EXPECT_EQ(decoder_2d.getLookBehind(), 0);
        EXPECT_EQ(decoder_2d.getLookAhead(), 0);

        PalColour::Configuration config_3d;
        config_3d.chromaFilter = PalColour::transform3DFilter;
        TestablePalDecoder decoder_3d(config_3d);

        EXPECT_GT(decoder_3d.getLookBehind(), 0);
        EXPECT_GT(decoder_3d.getLookAhead(), 0);
    }
}
