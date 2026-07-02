/*
 * File:        stage_plugin_keepalive_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Verify that stage instances keep their plugin library
 *              keep-alive token until the last instance is destroyed.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../../orc/core/include/stage_plugin_loader.h"

namespace orc_unit_test {
namespace {

// Minimal concrete DAGStage standing in for a plugin-provided stage. The
// factory below must be a plain function so it can be passed as the C
// function pointer type OrcStageFactoryFn.
class KeepaliveTestStage : public orc::DAGStage {
 public:
  std::string version() const override { return "keepalive-test"; }
  orc::NodeTypeInfo get_node_type_info() const override {
    return orc::NodeTypeInfo{orc::NodeType::TRANSFORM,
                             "keepalive_test",
                             "Keepalive Test",
                             "Test-only stage",
                             1,
                             1,
                             1,
                             1,
                             orc::VideoFormatCompatibility::ALL,
                             orc::SinkCategory::CORE,
                             "Test"};
  }
  std::vector<orc::ArtifactPtr> execute(
      const std::vector<orc::ArtifactPtr>& inputs,
      const std::map<std::string, orc::ParameterValue>& parameters,
      orc::ObservationContext& observation_context) override {
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    return {};
  }
  size_t required_input_count() const override { return 1; }
  size_t output_count() const override { return 1; }
};

orc::DAGStagePtr keepalive_test_factory() {
  return std::make_shared<KeepaliveTestStage>();
}

orc::DAGStagePtr null_stage_factory() { return nullptr; }

// Keep-alive token whose release is observable through a flag owned by the
// test body (the flag outlives the token because the token cannot outlive
// the wrapped factory and stages, all of which are test-scoped).
std::shared_ptr<void> make_tracked_token(bool* released) {
  return std::shared_ptr<void>(static_cast<void*>(released), [](void* flag) {
    *static_cast<bool*>(flag) = true;
  });
}

}  // namespace

TEST(StagePluginKeepaliveTest,
     Token_IsHeldByFactory_WhenLoaderReferenceIsReleased) {
  bool released = false;
  auto token = make_tracked_token(&released);

  auto wrapped = orc::core_internal::make_keepalive_stage_factory(
      &keepalive_test_factory, token);

  // Simulate StagePluginLoader::unload_all(): the loader's reference goes
  // away, but the registered factory still holds the token.
  token.reset();
  EXPECT_FALSE(released);

  wrapped = nullptr;
  EXPECT_TRUE(released);
}

TEST(StagePluginKeepaliveTest,
     Stage_RemainsUsableAndHoldsToken_AfterFactoryAndLoaderRelease) {
  bool released = false;
  auto token = make_tracked_token(&released);

  auto wrapped = orc::core_internal::make_keepalive_stage_factory(
      &keepalive_test_factory, token);

  orc::DAGStagePtr stage = wrapped();
  ASSERT_NE(stage, nullptr);

  // Simulate unload_all() plus StageRegistry::clear(): loader and factory
  // references are both gone while the stage instance is still alive.
  token.reset();
  wrapped = nullptr;
  EXPECT_FALSE(released);

  // The stage must remain fully callable (its code cannot be unmapped).
  EXPECT_EQ(stage->version(), "keepalive-test");
  EXPECT_EQ(stage->get_node_type_info().stage_name, "keepalive_test");

  stage.reset();
  EXPECT_TRUE(released);
}

TEST(StagePluginKeepaliveTest, Token_IsSharedAcrossMultipleStageInstances) {
  bool released = false;
  auto token = make_tracked_token(&released);

  auto wrapped = orc::core_internal::make_keepalive_stage_factory(
      &keepalive_test_factory, token);
  token.reset();

  orc::DAGStagePtr first = wrapped();
  orc::DAGStagePtr second = wrapped();
  ASSERT_NE(first, nullptr);
  ASSERT_NE(second, nullptr);

  wrapped = nullptr;
  first.reset();
  EXPECT_FALSE(released);

  second.reset();
  EXPECT_TRUE(released);
}

TEST(StagePluginKeepaliveTest, NullFactoryResult_DoesNotLeakToken) {
  bool released = false;
  auto token = make_tracked_token(&released);

  auto wrapped = orc::core_internal::make_keepalive_stage_factory(
      &null_stage_factory, token);
  token.reset();

  EXPECT_EQ(wrapped(), nullptr);
  EXPECT_FALSE(released);

  wrapped = nullptr;
  EXPECT_TRUE(released);
}

}  // namespace orc_unit_test
