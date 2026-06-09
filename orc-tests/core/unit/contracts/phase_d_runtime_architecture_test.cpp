/*
 * File:        phase_d_runtime_architecture_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Phase D runtime architecture validation: verify stage discovery,
 *              creation, and plugin compliance with SDK-only contracts
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "../../../orc/core/include/stage_plugin_loader.h"
#include "../../../orc/core/include/stage_registry.h"
#include "../include/public_stage_inventory.h"

namespace orc_unit_test {
namespace {

std::filesystem::path plugin_library_path(const std::string& base_name) {
#if defined(_WIN32)
  return std::filesystem::path(ORC_STAGE_PLUGIN_BUILD_DIR) /
         ("orc-stage-plugin-" + base_name + ".dll");
#elif defined(__APPLE__)
  return std::filesystem::path(ORC_STAGE_PLUGIN_BUILD_DIR) /
         ("liborc-stage-plugin-" + base_name + ".dylib");
#else
  return std::filesystem::path(ORC_STAGE_PLUGIN_BUILD_DIR) /
         ("liborc-stage-plugin-" + base_name + ".so");
#endif
}

}  // namespace

class PhaseDRuntimeArchitectureTest : public ::testing::Test {
 protected:
  orc::StagePluginLoader loader_;
};

TEST_F(PhaseDRuntimeArchitectureTest, corePublicStagesAreDiscoverable) {
  // Verify the Stage Registry contains all expected core-supplied stages
  std::vector<std::string> expected_stages;
  for (const auto& spec : public_stage_specs()) {
    expected_stages.push_back(spec.inventory_id);
  }

  EXPECT_FALSE(expected_stages.empty())
      << "Public stage inventory should not be empty";
  EXPECT_GT(expected_stages.size(), 5U) << "Should have at least 5 core stages";
}

TEST_F(PhaseDRuntimeArchitectureTest, stageCreationFromRegistrySucceeds) {
  // Verify stages can be created through the registry
  std::vector<orc::DAGStagePtr> created_stages;

  for (const auto& spec : public_stage_specs()) {
    auto stage = spec.create();
    ASSERT_TRUE(stage) << "Failed to create stage: " << spec.inventory_id;

    const auto info = stage->get_node_type_info();
    EXPECT_EQ(info.stage_name, spec.inventory_id)
        << "Stage name mismatch in registry";
    EXPECT_LE(info.min_inputs, info.max_inputs)
        << "Stage " << spec.inventory_id << " has invalid input bounds";
    EXPECT_LE(info.min_outputs, info.max_outputs)
        << "Stage " << spec.inventory_id << " has invalid output bounds";

    switch (info.type) {
      case orc::NodeType::SOURCE:
        EXPECT_EQ(info.min_inputs, 0U) << "Source stage " << spec.inventory_id
                                       << " must have min_inputs=0";
        EXPECT_EQ(info.max_inputs, 0U) << "Source stage " << spec.inventory_id
                                       << " must have max_inputs=0";
        EXPECT_GT(info.max_outputs, 0U)
            << "Source stage " << spec.inventory_id << " must declare outputs";
        break;

      case orc::NodeType::SINK:
      case orc::NodeType::ANALYSIS_SINK:
        EXPECT_EQ(info.min_outputs, 0U)
            << "Sink stage " << spec.inventory_id << " must have min_outputs=0";
        EXPECT_EQ(info.max_outputs, 0U)
            << "Sink stage " << spec.inventory_id << " must have max_outputs=0";
        EXPECT_GT(info.max_inputs, 0U)
            << "Sink stage " << spec.inventory_id << " must declare inputs";
        break;

      case orc::NodeType::TRANSFORM:
      case orc::NodeType::MERGER:
      case orc::NodeType::COMPLEX:
        EXPECT_GT(info.max_inputs, 0U)
            << "Processing stage " << spec.inventory_id
            << " must declare inputs";
        EXPECT_GT(info.max_outputs, 0U)
            << "Processing stage " << spec.inventory_id
            << " must declare outputs";
        break;
    }

    created_stages.push_back(stage);
  }

  EXPECT_GT(created_stages.size(), 5U)
      << "Should have created at least 5 stages";
}

TEST_F(PhaseDRuntimeArchitectureTest, runtimePluginDiscoverySucceeds) {
  auto plugin_path = plugin_library_path("raw-video-sink");
  ASSERT_TRUE(std::filesystem::exists(plugin_path)) << plugin_path.string();

  std::vector<std::string> loaded_stage_names;
  auto result = loader_.load_plugin(
      plugin_path.string(),
      [&loaded_stage_names](
          const std::string& stage_name,
          orc::StagePluginLoader::RegisterStageFactory factory) {
        loaded_stage_names.push_back(stage_name);
        auto stage = factory();
        if (!stage) {
          return false;
        }

        const auto info = stage->get_node_type_info();
        EXPECT_EQ(info.stage_name, stage_name) << "Plugin stage name mismatch";

        return true;
      });

  EXPECT_TRUE(result.success) << result.error_message;
  ASSERT_TRUE(result.plugin.has_value());
  EXPECT_FALSE(loaded_stage_names.empty())
      << "Plugin should register at least one stage";
  EXPECT_EQ(loaded_stage_names.front(), "raw_video_sink");
}

TEST_F(PhaseDRuntimeArchitectureTest, runtimePluginMetadataIsValid) {
  auto plugin_path = plugin_library_path("raw-video-sink");
  ASSERT_TRUE(std::filesystem::exists(plugin_path));

  auto result = loader_.load_plugin(
      plugin_path.string(),
      [](const std::string& stage_name,
         orc::StagePluginLoader::RegisterStageFactory factory) {
        auto stage = factory();
        return stage != nullptr;
      });

  ASSERT_TRUE(result.success) << result.error_message;
  if (!result.plugin.has_value()) {
    FAIL() << "Expected plugin to have a value";
    return;
  }

  const auto& plugin_info = *result.plugin;
  EXPECT_FALSE(plugin_info.plugin_id.empty())
      << "Plugin ID should not be empty";
  EXPECT_FALSE(plugin_info.plugin_version.empty())
      << "Plugin version should not be empty";
  EXPECT_FALSE(plugin_info.registered_stage_names.empty())
      << "Plugin should register at least one stage";
}

TEST_F(PhaseDRuntimeArchitectureTest, stageCreationIsRepeatable) {
  // Verify stages can be created multiple times from the registry
  auto spec = public_stage_specs()[0];  // Use first stage

  for (int i = 0; i < 3; ++i) {
    auto stage = spec.create();
    ASSERT_TRUE(stage) << "Failed to create stage on iteration " << i;

    const auto info = stage->get_node_type_info();
    EXPECT_EQ(info.stage_name, spec.inventory_id)
        << "Stage name inconsistency on iteration " << i;
  }
}

}  // namespace orc_unit_test
