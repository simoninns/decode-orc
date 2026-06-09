/*
 * File:        project_to_dag_contract_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Phase 5 contracts for project-to-DAG wiring
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <optional>
#include <string>

#include "../../../orc/core/include/project.h"
#include "../../../orc/core/include/project_to_dag.h"
#include "../include/public_stage_inventory.h"

namespace orc_unit_test {
namespace {
struct StageChain {
  std::string source;
  std::string middle;
  std::string sink;
};

std::optional<StageChain> find_representative_chain() {
  std::vector<std::string> source_names;
  std::vector<std::string> middle_names;
  std::vector<std::string> sink_names;

  for (const auto& spec : public_stage_specs()) {
    if (!spec.registry_backed) {
      continue;
    }
    const auto info = spec.create()->get_node_type_info();

    if (spec.family == PublicStageFamily::Source) {
      source_names.push_back(info.stage_name);
    } else if (spec.family == PublicStageFamily::Transform) {
      middle_names.push_back(info.stage_name);
    } else {
      sink_names.push_back(info.stage_name);
    }
  }

  for (const auto& source : source_names) {
    for (const auto& middle : middle_names) {
      if (!orc::is_connection_valid(source, middle)) {
        continue;
      }

      for (const auto& sink : sink_names) {
        if (orc::is_connection_valid(middle, sink)) {
          return StageChain{source, middle, sink};
        }
      }
    }
  }

  return std::nullopt;
}

std::string first_source_stage_name() {
  for (const auto& spec : public_stage_specs()) {
    if (spec.registry_backed && spec.family == PublicStageFamily::Source) {
      return spec.create()->get_node_type_info().stage_name;
    }
  }

  return {};
}
}  // namespace

TEST(ProjectToDagContractTest, convertsRepresentativePublicPipeline) {
  const auto chain = find_representative_chain();
  if (!chain.has_value()) {
    FAIL() << "Expected chain to have a value";
    return;
  }

  auto project = orc::project_io::create_empty_project(
      "phase5-contract", orc::VideoSystem::Unknown, orc::SourceType::Unknown);
  const auto source_id =
      orc::project_io::add_node(project, chain->source, 0.0, 0.0);
  const auto middle_id =
      orc::project_io::add_node(project, chain->middle, 100.0, 0.0);
  const auto sink_id =
      orc::project_io::add_node(project, chain->sink, 200.0, 0.0);

  orc::project_io::add_edge(project, source_id, middle_id);
  orc::project_io::add_edge(project, middle_id, sink_id);

  const auto dag = orc::project_to_dag(project);

  ASSERT_NE(dag, nullptr);
  EXPECT_TRUE(dag->validate());
  EXPECT_EQ(dag->nodes().size(), 3u);
  ASSERT_EQ(dag->output_nodes().size(), 1u);
  EXPECT_EQ(dag->output_nodes().front(), sink_id);
}

TEST(ProjectToDagContractTest, placeholderSourcesPassSourceValidation) {
  const auto source_stage_name = first_source_stage_name();
  ASSERT_FALSE(source_stage_name.empty());

  auto project = orc::project_io::create_empty_project(
      "phase5-placeholder", orc::VideoSystem::Unknown,
      orc::SourceType::Unknown);
  orc::project_io::add_node(project, source_stage_name, 0.0, 0.0);

  const auto dag = orc::project_to_dag(project);
  ASSERT_NE(dag, nullptr);
  EXPECT_NO_THROW(orc::validate_source_nodes(dag));
}

TEST(ProjectToDagContractTest,
     cvbsSourceParametersPersistWithoutTbcMetadataSidecar) {
  auto project = orc::project_io::create_empty_project(
      "phase5-cvbs-params", orc::VideoSystem::PAL, orc::SourceType::Composite);

  const auto source_id =
      orc::project_io::add_node(project, "PAL_CVBS_Source", 0.0, 0.0);

  std::map<std::string, orc::ParameterValue> params = {
      {"input_path", std::string("fixtures/test.composite")},
      {"use_metadata", false},
      {"sample_encoding", std::string("CVBS_TPG21_4FSC")}};

  EXPECT_NO_THROW(
      orc::project_io::set_node_parameters(project, source_id, params));

  const auto& nodes = project.get_nodes();
  const auto node_it = std::find_if(
      nodes.begin(), nodes.end(),
      [source_id](const auto& node) { return node.node_id == source_id; });

  ASSERT_NE(node_it, nodes.end());
  EXPECT_EQ(node_it->parameters, params);
}

TEST(ProjectToDagContractTest, unknownStageInProjectFailsCleanly) {
  auto project = orc::project_io::create_empty_project(
      "phase5-invalid", orc::VideoSystem::Unknown, orc::SourceType::Unknown);

  std::vector<orc::ProjectDAGNode> nodes = {{orc::NodeID(1),
                                             "phase5_missing_stage",
                                             orc::NodeType::TRANSFORM,
                                             "Missing",
                                             "Missing",
                                             0.0,
                                             0.0,
                                             {}}};

  orc::project_io::update_project_dag(project, nodes, {});

  EXPECT_THROW(orc::project_to_dag(project), orc::ProjectConversionError);
}

// ── Format-aware default parameter tests ─────────────────────────────────
//
// These tests guard against a class of bug where a project file does not
// store every parameter explicitly (e.g. a PAL project saved before
// decoder_type was persisted), causing project_to_dag() to leave the stage
// at its constructor default ("ntsc2d") rather than the correct format
// default ("pal2d").  The fix in project_to_dag.cpp seeds the parameter map
// from get_parameter_descriptors(video_format, source_type) *before*
// overlaying whatever is actually in the project file.

namespace {
// Extract the runtime decoder_type from the stage instance wired by
// project_to_dag.
std::string decoder_type_from_dag(const orc::DAG& dag, orc::NodeID node_id) {
  for (const auto& node : dag.nodes()) {
    if (node.node_id != node_id) {
      continue;
    }
    auto* p = dynamic_cast<orc::ParameterizedStage*>(node.stage.get());
    if (!p) {
      return "";
    }
    auto params = p->get_parameters();
    auto it = params.find("decoder_type");
    if (it == params.end()) {
      return "";
    }
    return std::holds_alternative<std::string>(it->second)
               ? std::get<std::string>(it->second)
               : "";
  }
  return "";
}
}  // namespace

TEST(ProjectToDagFormatDefaultsTest,
     palProjectGivesFFmpegSinkPalDecodeDefault) {
  // Simulate a PAL project that has an ffmpeg_video_sink node with NO
  // stored decoder_type (as would be the case for any project created
  // before the parameter was explicitly persisted).
  auto project = orc::project_io::create_empty_project(
      "pal-defaults", orc::VideoSystem::PAL, orc::SourceType::Composite);

  const auto sink_id =
      orc::project_io::add_node(project, "ffmpeg_video_sink", 0.0, 0.0);
  // Deliberately set NO parameters on the node.

  const auto dag = orc::project_to_dag(project);
  ASSERT_NE(dag, nullptr);

  EXPECT_EQ(decoder_type_from_dag(*dag, sink_id), "pal2d")
      << "PAL project with no stored decoder_type should default to 'pal2d', "
         "not the constructor default 'ntsc2d'";
}

TEST(ProjectToDagFormatDefaultsTest,
     ntscProjectGivesFFmpegSinkNtscDecodeDefault) {
  auto project = orc::project_io::create_empty_project(
      "ntsc-defaults", orc::VideoSystem::NTSC, orc::SourceType::Composite);

  const auto sink_id =
      orc::project_io::add_node(project, "ffmpeg_video_sink", 0.0, 0.0);

  const auto dag = orc::project_to_dag(project);
  ASSERT_NE(dag, nullptr);

  EXPECT_EQ(decoder_type_from_dag(*dag, sink_id), "ntsc2d")
      << "NTSC project with no stored decoder_type should default to 'ntsc2d'";
}

TEST(ProjectToDagFormatDefaultsTest,
     palProjectGivesRawVideoSinkPalDecodeDefault) {
  auto project = orc::project_io::create_empty_project(
      "pal-raw-defaults", orc::VideoSystem::PAL, orc::SourceType::Composite);

  const auto sink_id =
      orc::project_io::add_node(project, "raw_video_sink", 0.0, 0.0);

  const auto dag = orc::project_to_dag(project);
  ASSERT_NE(dag, nullptr);

  EXPECT_EQ(decoder_type_from_dag(*dag, sink_id), "pal2d")
      << "PAL project with no stored decoder_type should default to 'pal2d' "
         "for raw_video_sink";
}

TEST(ProjectToDagFormatDefaultsTest, storedDecoderTypeOverridesFormatDefault) {
  // If an explicit decoder_type IS stored (e.g. user changed it to "mono"),
  // it must win over the format-derived default.
  auto project = orc::project_io::create_empty_project(
      "pal-explicit", orc::VideoSystem::PAL, orc::SourceType::Composite);

  const auto sink_id =
      orc::project_io::add_node(project, "ffmpeg_video_sink", 0.0, 0.0);

  std::map<std::string, orc::ParameterValue> stored;
  stored["decoder_type"] = std::string("mono");
  orc::project_io::set_node_parameters(project, sink_id, stored);

  const auto dag = orc::project_to_dag(project);
  ASSERT_NE(dag, nullptr);

  EXPECT_EQ(decoder_type_from_dag(*dag, sink_id), "mono")
      << "Explicitly stored decoder_type='mono' must override the PAL format "
         "default";
}

TEST(ProjectToDagFormatDefaultsTest,
     allRegistryStagesHaveDefaultsForPALFormat) {
  // Every parameter descriptor returned for VideoSystem::PAL must carry a
  // default_value so that project_to_dag() can seed any parameter that is
  // absent from the project file.  Parameters that only appear in NTSC
  // descriptors (e.g. ntsc_phase_comp) are invisible here and are not
  // checked — format-filtering is intentional.
  for (const auto& spec : public_stage_specs()) {
    if (!spec.registry_backed) {
      continue;
    }

    auto stage = spec.create();
    auto* p = dynamic_cast<orc::ParameterizedStage*>(stage.get());
    if (!p) {
      continue;
    }

    const auto descriptors = p->get_parameter_descriptors(
        orc::VideoSystem::PAL, orc::SourceType::Composite);

    for (const auto& desc : descriptors) {
      EXPECT_TRUE(desc.constraints.default_value.has_value())
          << spec.inventory_id << ": PAL descriptor for '" << desc.name
          << "' has no default_value — project_to_dag cannot seed this "
             "parameter "
             "when it is absent from the project file";
    }
  }
}

TEST(ProjectToDagFormatDefaultsTest,
     allRegistryStagesHaveDefaultsForNTSCFormat) {
  // Same check for VideoSystem::NTSC.
  for (const auto& spec : public_stage_specs()) {
    if (!spec.registry_backed) {
      continue;
    }

    auto stage = spec.create();
    auto* p = dynamic_cast<orc::ParameterizedStage*>(stage.get());
    if (!p) {
      continue;
    }

    const auto descriptors = p->get_parameter_descriptors(
        orc::VideoSystem::NTSC, orc::SourceType::Composite);

    for (const auto& desc : descriptors) {
      EXPECT_TRUE(desc.constraints.default_value.has_value())
          << spec.inventory_id << ": NTSC descriptor for '" << desc.name
          << "' has no default_value — project_to_dag cannot seed this "
             "parameter "
             "when it is absent from the project file";
    }
  }
}
}  // namespace orc_unit_test