/*
 * File:        project_plugin_metadata_test.cpp
 * Module:      orc-core unit tests
 * Purpose:     Unit tests for per-project plugin metadata persistence and
 * guidance
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "../../../orc/core/include/project.h"
#include "../../../orc/core/include/project_to_dag.h"

namespace orc_unit_test {
namespace {

using testing::ElementsAre;
using testing::HasSubstr;

}  // namespace

TEST(ProjectPluginMetadataTest,
     Save_FiltersLoadedPluginMetadataToStillUsedStages) {
  const std::string yaml_text = R"yaml(
project:
  name: plugin-metadata-test
  version: "2.0"
  video_format: NTSC
  source_format: Composite
  amplitude_unit: IRE
dag:
  nodes:
    - id: 1
      stage: kept-stage
      node_type: TRANSFORM
      x: 0
      y: 0
    - id: 2
      stage: removed-stage
      node_type: TRANSFORM
      x: 100
      y: 0
  edges: []
required_plugins:
  - plugin_id: example.kept
    source_repo_url: https://example.invalid/kept
    stage_names:
      - kept-stage
  - plugin_id: example.removed
    source_repo_url: https://example.invalid/removed
    stage_names:
      - removed-stage
)yaml";

  auto project = orc::project_io::load_project_from_yaml(
      yaml_text, "/tmp/plugin-metadata.orcprj");

  std::vector<orc::ProjectDAGNode> nodes;
  nodes.push_back(project.get_nodes().front());
  orc::project_io::update_project_dag(project, nodes, {});

  const auto serialized = orc::project_io::serialize_project_to_yaml(
      project, "/tmp/plugin-metadata.orcprj");
  const auto reparsed = orc::project_io::load_project_from_yaml(
      serialized, "/tmp/plugin-metadata.orcprj");

  ASSERT_EQ(reparsed.get_required_plugins().size(), 1u);
  EXPECT_EQ(reparsed.get_required_plugins().front().plugin_id, "example.kept");
  EXPECT_THAT(reparsed.get_required_plugins().front().stage_names,
              ElementsAre("kept-stage"));
}

TEST(ProjectPluginMetadataTest,
     Missing_StageErrorReferencesSavedPluginMetadata) {
  const std::string yaml_text = R"yaml(
project:
  name: plugin-metadata-test
  version: "2.0"
  video_format: NTSC
  source_format: Composite
  amplitude_unit: IRE
dag:
  nodes:
    - id: 3
      stage: missing-third-party-stage
      node_type: TRANSFORM
      x: 0
      y: 0
  edges: []
required_plugins:
  - plugin_id: example.missing
    source_repo_url: https://example.invalid/orc-plugin_missing
    stage_names:
      - missing-third-party-stage
)yaml";

  const auto project = orc::project_io::load_project_from_yaml(
      yaml_text, "/tmp/missing-stage.orcprj");

  try {
    (void)orc::project_to_dag(project);
    FAIL() << "Expected ProjectConversionError";
  } catch (const orc::ProjectConversionError& error) {
    EXPECT_THAT(std::string(error.what()),
                HasSubstr("Required plugin: example.missing"));
    EXPECT_THAT(std::string(error.what()),
                HasSubstr("https://example.invalid/orc-plugin_missing"));
  }
}

}  // namespace orc_unit_test
