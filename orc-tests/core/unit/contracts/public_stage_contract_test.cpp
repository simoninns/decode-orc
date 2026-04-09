/*
 * File:        public_stage_contract_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Shared Phase 5 contracts for public stage metadata and defaults
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include <set>
#include <string>

#include "../include/observation_context_interface_mock.h"
#include "../include/public_stage_inventory.h"
#include "../../../orc/core/include/observation_context.h"

namespace orc_unit_test
{
    namespace
    {
        using testing::TestParamInfo;

        const PublicStageSpec& stage_spec_at(size_t index)
        {
            return public_stage_specs().at(index);
        }

        std::string stage_param_name(const TestParamInfo<size_t>& info)
        {
            return stage_spec_at(info.param).inventory_id;
        }

        orc::ObservationValue value_for_type(orc::ObservationType type)
        {
            switch (type) {
                case orc::ObservationType::INT32:
                    return int32_t{7};
                case orc::ObservationType::INT64:
                    return int64_t{9};
                case orc::ObservationType::DOUBLE:
                    return 1.25;
                case orc::ObservationType::STRING:
                    return std::string("ok");
                case orc::ObservationType::BOOL:
                    return true;
                case orc::ObservationType::CUSTOM:
                    return std::string("custom");
            }

            return std::string("unknown");
        }

        orc::ObservationValue mismatched_value_for_type(orc::ObservationType type)
        {
            if (type == orc::ObservationType::STRING) {
                return int32_t{3};
            }

            return std::string("bad");
        }
    }

    class PublicStageContractTest : public testing::TestWithParam<size_t>
    {
    protected:
        const PublicStageSpec& spec() const
        {
            return stage_spec_at(GetParam());
        }
    };

    TEST(PublicStageInventoryTest, inventoryCountMatchesPhaseFiveScope)
    {
        EXPECT_EQ(public_stage_specs().size(), 26u);
    }

    TEST_P(PublicStageContractTest, interfaceMetadataIsConsistent)
    {
        auto stage = spec().create();
        const auto info = stage->get_node_type_info();

        ASSERT_FALSE(info.stage_name.empty());
        ASSERT_FALSE(info.display_name.empty());
        EXPECT_LE(info.min_inputs, info.max_inputs);
        EXPECT_LE(info.min_outputs, info.max_outputs);
        EXPECT_GE(stage->required_input_count(), info.min_inputs);
        EXPECT_LE(stage->required_input_count(), info.max_inputs);
        EXPECT_GE(stage->output_count(), info.min_outputs);
        EXPECT_LE(stage->output_count(), info.max_outputs);

        switch (info.type) {
            case orc::NodeType::SOURCE:
                EXPECT_EQ(stage->required_input_count(), 0u);
                EXPECT_EQ(info.min_inputs, 0u);
                EXPECT_EQ(info.max_inputs, 0u);
                EXPECT_GT(info.max_outputs, 0u);
                break;
            case orc::NodeType::SINK:
            case orc::NodeType::ANALYSIS_SINK:
                EXPECT_EQ(stage->output_count(), 0u);
                EXPECT_EQ(info.min_outputs, 0u);
                EXPECT_EQ(info.max_outputs, 0u);
                break;
            case orc::NodeType::TRANSFORM:
            case orc::NodeType::MERGER:
            case orc::NodeType::COMPLEX:
                EXPECT_GT(info.max_outputs, 0u);
                break;
        }
    }

    TEST_P(PublicStageContractTest, parameterDefaultsMatchRuntimeState)
    {
        if (!spec().registry_backed) {
            GTEST_SKIP() << "Stage is an internal, non-registry-backed base with stage-specific subclasses owning the full parameter surface";
        }

        auto stage = spec().create();
        auto* parameterized = dynamic_cast<orc::ParameterizedStage*>(stage.get());
        if (!parameterized) {
            GTEST_SKIP() << "Stage is not parameterized";
        }

        const auto descriptors = parameterized->get_parameter_descriptors();
        const auto parameters = parameterized->get_parameters();

        for (const auto& [name, value] : parameters) {
            auto it = std::find_if(descriptors.begin(), descriptors.end(), [&](const orc::ParameterDescriptor& descriptor) {
                return descriptor.name == name;
            });
            EXPECT_NE(it, descriptors.end()) << spec().inventory_id << " exposes runtime parameter without descriptor: '" << name << "'";
            (void)value;
        }

        for (const auto& descriptor : descriptors) {
            if (!descriptor.constraints.default_value.has_value()) {
                continue;
            }

            auto stage_with_default_applied = spec().create();
            auto* parameterized_copy = dynamic_cast<orc::ParameterizedStage*>(stage_with_default_applied.get());
            ASSERT_NE(parameterized_copy, nullptr);
            ASSERT_TRUE(parameterized_copy->set_parameters({{descriptor.name, *descriptor.constraints.default_value}}))
                << spec().inventory_id << " rejected descriptor default for '" << descriptor.name << "'";

            const auto applied_parameters = parameterized_copy->get_parameters();
            auto it = applied_parameters.find(descriptor.name);
            ASSERT_NE(it, applied_parameters.end()) << spec().inventory_id << " did not expose applied default for '" << descriptor.name << "'";
            EXPECT_EQ(it->second, *descriptor.constraints.default_value)
                << spec().inventory_id << " default mismatch for '" << descriptor.name << "'";
        }
    }

    TEST_P(PublicStageContractTest, observationDeclarationsAreUniqueAndSchemaCompatible)
    {
        auto stage = spec().create();
        const auto required = stage->get_required_observations();
        const auto provided = stage->get_provided_observations();

        std::set<std::string> required_keys;
        for (const auto& key : required) {
            ASSERT_FALSE(key.namespace_.empty());
            ASSERT_FALSE(key.name.empty());
            EXPECT_TRUE(required_keys.insert(key.full_key()).second)
                << spec().inventory_id << " duplicated required observation '" << key.full_key() << "'";
        }

        std::set<std::string> provided_keys;
        for (const auto& key : provided) {
            ASSERT_FALSE(key.namespace_.empty());
            ASSERT_FALSE(key.name.empty());
            EXPECT_TRUE(provided_keys.insert(key.full_key()).second)
                << spec().inventory_id << " duplicated provided observation '" << key.full_key() << "'";
        }

        if (provided.empty()) {
            GTEST_SKIP() << "Stage does not declare provided observations";
        }

        orc::ObservationContext observation_context;
        observation_context.register_schema(provided);

        for (size_t index = 0; index < provided.size(); ++index) {
            const auto& key = provided[index];
            EXPECT_NO_THROW(observation_context.set(
                orc::FieldID(static_cast<int32_t>(index)),
                key.namespace_,
                key.name,
                value_for_type(key.type)));

            if (key.type != orc::ObservationType::CUSTOM) {
                EXPECT_THROW(observation_context.set(
                    orc::FieldID(static_cast<int32_t>(index)),
                    key.namespace_,
                    key.name,
                    mismatched_value_for_type(key.type)), std::invalid_argument);
            }
        }
    }

    TEST_P(PublicStageContractTest, triggerLifecycleIsCoherentOnImmediateInvalidInvocation)
    {
        auto stage = spec().create();
        auto* triggerable = dynamic_cast<orc::TriggerableStage*>(stage.get());
        if (!triggerable) {
            GTEST_SKIP() << "Stage is not triggerable";
        }

        MockObservationContext observation_context;
        EXPECT_FALSE(triggerable->is_trigger_in_progress());
        EXPECT_NO_THROW(triggerable->cancel_trigger());

        bool returned = false;
        bool completed_without_throw = false;

        try {
            returned = triggerable->trigger({}, {}, observation_context);
            completed_without_throw = true;
        } catch (...) {
        }

        const bool was_in_progress = triggerable->is_trigger_in_progress();
        EXPECT_FALSE(was_in_progress);
        if (completed_without_throw) {
            EXPECT_FALSE(returned) << spec().inventory_id << " unexpectedly accepted an invalid trigger invocation";
        }
    }

    INSTANTIATE_TEST_SUITE_P(
        PublicStages,
        PublicStageContractTest,
        testing::Range(size_t{0}, public_stage_specs().size()),
        stage_param_name);

    // ── Format-specific default parameter parity ──────────────────────────────
    //
    // For registry-backed parameterised stages, the set of parameter names
    // returned by get_parameters() must stay consistent across all supported
    // video formats so that project_to_dag() can always seed every parameter
    // from the format-aware descriptor defaults.

    class FormatSpecificDefaultsTest : public testing::TestWithParam<size_t>
    {
    protected:
        const PublicStageSpec& spec() const { return stage_spec_at(GetParam()); }
    };

    TEST_P(FormatSpecificDefaultsTest, palFormatDefaultsMatchRuntimeBehaviour)
    {
        if (!spec().registry_backed) {
            GTEST_SKIP() << "Non-registry-backed base class skipped";
        }

        auto stage = spec().create();
        auto* p = dynamic_cast<orc::ParameterizedStage*>(stage.get());
        if (!p) {
            GTEST_SKIP() << "Stage is not parameterized";
        }

        const auto descriptors =
            p->get_parameter_descriptors(orc::VideoSystem::PAL, orc::SourceType::Composite);

        for (const auto& desc : descriptors) {
            if (!desc.constraints.default_value.has_value()) {
                continue;
            }

            // Apply the descriptor default and verify get_parameters() reflects it.
            auto fresh = spec().create();
            auto* fp = dynamic_cast<orc::ParameterizedStage*>(fresh.get());
            ASSERT_NE(fp, nullptr);
            ASSERT_TRUE(fp->set_parameters({{desc.name, *desc.constraints.default_value}}))
                << spec().inventory_id << " rejected PAL descriptor default for '" << desc.name << "'";

            auto applied = fp->get_parameters();
            auto it = applied.find(desc.name);
            ASSERT_NE(it, applied.end())
                << spec().inventory_id << " did not expose applied PAL default for '" << desc.name << "'";
            EXPECT_EQ(it->second, *desc.constraints.default_value)
                << spec().inventory_id << " PAL default mismatch for '" << desc.name << "'";
        }
    }

    TEST_P(FormatSpecificDefaultsTest, ntscFormatDefaultsMatchRuntimeBehaviour)
    {
        if (!spec().registry_backed) {
            GTEST_SKIP() << "Non-registry-backed base class skipped";
        }

        auto stage = spec().create();
        auto* p = dynamic_cast<orc::ParameterizedStage*>(stage.get());
        if (!p) {
            GTEST_SKIP() << "Stage is not parameterized";
        }

        const auto descriptors =
            p->get_parameter_descriptors(orc::VideoSystem::NTSC, orc::SourceType::Composite);

        for (const auto& desc : descriptors) {
            if (!desc.constraints.default_value.has_value()) {
                continue;
            }

            auto fresh = spec().create();
            auto* fp = dynamic_cast<orc::ParameterizedStage*>(fresh.get());
            ASSERT_NE(fp, nullptr);
            ASSERT_TRUE(fp->set_parameters({{desc.name, *desc.constraints.default_value}}))
                << spec().inventory_id << " rejected NTSC descriptor default for '" << desc.name << "'";

            auto applied = fp->get_parameters();
            auto it = applied.find(desc.name);
            ASSERT_NE(it, applied.end())
                << spec().inventory_id << " did not expose applied NTSC default for '" << desc.name << "'";
            EXPECT_EQ(it->second, *desc.constraints.default_value)
                << spec().inventory_id << " NTSC default mismatch for '" << desc.name << "'";
        }
    }

    INSTANTIATE_TEST_SUITE_P(
        PublicStages,
        FormatSpecificDefaultsTest,
        testing::Range(size_t{0}, public_stage_specs().size()),
        stage_param_name);
} // namespace orc_unit_test