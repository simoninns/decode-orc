/*
 * File:        preview_helpers_yc_combine_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Verify YC preview "Y+C" mode removes chroma mid-code offset
 *              before combining, matching composite-like visualization.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../../../orc/core/include/preview_helpers.h"
#include "../../../orc/core/include/video_field_representation.h"

namespace orc_unit_test {
namespace {

class TestYCRepresentation final : public orc::VideoFieldRepresentation {
public:
    TestYCRepresentation(std::vector<uint16_t> field0_y,
                         std::vector<uint16_t> field0_c,
                         std::vector<uint16_t> field1_y,
                         std::vector<uint16_t> field1_c,
                         uint32_t width,
                         uint32_t height)
        : orc::VideoFieldRepresentation(orc::ArtifactID("test-yc-repr"), orc::Provenance{}),
          field0_y_(std::move(field0_y)),
          field0_c_(std::move(field0_c)),
          field1_y_(std::move(field1_y)),
          field1_c_(std::move(field1_c)) {
        params_.field_width = static_cast<int32_t>(width);
        params_.field_height = static_cast<int32_t>(height);
        params_.active_video_start = 0;
        params_.active_video_end = static_cast<int32_t>(width);
        params_.first_active_frame_line = 0;
        params_.last_active_frame_line = static_cast<int32_t>(height * 2);
        params_.black_16b_ire = 0;
        params_.white_16b_ire = 65535;
    }

    std::string type_name() const override { return "TestYCRepresentation"; }

    orc::FieldIDRange field_range() const override { return orc::FieldIDRange(orc::FieldID(0), orc::FieldID(2)); }
    size_t field_count() const override { return 2; }
    bool has_field(orc::FieldID id) const override { return id.is_valid() && id.value() < 2; }

    std::optional<orc::FieldDescriptor> get_descriptor(orc::FieldID id) const override {
        if (!has_field(id)) {
            return std::nullopt;
        }

        orc::FieldDescriptor d{};
        d.field_id = id;
        d.parity = (id.value() % 2 == 0) ? orc::FieldParity::Top : orc::FieldParity::Bottom;
        d.format = orc::VideoFormat::NTSC;
        d.system = orc::VideoSystem::NTSC;
        d.width = static_cast<size_t>(params_.field_width);
        d.height = static_cast<size_t>(params_.field_height);
        return d;
    }

    const sample_type* get_line(orc::FieldID, size_t) const override { return nullptr; }
    std::vector<sample_type> get_field(orc::FieldID) const override { return {}; }

    bool has_separate_channels() const override { return true; }

    std::vector<sample_type> get_field_luma(orc::FieldID id) const override {
        if (!has_field(id)) {
            return {};
        }
        return (id.value() == 0) ? field0_y_ : field1_y_;
    }

    std::vector<sample_type> get_field_chroma(orc::FieldID id) const override {
        if (!has_field(id)) {
            return {};
        }
        return (id.value() == 0) ? field0_c_ : field1_c_;
    }

    std::optional<orc::SourceParameters> get_video_parameters() const override {
        return params_;
    }

private:
    std::vector<uint16_t> field0_y_;
    std::vector<uint16_t> field0_c_;
    std::vector<uint16_t> field1_y_;
    std::vector<uint16_t> field1_c_;
    orc::SourceParameters params_{};
};

uint16_t combine_expected(uint16_t y, uint16_t c) {
    constexpr int32_t CHROMA_MID_CODE = 32768;
    const int32_t combined = static_cast<int32_t>(y) + (static_cast<int32_t>(c) - CHROMA_MID_CODE);
    return static_cast<uint16_t>(std::clamp(combined, 0, 65535));
}

uint8_t raw_scale_expected(uint16_t sample) {
    const int32_t raw_mult = static_cast<int32_t>((255.0 / 65535.0) * 65536.0);
    const int32_t scaled = (static_cast<int32_t>(sample) * raw_mult) >> 16;
    return static_cast<uint8_t>(std::clamp(scaled, 0, 255));
}

}  // namespace

TEST(PreviewHelpersYCCombineTest, fieldRawCompositeYc_removesChromaMidCodeBeforeSumming) {
    auto repr = std::make_shared<TestYCRepresentation>(
        std::vector<uint16_t>{40000, 10000},
        std::vector<uint16_t>{32768, 40000},
        std::vector<uint16_t>{0, 0},
        std::vector<uint16_t>{32768, 32768},
        2,
        1);

    const auto image = orc::PreviewHelpers::render_standard_preview_with_channel(
        repr,
        "field_raw",
        0,
        orc::RenderChannel::COMPOSITE_YC);

    ASSERT_EQ(image.width, 2u);
    ASSERT_EQ(image.height, 1u);
    ASSERT_EQ(image.rgb_data.size(), 6u);

    const uint16_t expected0 = combine_expected(40000, 32768);
    const uint16_t expected1 = combine_expected(10000, 40000);

    const uint8_t gray0 = image.rgb_data[0];
    const uint8_t gray1 = image.rgb_data[3];

    EXPECT_EQ(gray0, raw_scale_expected(expected0));
    EXPECT_EQ(gray1, raw_scale_expected(expected1));
}

TEST(PreviewHelpersYCCombineTest, splitRawCompositeYc_usesSameMidCodeRemovalAcrossFields) {
    auto repr = std::make_shared<TestYCRepresentation>(
        std::vector<uint16_t>{33000, 20000},
        std::vector<uint16_t>{32768, 34000},
        std::vector<uint16_t>{10000, 50000},
        std::vector<uint16_t>{30000, 32768},
        2,
        1);

    const auto image = orc::PreviewHelpers::render_standard_preview_with_channel(
        repr,
        "split_raw",
        0,
        orc::RenderChannel::COMPOSITE_YC);

    ASSERT_EQ(image.width, 2u);
    ASSERT_EQ(image.height, 2u);
    ASSERT_EQ(image.rgb_data.size(), 12u);

    const uint16_t top0 = combine_expected(33000, 32768);
    const uint16_t top1 = combine_expected(20000, 34000);
    const uint16_t bottom0 = combine_expected(10000, 30000);
    const uint16_t bottom1 = combine_expected(50000, 32768);

    EXPECT_EQ(image.rgb_data[0], raw_scale_expected(top0));
    EXPECT_EQ(image.rgb_data[3], raw_scale_expected(top1));
    EXPECT_EQ(image.rgb_data[6], raw_scale_expected(bottom0));
    EXPECT_EQ(image.rgb_data[9], raw_scale_expected(bottom1));
}

}  // namespace orc_unit_test
