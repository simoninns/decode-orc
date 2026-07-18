/*
 * File:        project_presenter_validation_test.cpp
 * Module:      orc-presenters unit tests
 * Purpose:     Validation logic for ProjectPresenter source/sink detection
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../../../../orc/core/include/project.h"
#include "../../../../orc/presenters/include/project_presenter.h"

namespace orc_unit_test {
namespace {

// Builds an in-memory v2.0 project (no edges, so no registry lookups are
// triggered during load) from a list of nodes described as
// "stage_name / node_type" pairs. The stage names are deliberately neutral so
// the source-format consistency checks in the loader never fire.
struct NodeSpec {
  int id;
  std::string stage;
  std::string node_type;
};

orc::Project make_project(const std::vector<NodeSpec>& nodes) {
  std::string yaml =
      "project:\n"
      "  name: validation-test\n"
      "  version: \"2.0\"\n"
      "  video_format: PAL\n"
      "  source_format: Composite\n"
      "  amplitude_unit: mV\n"
      "dag:\n";
  if (nodes.empty()) {
    yaml += "  nodes: []\n";
  } else {
    yaml += "  nodes:\n";
    for (const auto& n : nodes) {
      yaml += "    - id: " + std::to_string(n.id) + "\n";
      yaml += "      stage: " + n.stage + "\n";
      yaml += "      node_type: " + n.node_type + "\n";
    }
  }
  yaml += "  edges: []\n";
  return orc::project_io::load_project_from_yaml(yaml, "/virtual/test.orcprj");
}

orc::presenters::ProjectPresenter wrap(orc::Project& project) {
  return orc::presenters::ProjectPresenter(static_cast<void*>(&project));
}

}  // namespace

TEST(ProjectPresenterValidationTest, EmptyProject_FailsValidation) {
  auto project = make_project({});
  auto presenter = wrap(project);

  EXPECT_FALSE(presenter.validateProject());
  EXPECT_EQ(presenter.getValidationErrors(),
            std::vector<std::string>{"Project has no nodes"});
}

TEST(ProjectPresenterValidationTest, SourceOnly_FailsValidation) {
  auto project = make_project({{1, "tbc_source", "SOURCE"}});
  auto presenter = wrap(project);

  EXPECT_FALSE(presenter.validateProject());
  EXPECT_EQ(presenter.getValidationErrors(),
            std::vector<std::string>{"Project has no sink nodes"});
}

TEST(ProjectPresenterValidationTest, SinkOnly_ReportsNoSourceNodes) {
  auto project = make_project({{1, "video_sink", "SINK"}});
  auto presenter = wrap(project);

  EXPECT_FALSE(presenter.validateProject());
  EXPECT_EQ(presenter.getValidationErrors(),
            std::vector<std::string>{"Project has no source nodes"});
}

TEST(ProjectPresenterValidationTest, SourceAndSink_PassesValidation) {
  auto project =
      make_project({{1, "tbc_source", "SOURCE"}, {2, "video_sink", "SINK"}});
  auto presenter = wrap(project);

  EXPECT_TRUE(presenter.validateProject());
  EXPECT_TRUE(presenter.getValidationErrors().empty());
}

TEST(ProjectPresenterValidationTest, TransformBetweenSourceAndSink_Passes) {
  auto project = make_project({{1, "tbc_source", "SOURCE"},
                               {2, "dropout_correct", "TRANSFORM"},
                               {3, "video_sink", "SINK"}});
  auto presenter = wrap(project);

  EXPECT_TRUE(presenter.validateProject());
  EXPECT_TRUE(presenter.getValidationErrors().empty());
}

TEST(ProjectPresenterValidationTest, AnalysisSinkCountsAsSink) {
  auto project = make_project(
      {{1, "tbc_source", "SOURCE"}, {2, "dropout_analysis", "ANALYSIS_SINK"}});
  auto presenter = wrap(project);

  EXPECT_TRUE(presenter.validateProject());
  EXPECT_TRUE(presenter.getValidationErrors().empty());
}

TEST(ProjectPresenterValidationTest, TransformOnly_ReportsBothMissing) {
  auto project = make_project({{1, "dropout_correct", "TRANSFORM"}});
  auto presenter = wrap(project);

  EXPECT_FALSE(presenter.validateProject());
  const std::vector<std::string> expected{"Project has no source nodes",
                                          "Project has no sink nodes"};
  EXPECT_EQ(presenter.getValidationErrors(), expected);
}

}  // namespace orc_unit_test
