/*
 * File:        pal_m_integration_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Integration test for PAL-M support across decoder pipeline
 *
 * Tests that PAL-M video parameters flow correctly through the shared
 * PAL decoder path and produce valid output.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <memory>
#include <vector>

#include "../../../../orc/core/stages/chroma_sink/decoders/paldecoder.h"
#include "../../../../orc/core/stages/chroma_sink/decoders/palcolour.h"
#include "../../../../orc/core/stages/chroma_sink/decoders/sourcefield.h"
#include "../../../../orc/core/stages/chroma_sink/decoders/componentframe.h"

namespace orc_unit_test
{
    namespace
    {
        // Create standard PAL-M video parameters matching Brazilian PAL-M specs
        orc::SourceParameters make_pal_m_video_params()
        {
            orc::SourceParameters p;
            p.system = orc::VideoSystem::PAL_M;
            p.field_width = 909;
            p.field_height = 263;
            p.active_video_start = 16;
            p.active_video_end = 893;
            p.first_active_frame_line = 0;
            p.last_active_frame_line = 520;  // 525 lines, -1 for 0-indexing adjustment
            p.first_active_field_line = 0;
            p.last_active_field_line = 262;
            p.colour_burst_start = 16;
            p.colour_burst_end = 110;
            p.black_16b_ire = 0;
            p.white_16b_ire = 100;
            // PAL-M FSC and sample rate per official specs
            p.sample_rate = 14318181.8181;  // 4 * FSC
            p.fsc = 3575611.89;
            p.active_area_cropping_applied = false;
            return p;
        }

        // Create standard PAL video parameters for comparison
        orc::SourceParameters make_pal_video_params()
        {
            orc::SourceParameters p;
            p.system = orc::VideoSystem::PAL;
            p.field_width = 909;
            p.field_height = 313;
            p.active_video_start = 16;
            p.active_video_end = 893;
            p.first_active_frame_line = 0;
            p.last_active_frame_line = 620;
            p.first_active_field_line = 0;
            p.last_active_field_line = 312;
            p.colour_burst_start = 16;
            p.colour_burst_end = 110;
            p.black_16b_ire = 0;
            p.white_16b_ire = 100;
            // Standard PAL FSC and sample rate
            p.sample_rate = 17734472.0;  // 4 * FSC
            p.fsc = 4433618.75;
            p.active_area_cropping_applied = false;
            return p;
        }

        // Create a simple YC test field with known patterns
        SourceField make_pal_m_yc_test_field(bool is_first, uint16_t base_y, uint16_t base_c)
        {
            SourceField field;
            field.is_yc = true;
            field.is_first_field = is_first;

            const int width = 909;
            const int height = 263;

            field.luma_data.resize(width * height, 0);
            field.chroma_data.resize(width * height, 0);

            // Fill with simple gradients to test processing
            for (int line = 0; line < height; ++line) {
                for (int x = 0; x < width; ++x) {
                    int idx = line * width + x;
                    // Simple gradient patterns
                    field.luma_data[idx] = static_cast<uint16_t>(base_y + (line % 256));
                    field.chroma_data[idx] = static_cast<uint16_t>(base_c + ((x % 4) * 16));
                }
            }

            return field;
        }
    }

    // =========================================================================
    // PAL-M Decoder Configuration Tests
    // =========================================================================

    TEST(PalMIntegrationTest, palColourAcceptsPalM)
    {
        const auto params = make_pal_m_video_params();

        // Create decoder with standard PAL configuration
        PalColour::Configuration pal_config;
        pal_config.chromaFilter = PalColour::palColourFilter;
        PalColour decoder;

        // Configure with PAL-M parameters
        EXPECT_NO_THROW({
            decoder.updateConfiguration(params, pal_config);
        }) << "PalColour should accept PAL-M parameters";
    }

    // =========================================================================
    // PAL-M vs PAL Parameter Validation Tests
    // =========================================================================

    TEST(PalMIntegrationTest, palMHasCorrectFieldHeight)
    {
        const auto pal_params = make_pal_video_params();
        const auto pal_m_params = make_pal_m_video_params();

        // PAL uses 625 lines (313 per field)
        EXPECT_EQ(pal_params.field_height, 313);
        EXPECT_EQ(pal_params.field_height * 2 - 1, 625);

        // PAL-M uses 525 lines (263 per field, -1 adjustment)
        EXPECT_EQ(pal_m_params.field_height, 263);
        EXPECT_EQ(pal_m_params.field_height * 2 - 1, 525);
    }

    TEST(PalMIntegrationTest, palMHasCorrectFsc)
    {
        const auto pal_params = make_pal_video_params();
        const auto pal_m_params = make_pal_m_video_params();

        // PAL FSC ~4.43 MHz
        EXPECT_NEAR(pal_params.fsc, 4433618.75, 100.0);

        // PAL-M FSC ~3.58 MHz (distinct from PAL)
        EXPECT_NEAR(pal_m_params.fsc, 3575611.89, 100.0);

        // Verify they are distinctly different
        EXPECT_NE(pal_params.fsc, pal_m_params.fsc);
    }

    // =========================================================================
    // PAL-M Decoder Decoding Tests
    // =========================================================================

    TEST(PalMIntegrationTest, palColourDecodesYcFieldWithPalMParameters)
    {
        const auto params = make_pal_m_video_params();

        PalColour::Configuration config;
        config.chromaFilter = PalColour::palColourFilter;
        config.chromaGain = 1.0;
        config.chromaPhase = 0.0;
        config.yNRLevel = 0.0;

        PalColour decoder;
        decoder.updateConfiguration(params, config);

        // Create test fields
        std::vector<SourceField> input_fields;
        input_fields.push_back(make_pal_m_yc_test_field(true, 50, 64));
        input_fields.push_back(make_pal_m_yc_test_field(false, 52, 64));

        // Decode frames
        std::vector<ComponentFrame> output_frames(1);
        ASSERT_NO_THROW({
            decoder.decodeFrames(input_fields, 0, 2, output_frames);
        }) << "Decoding YC fields with PAL-M parameters should not throw";

        // Verify output dimensions match input parameters
        EXPECT_EQ(output_frames[0].getWidth(), params.field_width);
        // Frame height = (field_height * 2) - 1 for interlaced
        EXPECT_EQ(output_frames[0].getHeight(), (params.field_height * 2) - 1);
    }

    TEST(PalMIntegrationTest, palColourPreservesYcDataPath)
    {
        const auto params = make_pal_m_video_params();

        PalColour::Configuration config;
        config.chromaFilter = PalColour::palColourFilter;
        config.yNRLevel = 0.0;

        PalColour decoder;
        decoder.updateConfiguration(params, config);

        // Create test field with known luma values
        std::vector<SourceField> input_fields;
        input_fields.push_back(make_pal_m_yc_test_field(true, 100, 64));
        input_fields.push_back(make_pal_m_yc_test_field(false, 102, 64));

        std::vector<ComponentFrame> output_frames(1);
        decoder.decodeFrames(input_fields, 0, 2, output_frames);

        // In YC path, luma should be copied directly (not filtered)
        // Sample a point in the active area
        const double* y_data = output_frames[0].y(10);
        ASSERT_NE(y_data, nullptr);

        // Luma values should be in the expected range
        for (int i = 16; i < 893; ++i) {  // active_video_start to active_video_end
            double luma = y_data[i];
            // Should be in the range of our test data (100-102 + line offset)
            EXPECT_GT(luma, 50.0) << "Luma values should be preserved in YC path";
            EXPECT_LT(luma, 250.0) << "Luma values should be in reasonable range";
        }
    }

    // =========================================================================
    // PAL-M Transform Filter Configuration Tests
    // =========================================================================

    TEST(PalMIntegrationTest, palColourSupportsTransform2dFilterWithPalM)
    {
        const auto params = make_pal_m_video_params();

        PalColour::Configuration config;
        config.chromaFilter = PalColour::transform2DFilter;
        config.transformThreshold = 0.4;

        PalColour decoder;
        // Should configure without error
        ASSERT_NO_THROW({
            decoder.updateConfiguration(params, config);
        }) << "PalColour should support Transform 2D filter with PAL-M";
    }

    TEST(PalMIntegrationTest, palColourSupportsTransform3dFilterWithPalM)
    {
        const auto params = make_pal_m_video_params();

        PalColour::Configuration config;
        config.chromaFilter = PalColour::transform3DFilter;
        config.transformThreshold = 0.4;

        PalColour decoder;
        // Should configure without error
        ASSERT_NO_THROW({
            decoder.updateConfiguration(params, config);
        }) << "PalColour should support Transform 3D filter with PAL-M";
    }

    // =========================================================================
    // PAL-M vs PAL Comparative Tests
    // =========================================================================

    TEST(PalMIntegrationTest, palAndPalMDecoderConfigureIndependently)
    {
        const auto pal_params = make_pal_video_params();
        const auto pal_m_params = make_pal_m_video_params();

        PalColour::Configuration config;
        config.chromaFilter = PalColour::palColourFilter;

        PalColour decoder;

        // Configure with PAL
        EXPECT_NO_THROW({
            decoder.updateConfiguration(pal_params, config);
        }) << "Should configure with PAL parameters";

        // Reconfigure with PAL-M
        EXPECT_NO_THROW({
            decoder.updateConfiguration(pal_m_params, config);
        }) << "Should reconfigure with PAL-M parameters";

        // Reconfigure back to PAL
        EXPECT_NO_THROW({
            decoder.updateConfiguration(pal_params, config);
        }) << "Should reconfigure back to PAL parameters";
    }

    TEST(PalMIntegrationTest, palMDecoderLookAroundValuesMatchPal)
    {
        // PAL-M should have same look-behind/look-ahead as PAL
        // since they share the same decoder implementation

        PalColour::Configuration pal_config;
        pal_config.chromaFilter = PalColour::palColourFilter;

        PalColour pal_decoder;
        pal_decoder.updateConfiguration(make_pal_video_params(), pal_config);
        auto pal_look_behind = pal_decoder.getConfiguration().getLookBehind();
        auto pal_look_ahead = pal_decoder.getConfiguration().getLookAhead();

        PalColour pal_m_decoder;
        pal_m_decoder.updateConfiguration(make_pal_m_video_params(), pal_config);
        auto pal_m_look_behind = pal_m_decoder.getConfiguration().getLookBehind();
        auto pal_m_look_ahead = pal_m_decoder.getConfiguration().getLookAhead();

        // Should match since both use palColourFilter
        EXPECT_EQ(pal_look_behind, pal_m_look_behind);
        EXPECT_EQ(pal_look_ahead, pal_m_look_ahead);
    }
}
