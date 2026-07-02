/*
 * File:        stage_plugin_isolation_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Verify RTLD_LOCAL cross-boundary RTTI and keep-alive plugin
 *              library lifetime against real dlopen'd plugin binaries.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 *
 * IMPORTANT: unlike the other functional contract tests, this executable
 * must NOT link any stage plugin target. Linking a plugin would map its
 * library (and unify its symbols/typeinfo) before the loader runs, which
 * would make these isolation checks pass trivially.
 */

#include <gtest/gtest.h>
#include <orc/plugin/orc_stage_tooling.h>

#include <filesystem>
#include <string>
#include <utility>

#include "../../../orc/core/include/stage_parameter.h"
#include "../../../orc/core/include/stage_plugin_loader.h"
#include "../../../orc/core/include/stage_preview_capability.h"
#include "../../../orc/core/stages/triggerable_stage.h"

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

// ---------------------------------------------------------------------------
// Cross-boundary RTTI under RTLD_LOCAL
// ---------------------------------------------------------------------------
// Plugins are loaded with RTLD_LOCAL, so mixin typeinfo is not unified via
// the dynamic linker; libstdc++ falls back to typeinfo-name comparison.
// These casts must keep working for the host's capability discovery.

TEST(StagePluginIsolationTest, MixinDynamicCasts_WorkAcrossPluginBoundary) {
  auto plugin_path = plugin_library_path("dropout-analysis-sink");
  ASSERT_TRUE(std::filesystem::exists(plugin_path)) << plugin_path.string();

  orc::StagePluginLoader loader;
  orc::DAGStagePtr stage;

  const auto result = loader.load_plugin(
      plugin_path.string(),
      [&stage](const std::string&,
               orc::StagePluginLoader::RegisterStageFactory factory) {
        if (!stage) {
          stage = factory();
        }
        return stage != nullptr;
      });

  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_NE(stage, nullptr);

  // Mixins implemented by DropoutAnalysisSinkStage must all be reachable.
  EXPECT_NE(dynamic_cast<orc::ParameterizedStage*>(stage.get()), nullptr);
  EXPECT_NE(dynamic_cast<orc::TriggerableStage*>(stage.get()), nullptr);
  EXPECT_NE(dynamic_cast<orc::IStagePreviewCapability*>(stage.get()), nullptr);
  EXPECT_NE(dynamic_cast<orc::StageToolProvider*>(stage.get()), nullptr);

  // No in-tree stage implements AnalysisToolProvider; the negative lookup
  // must still resolve cleanly (no fault) across the plugin boundary.
  EXPECT_EQ(dynamic_cast<orc::AnalysisToolProvider*>(stage.get()), nullptr);
}

// ---------------------------------------------------------------------------
// Stage instances outlive unload_all()
// ---------------------------------------------------------------------------
// Factories and stage instances hold a keep-alive reference to the plugin
// library; unload_all() only drops the loader's reference, so a live stage
// must remain fully usable (vtable intact) afterwards.

TEST(StagePluginIsolationTest, StageInstance_RemainsUsableAfterUnloadAll) {
  auto plugin_path = plugin_library_path("mask-line");
  ASSERT_TRUE(std::filesystem::exists(plugin_path)) << plugin_path.string();

  orc::StagePluginLoader loader;
  orc::StagePluginLoader::RegisterStageFactory registered_factory;

  const auto result = loader.load_plugin(
      plugin_path.string(),
      [&registered_factory](
          const std::string&,
          orc::StagePluginLoader::RegisterStageFactory factory) {
        registered_factory = std::move(factory);
        return true;
      });

  ASSERT_TRUE(result.success) << result.error_message;
  ASSERT_TRUE(registered_factory);

  orc::DAGStagePtr stage = registered_factory();
  ASSERT_NE(stage, nullptr);

  loader.unload_all();
  EXPECT_TRUE(loader.loaded_plugins().empty());

  // Virtual dispatch into plugin code after the loader dropped its handle.
  EXPECT_FALSE(stage->version().empty());
  const auto info = stage->get_node_type_info();
  EXPECT_EQ(info.stage_name, "mask_line");
  auto* parameterized = dynamic_cast<orc::ParameterizedStage*>(stage.get());
  ASSERT_NE(parameterized, nullptr);
  EXPECT_FALSE(parameterized->get_parameter_descriptors().empty());

  // Release order: factory first, then the last stage reference — the
  // library must stay mapped until this final reset (which may dlclose it).
  registered_factory = nullptr;
  stage.reset();
}

// A second load of the same plugin after a full release must succeed —
// guards against keep-alive tokens leaking a stale library reference.
TEST(StagePluginIsolationTest, Plugin_CanBeReloadedAfterFullRelease) {
  auto plugin_path = plugin_library_path("mask-line");
  ASSERT_TRUE(std::filesystem::exists(plugin_path)) << plugin_path.string();

  for (int round = 0; round < 2; ++round) {
    orc::StagePluginLoader loader;
    orc::DAGStagePtr stage;
    const auto result = loader.load_plugin(
        plugin_path.string(),
        [&stage](const std::string&,
                 orc::StagePluginLoader::RegisterStageFactory factory) {
          stage = factory();
          return stage != nullptr;
        });
    ASSERT_TRUE(result.success)
        << "round " << round << ": " << result.error_message;
    ASSERT_NE(stage, nullptr);
    EXPECT_EQ(stage->get_node_type_info().stage_name, "mask_line");
  }
}

}  // namespace orc_unit_test
