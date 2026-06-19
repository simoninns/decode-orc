/*
 * File:        project_format_test.cpp
 * Module:      orc-core unit tests
 * Purpose:     Unit tests for project format version enforcement and
 *              source-stage/video-format consistency validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <stdexcept>
#include <string>

#include "../../../orc/core/include/project.h"

namespace orc_unit_test {
namespace {

// Minimal v2.0 YAML project skeleton with no nodes.
constexpr const char* kV2EmptyProject = R"yaml(
project:
  name: test-project
  version: "2.0"
  video_format: PAL
  source_format: Composite
  amplitude_unit: mV
dag:
  nodes: []
  edges: []
)yaml";

// Same skeleton with version "1.0".
constexpr const char* kV1Project = R"yaml(
project:
  name: legacy-project
  version: "1.0"
  video_format: PAL
  source_format: Composite
  amplitude_unit: mV
dag:
  nodes: []
  edges: []
)yaml";

// Project with no version field (defaults to "1.0" internally).
constexpr const char* kNoVersionProject = R"yaml(
project:
  name: no-version-project
  video_format: PAL
  source_format: Composite
  amplitude_unit: mV
dag:
  nodes: []
  edges: []
)yaml";

// Minimal v2.0 project with amplitude_unit set to 10-bit samples.
constexpr const char* kProject10BitUnit = R"yaml(
project:
  name: ten-bit-project
  version: "2.0"
  video_format: PAL
  source_format: Composite
  amplitude_unit: 10bit
dag:
  nodes: []
  edges: []
)yaml";

// Minimal v2.0 NTSC project (defaults to IRE when created via
// create_empty_project).
constexpr const char* kNtscProjectIRE = R"yaml(
project:
  name: ntsc-ire-project
  version: "2.0"
  video_format: NTSC
  source_format: Composite
  amplitude_unit: IRE
dag:
  nodes: []
  edges: []
)yaml";

}  // namespace

// ---------------------------------------------------------------------------
// v2.0 acceptance
// ---------------------------------------------------------------------------

TEST(ProjectFormatTest, V2_ProjectLoads_Successfully) {
  EXPECT_NO_THROW(orc::project_io::load_project_from_yaml(
      kV2EmptyProject, "/tmp/v2-test.orcprj"));
}

TEST(ProjectFormatTest, V2_Project_ReturnsCorrectVersion) {
  const auto project = orc::project_io::load_project_from_yaml(
      kV2EmptyProject, "/tmp/v2-test.orcprj");
  EXPECT_EQ(project.get_version(), "2.0");
}

// ---------------------------------------------------------------------------
// v1.x hard rejection
// ---------------------------------------------------------------------------

TEST(ProjectFormatTest, V1_ProjectLoad_ThrowsRuntimeError) {
  EXPECT_THROW(orc::project_io::load_project_from_yaml(kV1Project,
                                                       "/tmp/v1-test.orcprj"),
               std::runtime_error);
}

TEST(ProjectFormatTest, V1_ProjectLoad_ErrorMessage_ContainsVersion) {
  try {
    orc::project_io::load_project_from_yaml(kV1Project, "/tmp/v1-test.orcprj");
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    const std::string msg(e.what());
    EXPECT_NE(msg.find("1.0"), std::string::npos)
        << "Error message must quote the offending version: " << msg;
    EXPECT_NE(msg.find("2.x"), std::string::npos)
        << "Error message must mention 2.x requirement: " << msg;
  }
}

TEST(ProjectFormatTest,
     V1_ProjectLoad_ErrorMessage_ContainsActionableGuidance) {
  try {
    orc::project_io::load_project_from_yaml(kV1Project, "/tmp/v1-test.orcprj");
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    const std::string msg(e.what());
    EXPECT_NE(msg.find("Decode-Orc 2.0"), std::string::npos)
        << "Error message must reference Decode-Orc 2.0: " << msg;
    // The message must NOT reference a migration command that does not exist.
    EXPECT_EQ(msg.find("migrate-project"), std::string::npos)
        << "Error message must not reference migrate-project command: " << msg;
  }
}

TEST(ProjectFormatTest, NoVersion_ProjectLoad_ThrowsRuntimeError) {
  EXPECT_THROW(orc::project_io::load_project_from_yaml(
                   kNoVersionProject, "/tmp/no-version-test.orcprj"),
               std::runtime_error);
}

// ---------------------------------------------------------------------------
// create_empty_project produces v2.0 output
// ---------------------------------------------------------------------------

TEST(ProjectFormatTest, CreateEmptyProject_SetsVersion2) {
  const auto project = orc::project_io::create_empty_project(
      "new-project", orc::VideoSystem::PAL, orc::SourceType::Composite);
  EXPECT_EQ(project.get_version(), "2.0");
}

TEST(ProjectFormatTest, SerializeAndReload_PreservesV2Version) {
  const auto project = orc::project_io::create_empty_project(
      "round-trip", orc::VideoSystem::PAL, orc::SourceType::Composite);

  const std::string yaml =
      orc::project_io::serialize_project_to_yaml(project, "/tmp/rt.orcprj");

  const auto reloaded =
      orc::project_io::load_project_from_yaml(yaml, "/tmp/rt.orcprj");
  EXPECT_EQ(reloaded.get_version(), "2.0");
}

// ---------------------------------------------------------------------------
// video_format / source_format load-time source-stage validation
// ---------------------------------------------------------------------------

// A PAL project containing an NTSC-named source stage must be rejected.
TEST(ProjectFormatTest, VideoFormatMismatch_NtscStageInPalProject_Throws) {
  const std::string yaml = R"yaml(
project:
  name: mismatch-project
  version: "2.0"
  video_format: PAL
  source_format: Composite
  amplitude_unit: mV
dag:
  nodes:
    - id: 1
      stage: NTSC_Comp_Source
      node_type: SOURCE
      x: 0
      y: 0
  edges: []
)yaml";

  EXPECT_THROW(
      orc::project_io::load_project_from_yaml(yaml, "/tmp/mismatch.orcprj"),
      std::runtime_error);
}

// An NTSC project containing a PAL-named source stage must be rejected.
TEST(ProjectFormatTest, VideoFormatMismatch_PalStageInNtscProject_Throws) {
  const std::string yaml = R"yaml(
project:
  name: mismatch-ntsc-project
  version: "2.0"
  video_format: NTSC
  source_format: Composite
  amplitude_unit: IRE
dag:
  nodes:
    - id: 1
      stage: PAL_Comp_Source
      node_type: SOURCE
      x: 0
      y: 0
  edges: []
)yaml";

  EXPECT_THROW(orc::project_io::load_project_from_yaml(
                   yaml, "/tmp/mismatch-ntsc.orcprj"),
               std::runtime_error);
}

// The error message must identify the offending stage name.
TEST(ProjectFormatTest, VideoFormatMismatch_ErrorMessage_NamesOffendingStage) {
  const std::string yaml = R"yaml(
project:
  name: mismatch-msg-project
  version: "2.0"
  video_format: PAL
  source_format: Composite
  amplitude_unit: mV
dag:
  nodes:
    - id: 3
      stage: NTSC_YC_Source
      node_type: SOURCE
      x: 0
      y: 0
  edges: []
)yaml";

  try {
    orc::project_io::load_project_from_yaml(yaml, "/tmp/mismatch-msg.orcprj");
    FAIL() << "Expected std::runtime_error";
  } catch (const std::runtime_error& e) {
    const std::string msg(e.what());
    EXPECT_NE(msg.find("NTSC_YC_Source"), std::string::npos)
        << "Error message must name the offending stage: " << msg;
  }
}

// A Composite project containing a YC-named source stage must be rejected.
TEST(ProjectFormatTest, SourceFormatMismatch_YcStageInCompositeProject_Throws) {
  const std::string yaml = R"yaml(
project:
  name: yc-mismatch-project
  version: "2.0"
  video_format: PAL
  source_format: Composite
  amplitude_unit: mV
dag:
  nodes:
    - id: 1
      stage: PAL_YC_Source
      node_type: SOURCE
      x: 0
      y: 0
  edges: []
)yaml";

  EXPECT_THROW(
      orc::project_io::load_project_from_yaml(yaml, "/tmp/yc-mismatch.orcprj"),
      std::runtime_error);
}

// Non-source nodes (TRANSFORM, SINK) are not validated against format.
TEST(ProjectFormatTest, VideoFormatMismatch_TransformStage_NotValidated) {
  const std::string yaml = R"yaml(
project:
  name: transform-ok-project
  version: "2.0"
  video_format: PAL
  source_format: Composite
  amplitude_unit: mV
dag:
  nodes:
    - id: 1
      stage: NTSC_Transform
      node_type: TRANSFORM
      x: 0
      y: 0
  edges: []
)yaml";

  // TRANSFORM nodes are not subject to source-stage format validation.
  EXPECT_NO_THROW(orc::project_io::load_project_from_yaml(
      yaml, "/tmp/transform-ok.orcprj"));
}

// Unified source stages (tbc_source, cvbs_source) with no system-specific
// name component are accepted in any project regardless of video_format.
TEST(ProjectFormatTest, UnifiedSourceStage_AcceptedInAnyFormat) {
  const std::string yaml = R"yaml(
project:
  name: unified-source-project
  version: "2.0"
  video_format: NTSC
  source_format: Composite
  amplitude_unit: IRE
dag:
  nodes:
    - id: 1
      stage: tbc_source
      node_type: SOURCE
      x: 0
      y: 0
  edges: []
)yaml";

  EXPECT_NO_THROW(orc::project_io::load_project_from_yaml(
      yaml, "/tmp/unified-source.orcprj"));
}

// ---------------------------------------------------------------------------
// amplitude_unit storage and round-trip (Task 7.2)
// ---------------------------------------------------------------------------

TEST(ProjectFormatTest, AmplitudeUnit_LoadFromYaml_mV_ReturnsMillivolts) {
  const auto project = orc::project_io::load_project_from_yaml(
      kV2EmptyProject, "/tmp/au-mv.orcprj");
  EXPECT_EQ(project.get_amplitude_unit(),
            orc::AmplitudeDisplayUnit::Millivolts);
}

TEST(ProjectFormatTest, AmplitudeUnit_LoadFromYaml_IRE_ReturnsIRE) {
  const auto project = orc::project_io::load_project_from_yaml(
      kNtscProjectIRE, "/tmp/au-ire.orcprj");
  EXPECT_EQ(project.get_amplitude_unit(), orc::AmplitudeDisplayUnit::IRE);
}

TEST(ProjectFormatTest, AmplitudeUnit_LoadFromYaml_10bit_ReturnsSamples10Bit) {
  const auto project = orc::project_io::load_project_from_yaml(
      kProject10BitUnit, "/tmp/au-10bit.orcprj");
  EXPECT_EQ(project.get_amplitude_unit(),
            orc::AmplitudeDisplayUnit::Samples10Bit);
}

TEST(ProjectFormatTest, AmplitudeUnit_SetAmplitudeUnit_MarksProjectModified) {
  auto project = orc::project_io::create_empty_project(
      "modify-test", orc::VideoSystem::PAL, orc::SourceType::Composite);
  project.clear_modified_flag();
  EXPECT_FALSE(project.has_unsaved_changes());

  orc::project_io::set_amplitude_unit(project, orc::AmplitudeDisplayUnit::IRE);
  EXPECT_TRUE(project.has_unsaved_changes());
}

TEST(ProjectFormatTest, AmplitudeUnit_SerializeReload_PreservesNonDefaultUnit) {
  auto project = orc::project_io::create_empty_project(
      "rt-unit", orc::VideoSystem::PAL, orc::SourceType::Composite);
  // PAL default is Millivolts; explicitly override to Samples10Bit.
  orc::project_io::set_amplitude_unit(project,
                                      orc::AmplitudeDisplayUnit::Samples10Bit);

  const std::string yaml = orc::project_io::serialize_project_to_yaml(
      project, "/tmp/rt-unit.orcprj");
  const auto reloaded =
      orc::project_io::load_project_from_yaml(yaml, "/tmp/rt-unit.orcprj");

  EXPECT_EQ(reloaded.get_amplitude_unit(),
            orc::AmplitudeDisplayUnit::Samples10Bit);
}

}  // namespace orc_unit_test
