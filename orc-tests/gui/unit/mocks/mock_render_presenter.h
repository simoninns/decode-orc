/*
 * File:        mock_render_presenter.h
 * Module:      orc-tests/gui/unit
 * Purpose:     GMock scaffold for RenderCoordinator's IRenderPresenter seam
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <gmock/gmock.h>

#include "render_coordinator.h"

namespace orc::presenters::test {

class MockRenderPresenter : public IRenderPresenter {
 public:
  MOCK_METHOD(void, setDAG, (std::shared_ptr<void> dag_handle), (override));
  MOCK_METHOD(bool, getShowDropouts, (), (const, override));
  MOCK_METHOD(void, setShowDropouts, (bool show), (override));

  MOCK_METHOD(orc::PreviewRenderResult, renderPreview,
              (NodeID node_id, orc::PreviewOutputType output_type,
               uint64_t output_index, const std::string& option_id),
              (override));

  MOCK_METHOD((std::optional<VBIFieldInfoView>), getVBIData,
              (NodeID node_id, FieldID field_id), (override));
  MOCK_METHOD(bool, getDropoutAnalysisData,
              (NodeID node_id, (std::vector<void*> & frame_stats),
               int32_t& total_frames),
              (override));
  MOCK_METHOD(bool, getSNRAnalysisData,
              (NodeID node_id, (std::vector<void*> & frame_stats),
               int32_t& total_frames),
              (override));
  MOCK_METHOD(bool, getBurstLevelAnalysisData,
              (NodeID node_id, (std::vector<void*> & frame_stats),
               int32_t& total_frames),
              (override));
  MOCK_METHOD((std::vector<orc::PreviewOutputInfo>), getAvailableOutputs,
              (NodeID node_id), (override));

  MOCK_METHOD(LineSampleData, getLineSamplesWithYC,
              (NodeID node_id, orc::PreviewOutputType output_type,
               uint64_t output_index, int line_number, int sample_x,
               int preview_width),
              (override));

  MOCK_METHOD((std::optional<orc::SourceParameters>), getVideoParameters,
              (NodeID node_id), (override));

  MOCK_METHOD(LineSampleData, getFieldSamplesForTiming,
              (NodeID node_id, orc::PreviewOutputType output_type,
               uint64_t output_index),
              (override));

  MOCK_METHOD(orc::FrameLineNavigationResult, navigateFrameLine,
              (NodeID node_id, orc::PreviewOutputType output_type,
               uint64_t current_field, int current_line, int direction,
               int field_height),
              (override));

  MOCK_METHOD(uint64_t, triggerStage,
              (NodeID node_id, TriggerProgressCallback callback), (override));
  MOCK_METHOD(void, cancelTrigger, (), (override));

  MOCK_METHOD(bool, savePNG,
              (NodeID node_id, orc::PreviewOutputType output_type,
               uint64_t output_index, const std::string& filename,
               const std::string& option_id, double aspect_correction),
              (override));

  MOCK_METHOD(orc::ImageToFieldMappingResult, mapImageToField,
              (NodeID node_id, orc::PreviewOutputType output_type,
               uint64_t output_index, int image_y, int image_height),
              (override));

  MOCK_METHOD(orc::FieldToImageMappingResult, mapFieldToImage,
              (NodeID node_id, orc::PreviewOutputType output_type,
               uint64_t output_index, uint64_t field_index, int field_line,
               int image_height),
              (override));

  MOCK_METHOD(orc::FrameFieldsResult, getFrameFields,
              (NodeID node_id, uint64_t frame_index), (override));

  MOCK_METHOD((std::vector<orc::PreviewViewDescriptor>),
              getAvailablePreviewViews,
              (NodeID node_id, orc::VideoDataType data_type), (override));

  MOCK_METHOD(orc::PreviewViewDataResult, requestPreviewViewData,
              (NodeID node_id, const std::string& view_id,
               orc::VideoDataType data_type,
               const orc::PreviewCoordinate& coordinate),
              (override));

  MOCK_METHOD(bool, applyStageParameters,
              (NodeID node_id,
               (const std::map<std::string, orc::ParameterValue>& params)),
              (override));

  MOCK_METHOD((std::vector<orc::LiveTweakableParameterView>),
              getStageTweakableParameters, (NodeID node_id), (override));
  MOCK_METHOD((std::map<std::string, orc::ParameterValue>),
              getStageCurrentParameters, (NodeID node_id), (override));
};

}  // namespace orc::presenters::test
