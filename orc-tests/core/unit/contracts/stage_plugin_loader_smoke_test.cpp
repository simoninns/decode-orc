/*
 * File:        stage_plugin_loader_smoke_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Runtime smoke coverage for plugin loading through
 * StagePluginLoader
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include "../../../orc/core/include/stage_plugin_loader.h"

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

TEST(StagePluginLoaderSmokeTest, loadsRawVideoSinkPluginFromBuiltLibrary) {
  auto plugin_path = plugin_library_path("raw-video-sink");
  ASSERT_FALSE(plugin_path.empty());
  ASSERT_TRUE(std::filesystem::exists(plugin_path)) << plugin_path.string();

  orc::StagePluginLoader loader;
  std::vector<std::string> loaded_stage_names;

  const auto result = loader.load_plugin(
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
        EXPECT_EQ(info.stage_name, stage_name);
        return true;
      });

  ASSERT_TRUE(result.success) << result.error_message;
  if (!result.plugin.has_value()) {
    FAIL() << "Expected plugin to have a value";
    return;
  }
  EXPECT_FALSE(result.plugin->plugin_id.empty());
  EXPECT_FALSE(result.plugin->plugin_version.empty());
  EXPECT_FALSE(loaded_stage_names.empty());
  EXPECT_EQ(loaded_stage_names.size(),
            result.plugin->registered_stage_names.size());
  EXPECT_EQ(loaded_stage_names.front(), "raw_video_sink");
  EXPECT_EQ(result.plugin->registered_stage_names.front(), "raw_video_sink");
}

}  // namespace orc_unit_test