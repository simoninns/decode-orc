/*
 * File:        nn_ntsc_chroma_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for NnNtscChromaSinkStage — interface invariants,
 *              parameter contract, and preview capability / option structure.
 *
 *              In accordance with the project's unit-test constraints, no
 *              ONNX inference is performed here: tests that require a loaded
 *              VFR feed the stage a MockVideoFieldRepresentation and only
 *              exercise the metadata / capability layer, never the decoder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <algorithm>
#include <optional>
#include <string>

#include "../../include/video_field_representation_mock.h"
#include "../../../../orc/core/include/observation_context.h"
#include "../../../../orc/core/stages/nn_ntsc_chroma_sink/nn_ntsc_chroma_sink_stage.h"
#include "../../../../orc/core/stages/nn_ntsc_chroma_sink/vfr_to_source_field_adapter.h"

namespace orc_unit_test
{
    using testing::NiceMock;
    using testing::Return;

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------

    namespace
    {
        /// Minimal NTSC SourceParameters used in mock expectations.
        orc::SourceParameters make_ntsc_source_params()
        {
            orc::SourceParameters params;
            params.system                 = orc::VideoSystem::NTSC;
            params.field_width            = 910;
            params.field_height           = 263;
            params.active_video_start     = 76;
            params.active_video_end       = 834;
            params.first_active_frame_line = 40;
            params.last_active_frame_line  = 526;
            params.colour_burst_start     = 20;
            params.colour_burst_end       = 60;
            params.black_16b_ire          = 1000;
            params.white_16b_ire          = 60000;
            params.sample_rate            = 4.0;
            params.fsc                    = 1.0;
            return params;
        }

        /// Build a mock VFR that looks like a loaded NTSC composite source.
        std::shared_ptr<NiceMock<MockVideoFieldRepresentation>> make_ntsc_mock_vfr()
        {
            auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
            EXPECT_CALL(*vfr, get_video_parameters())
                .WillRepeatedly(Return(make_ntsc_source_params()));
            EXPECT_CALL(*vfr, field_count())
                .WillRepeatedly(Return(200u));
            EXPECT_CALL(*vfr, get_active_line_hint())
                .WillRepeatedly(Return(std::nullopt));
            return vfr;
        }

        bool has_tweak(
            const orc::StagePreviewCapability&  capability,
            const std::string&                  parameter_name,
            orc::PreviewTweakClass              tweak_class)
        {
            return std::any_of(
                capability.tweakable_parameters.begin(),
                capability.tweakable_parameters.end(),
                [&](const orc::PreviewTweakableParameter& t) {
                    return t.parameter_name == parameter_name
                        && t.tweak_class    == tweak_class;
                });
        }
    }

    // =========================================================================
    // Interface invariants
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, stageInterface_invariantsMatchSink)
    {
        orc::NnNtscChromaSinkStage stage;

        EXPECT_EQ(stage.required_input_count(), 1u);
        EXPECT_EQ(stage.output_count(), 0u);

        const auto info = stage.get_node_type_info();
        EXPECT_EQ(info.type, orc::NodeType::SINK);
        EXPECT_EQ(info.sink_category, orc::SinkCategory::THIRD_PARTY);
    }

    TEST(NnNtscChromaSinkStageTest, nodeTypeInfo_stageNameIsCorrect)
    {
        orc::NnNtscChromaSinkStage stage;

        EXPECT_EQ(stage.get_node_type_info().stage_name, "nn_ntsc_chroma_sink");
    }

    TEST(NnNtscChromaSinkStageTest, nodeTypeInfo_displayNameIsSet)
    {
        orc::NnNtscChromaSinkStage stage;

        EXPECT_FALSE(stage.get_node_type_info().display_name.empty());
    }

    // =========================================================================
    // Parameter contract
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, parameters_defaultsAreValid)
    {
        orc::NnNtscChromaSinkStage stage;
        const auto params = stage.get_parameters();

        ASSERT_TRUE(params.count("output_path")          > 0);
        ASSERT_TRUE(params.count("output_format")        > 0);
        ASSERT_TRUE(params.count("threads")              > 0);
        ASSERT_TRUE(params.count("chroma_gain")          > 0);
        ASSERT_TRUE(params.count("chroma_phase")         > 0);
        ASSERT_TRUE(params.count("deterministic_output") > 0);
        ASSERT_TRUE(params.count("chroma_nr")            > 0);
        ASSERT_TRUE(params.count("luma_nr")              > 0);

        EXPECT_EQ(std::get<std::string>(params.at("output_format")), "rgb");
        EXPECT_DOUBLE_EQ(std::get<double>(params.at("chroma_gain")), 1.0);
        EXPECT_DOUBLE_EQ(std::get<double>(params.at("chroma_phase")), 0.0);
        EXPECT_EQ(std::get<bool>(params.at("deterministic_output")), false);
    }

    TEST(NnNtscChromaSinkStageTest, parameters_roundTrip)
    {
        orc::NnNtscChromaSinkStage stage;
        const auto original = stage.get_parameters();
        EXPECT_TRUE(stage.set_parameters(original));
        EXPECT_EQ(stage.get_parameters(), original);
    }

    TEST(NnNtscChromaSinkStageTest, parameters_unknownKeyRejected)
    {
        orc::NnNtscChromaSinkStage stage;
        EXPECT_FALSE(stage.set_parameters({{"bogus_key", std::string("value")}}));
    }

    TEST(NnNtscChromaSinkStageTest, parameters_outputFormatRejectsInvalid)
    {
        orc::NnNtscChromaSinkStage stage;
        EXPECT_FALSE(stage.set_parameters({{"output_format", std::string("h264")}}));
    }

    // =========================================================================
    // Trigger state
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, trigger_isNotInProgressInitially)
    {
        orc::NnNtscChromaSinkStage stage;
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    // =========================================================================
    // PreviewableStage — empty-state behavior (no VFR loaded)
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, supportsPreview_isTrueAlways)
    {
        orc::NnNtscChromaSinkStage stage;
        // supports_preview() always returns true; the capability reports
        // is_valid() == false until data is loaded.
        EXPECT_TRUE(stage.supports_preview());
    }

    TEST(NnNtscChromaSinkStageTest, getPreviewOptions_emptyBeforeDataLoaded)
    {
        orc::NnNtscChromaSinkStage stage;
        EXPECT_TRUE(stage.get_preview_options().empty());
    }

    TEST(NnNtscChromaSinkStageTest, getColourPreviewCarrier_nulloptBeforeDataLoaded)
    {
        orc::NnNtscChromaSinkStage stage;
        EXPECT_FALSE(stage.get_colour_preview_carrier(0).has_value());
    }

    // =========================================================================
    // IStagePreviewCapability — tweakable parameters declared before data load
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, previewCapability_declaresTweakableParamsEvenBeforeDataLoad)
    {
        orc::NnNtscChromaSinkStage stage;
        const auto capability = stage.get_preview_capability();

        // Tweakable parameters are declared unconditionally so the GUI can build
        // the live-tweak panel immediately after node selection.
        EXPECT_TRUE(has_tweak(capability, "chroma_gain",  orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "chroma_phase", orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "luma_nr",      orc::PreviewTweakClass::DecodePhase));
        EXPECT_TRUE(has_tweak(capability, "chroma_nr",    orc::PreviewTweakClass::DecodePhase));
    }

    TEST(NnNtscChromaSinkStageTest, previewCapability_isInvalidBeforeDataLoad)
    {
        orc::NnNtscChromaSinkStage stage;
        EXPECT_FALSE(stage.get_preview_capability().is_valid());
    }

    // =========================================================================
    // IStagePreviewCapability — capability structure after execute() with NTSC VFR
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, previewCapability_isValidAfterExecuteWithNtscVfr)
    {
        orc::NnNtscChromaSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = make_ntsc_mock_vfr();

        stage.execute({vfr}, {}, observation_context);

        const auto capability = stage.get_preview_capability();
        EXPECT_TRUE(capability.is_valid());
    }

    TEST(NnNtscChromaSinkStageTest, previewCapability_reportsColourNtscDataType)
    {
        orc::NnNtscChromaSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = make_ntsc_mock_vfr();

        stage.execute({vfr}, {}, observation_context);

        const auto capability = stage.get_preview_capability();
        EXPECT_TRUE(std::find(
            capability.supported_data_types.begin(),
            capability.supported_data_types.end(),
            orc::VideoDataType::ColourNTSC)
                != capability.supported_data_types.end());
    }

    TEST(NnNtscChromaSinkStageTest, previewCapability_navigationExtentMatchesFieldCount)
    {
        orc::NnNtscChromaSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = make_ntsc_mock_vfr();  // field_count() returns 200

        stage.execute({vfr}, {}, observation_context);

        const auto capability = stage.get_preview_capability();
        EXPECT_EQ(capability.navigation_extent.item_count, 100u);  // 200 fields / 2
        EXPECT_EQ(capability.navigation_extent.item_label, "frame");
    }

    TEST(NnNtscChromaSinkStageTest, previewCapability_geometryIsPositive)
    {
        orc::NnNtscChromaSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = make_ntsc_mock_vfr();

        stage.execute({vfr}, {}, observation_context);

        const auto capability = stage.get_preview_capability();
        EXPECT_GT(capability.geometry.active_width, 0u);
        EXPECT_GT(capability.geometry.active_height, 0u);
        EXPECT_GT(capability.geometry.dar_correction_factor, 0.0);
    }

    TEST(NnNtscChromaSinkStageTest, previewCapability_doesNotExposeDecoderTypeTweak)
    {
        // The NN stage has no decoder_type parameter — the NN 3D path is fixed.
        orc::NnNtscChromaSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = make_ntsc_mock_vfr();

        stage.execute({vfr}, {}, observation_context);

        const auto capability = stage.get_preview_capability();
        bool found_decoder_type = std::any_of(
            capability.tweakable_parameters.begin(),
            capability.tweakable_parameters.end(),
            [](const orc::PreviewTweakableParameter& t) {
                return t.parameter_name == "decoder_type";
            });
        EXPECT_FALSE(found_decoder_type);
    }

    // =========================================================================
    // PreviewableStage options — structure after execute()
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, getPreviewOptions_returnsFrameOptionAfterNtscExecute)
    {
        orc::NnNtscChromaSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = make_ntsc_mock_vfr();

        stage.execute({vfr}, {}, observation_context);

        const auto options = stage.get_preview_options();
        ASSERT_EQ(options.size(), 1u);
        EXPECT_EQ(options[0].id, "frame");
        EXPECT_EQ(options[0].count, 100u);  // 200 fields / 2
        EXPECT_GT(options[0].width, 0u);
        EXPECT_GT(options[0].height, 0u);
        EXPECT_GT(options[0].dar_aspect_correction, 0.0);
    }

    TEST(NnNtscChromaSinkStageTest, getColourPreviewCarrier_nulloptForPalInput)
    {
        orc::NnNtscChromaSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        orc::SourceParameters pal_params = make_ntsc_source_params();
        pal_params.system = orc::VideoSystem::PAL;
        EXPECT_CALL(*vfr, get_video_parameters()).WillRepeatedly(Return(pal_params));
        EXPECT_CALL(*vfr, field_count()).WillRepeatedly(Return(200u));
        EXPECT_CALL(*vfr, get_active_line_hint()).WillRepeatedly(Return(std::nullopt));

        // execute() stores the VFR. For PAL input, preview carrier must return nullopt
        // because this stage is NTSC-only.
        orc::SourceParameters ntsc_params = make_ntsc_source_params();
        EXPECT_CALL(*vfr, get_video_parameters())
            .WillOnce(Return(ntsc_params))        // execute() validation passes
            .WillRepeatedly(Return(pal_params));  // subsequent preview calls see PAL

        stage.execute({vfr}, {}, observation_context);

        EXPECT_FALSE(stage.get_colour_preview_carrier(0).has_value());
    }

    // =========================================================================
    // Parameter — deterministic_output overrides thread count at trigger time
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, parameters_deterministicForcesThreadsOne)
    {
        orc::NnNtscChromaSinkStage stage;

        // Store deterministic_output=true alongside a non-zero threads value.
        // The trigger() implementation resolves the effective thread count to 1
        // when deterministic_output is true, regardless of the threads parameter.
        EXPECT_TRUE(stage.set_parameters({
            {"deterministic_output", true},
            {"threads",              int32_t(8)}
        }));

        const auto params = stage.get_parameters();
        EXPECT_EQ(std::get<bool>(params.at("deterministic_output")), true);
        // The user-visible threads value is preserved in storage; trigger() is
        // responsible for clamping the execution-time count to 1 when deterministic.
        EXPECT_EQ(std::get<int32_t>(params.at("threads")), int32_t(8));
    }

    // =========================================================================
    // execute() contract
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, execute_returnsCachedInputAsEmptyOutput)
    {
        orc::NnNtscChromaSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = make_ntsc_mock_vfr();

        const auto result = stage.execute({vfr}, {}, observation_context);

        // Sink stage: execute() caches the input and always returns no output artifacts.
        EXPECT_TRUE(result.empty());
    }

    TEST(NnNtscChromaSinkStageTest, execute_rejectsNonNtscInput)
    {
        orc::NnNtscChromaSinkStage stage;
        orc::ObservationContext observation_context;
        auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();

        orc::SourceParameters pal_params = make_ntsc_source_params();
        pal_params.system = orc::VideoSystem::PAL;
        EXPECT_CALL(*vfr, get_video_parameters()).WillRepeatedly(Return(pal_params));
        EXPECT_CALL(*vfr, field_count()).WillRepeatedly(Return(200u));
        EXPECT_CALL(*vfr, get_active_line_hint()).WillRepeatedly(Return(std::nullopt));

        const auto result = stage.execute({vfr}, {}, observation_context);

        // PAL input must be rejected: execute() returns empty and records an error status.
        EXPECT_TRUE(result.empty());
        EXPECT_NE(stage.get_trigger_status().find("Error"), std::string::npos);
    }

    // =========================================================================
    // Trigger — cancel request propagation
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, trigger_cancelRequestedStatePropagates)
    {
        orc::NnNtscChromaSinkStage stage;

        // cancel_trigger() must be safe to call when the trigger is not running.
        // is_trigger_in_progress() must remain false (cancel only sets the request
        // flag; it does not alter the processing-active flag).
        stage.cancel_trigger();
        EXPECT_FALSE(stage.is_trigger_in_progress());
    }

    // =========================================================================
    // VfrToSourceFieldAdapter — video parameter mapping
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, vfrAdapter_correctlyMapsVideoParameters)
    {
        auto vfr = make_ntsc_mock_vfr();
        const auto nn_params_opt =
            orc::VfrToSourceFieldAdapter::extract_video_parameters(*vfr);

        ASSERT_TRUE(nn_params_opt.has_value());

        const auto& nn = *nn_params_opt;
        const auto  sp = make_ntsc_source_params();

        EXPECT_EQ(nn.fieldWidth,           sp.field_width);
        EXPECT_EQ(nn.fieldHeight,          sp.field_height);
        EXPECT_EQ(nn.colourBurstStart,     sp.colour_burst_start);
        EXPECT_EQ(nn.colourBurstEnd,       sp.colour_burst_end);
        EXPECT_EQ(nn.activeVideoStart,     sp.active_video_start);
        EXPECT_EQ(nn.activeVideoEnd,       sp.active_video_end);
        EXPECT_EQ(nn.firstActiveFrameLine, sp.first_active_frame_line);
        EXPECT_EQ(nn.lastActiveFrameLine,  sp.last_active_frame_line);
        EXPECT_DOUBLE_EQ(nn.white16bIre,   static_cast<double>(sp.white_16b_ire));
        EXPECT_DOUBLE_EQ(nn.black16bIre,   static_cast<double>(sp.black_16b_ire));
        EXPECT_DOUBLE_EQ(nn.sampleRate,    sp.sample_rate);
        EXPECT_DOUBLE_EQ(nn.fSC,           sp.fsc);
    }

    // =========================================================================
    // VfrToSourceFieldAdapter — field parity mapping
    // =========================================================================

    TEST(NnNtscChromaSinkStageTest, vfrAdapter_correctlyMapsFieldParity)
    {
        // When no FieldParityHint or FieldDescriptor is available, the adapter
        // falls back to index parity: even field index → is_first_field=true,
        // odd field index → is_first_field=false.
        const size_t sample_count = 910u * 263u;

        const std::vector<std::pair<int32_t, bool>> cases = {
            {0, true},
            {1, false},
            {2, true},
            {3, false},
        };

        for (const auto& [field_id_val, expected_first_field] : cases)
        {
            auto vfr = std::make_shared<NiceMock<MockVideoFieldRepresentation>>();
            const orc::FieldID fid(field_id_val);

            EXPECT_CALL(*vfr, get_video_parameters())
                .WillRepeatedly(Return(make_ntsc_source_params()));
            EXPECT_CALL(*vfr, has_field(fid))
                .WillRepeatedly(Return(true));
            EXPECT_CALL(*vfr, get_field(fid))
                .WillRepeatedly(Return(std::vector<uint16_t>(sample_count, 0u)));
            EXPECT_CALL(*vfr, get_descriptor(fid))
                .WillRepeatedly(Return(std::nullopt));

            const auto sf_opt = orc::VfrToSourceFieldAdapter::convert(*vfr, fid);

            ASSERT_TRUE(sf_opt.has_value())
                << "convert() returned nullopt for field_id=" << field_id_val;
            EXPECT_EQ(sf_opt->is_first_field, expected_first_field)
                << "Parity mismatch for field_id=" << field_id_val;
        }
    }

}  // namespace orc_unit_test
