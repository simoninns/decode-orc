/*
 * File:        daphne_vbi_sink_stage_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit test(s) for Daphne VBI sink stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#include "daphne_vbi_sink_stage.h"

#include <gtest/gtest.h>

#include "../../include/observation_context_interface_mock.h"
#include "../../include/video_field_representation_mock.h"
#include "daphne_vbi_sink_stage_deps_interface_mock.h"

using testing::_;  // NOLINT(bugprone-reserved-identifier)
using testing::Ref;
using testing::Return;
using testing::ReturnRef;
using testing::StrictMock;

// using different namespace from module-under-test so that we can use the same
// class names in the tests as in the module-under-test
namespace orc_unit_test {
// test fixture for DaphneVBISinkStage suite of tests
class DaphneVBISinkStage : public ::testing::Test {
 public:
  void SetUp() override {
    pMockDeps_ = std::make_shared<StrictMock<MockDaphneVBISinkStageDeps>>();
    pMockRepresentation_ =
        std::make_shared<StrictMock<MockVideoFieldRepresentation>>();

    instance_ = std::make_unique<orc::DaphneVBISinkStage>(
        static_cast<orc::IStageServices*>(nullptr));
  }

  void TearDown() override { instance_.reset(); }

 protected:
  std::vector<orc::ArtifactPtr> make_valid_input() const {
    return {std::static_pointer_cast<orc::Artifact>(pMockRepresentation_)};
  }

  std::shared_ptr<StrictMock<MockDaphneVBISinkStageDeps>> pMockDeps_;
  std::shared_ptr<StrictMock<MockVideoFieldRepresentation>>
      pMockRepresentation_;
  MockObservationContext mockObservationContext;

  std::unique_ptr<orc::DaphneVBISinkStage> instance_;
};

////////////////////////////////////////////////////////////////////////////////////////////

TEST_F(DaphneVBISinkStage, triggerReturnsFalseWhenOutputPathMissing) {
  const bool result = instance_->trigger({}, {}, mockObservationContext);

  EXPECT_FALSE(result);
  EXPECT_EQ(instance_->get_trigger_status(), "Error: No output path specified");
}

TEST_F(DaphneVBISinkStage, triggerReturnsFalseWhenOutputPathIsEmpty) {
  const std::map<std::string, orc::ParameterValue> parameters = {
      {"output_path", std::string("")}};

  const bool result =
      instance_->trigger({}, parameters, mockObservationContext);

  EXPECT_FALSE(result);
  EXPECT_EQ(instance_->get_trigger_status(), "Error: Output path is empty");
}

TEST_F(DaphneVBISinkStage, triggerReturnsFalseWhenNoInputConnected) {
  const std::map<std::string, orc::ParameterValue> parameters = {
      {"output_path", std::string("out")}};

  const bool result =
      instance_->trigger({}, parameters, mockObservationContext);

  EXPECT_FALSE(result);
  EXPECT_EQ(instance_->get_trigger_status(), "Error: No input connected");
}

TEST_F(DaphneVBISinkStage,
       triggerReturnsFalseWhenInputNotVideoFieldRepresentation) {
  const std::map<std::string, orc::ParameterValue> parameters = {
      {"output_path", std::string("out")}};
  const std::vector<orc::ArtifactPtr> inputs = {nullptr};

  const bool result =
      instance_->trigger(inputs, parameters, mockObservationContext);

  EXPECT_FALSE(result);
  EXPECT_EQ(instance_->get_trigger_status(),
            "Error: Input is not a video field representation");
}

TEST_F(DaphneVBISinkStage, triggerWritesVbiAndSetsSuccessStatus) {
  const std::map<std::string, orc::ParameterValue> parameters = {
      {"output_path", std::string("out_path")}};
  const std::vector<orc::ArtifactPtr> inputs = make_valid_input();

  // Inject mock deps via seam instead of factory mock.
  instance_->set_deps_override(pMockDeps_);

  // Ref needed here so gMock doesn't try to do the wrong thing
  EXPECT_CALL(*pMockDeps_, write_vbi(pMockRepresentation_.get(), "out_path",
                                     testing::Ref(mockObservationContext)))
      .Times(1)
      .WillOnce(Return(true));

  EXPECT_CALL(*pMockRepresentation_, field_range())
      .Times(1)
      .WillOnce(Return(orc::FieldIDRange(orc::FieldID(0), orc::FieldID(3))));

  const bool result =
      instance_->trigger(inputs, parameters, mockObservationContext);

  EXPECT_TRUE(result);
  EXPECT_EQ(instance_->get_trigger_status(), "Exported 3 fields to out_path");
  EXPECT_FALSE(instance_->is_trigger_in_progress());
}

TEST_F(DaphneVBISinkStage, triggerSetsErrorStatusWhenDepWriteFails) {
  const std::map<std::string, orc::ParameterValue> parameters = {
      {"output_path", std::string("out_path")}};
  const std::vector<orc::ArtifactPtr> inputs = make_valid_input();

  // Inject mock deps via seam instead of factory mock.
  instance_->set_deps_override(pMockDeps_);

  // Ref needed here so gMock doesn't try to do the wrong thing
  EXPECT_CALL(*pMockDeps_, write_vbi(pMockRepresentation_.get(), "out_path",
                                     Ref(mockObservationContext)))
      .Times(1)
      .WillOnce(Return(false));

  EXPECT_CALL(*pMockRepresentation_, field_range()).Times(0);

  const bool result =
      instance_->trigger(inputs, parameters, mockObservationContext);

  EXPECT_FALSE(result);
  EXPECT_EQ(instance_->get_trigger_status(),
            "Error: Failed to write output files");
  EXPECT_FALSE(instance_->is_trigger_in_progress());
}

TEST_F(DaphneVBISinkStage, setParametersAcceptsOutputPathString) {
  const bool result =
      instance_->set_parameters({{"output_path", std::string("abc.vbi")}});
  const auto params = instance_->get_parameters();

  EXPECT_TRUE(result);
  ASSERT_TRUE(params.find("output_path") != params.end());
  EXPECT_EQ(std::get<std::string>(params.at("output_path")), "abc.vbi");
}

TEST_F(DaphneVBISinkStage, setParametersRejectsNonStringOutputPath) {
  const bool result = instance_->set_parameters({{"output_path", static_cast<int32_t>(7)}});

  EXPECT_FALSE(result);
}
}  // namespace orc_unit_test