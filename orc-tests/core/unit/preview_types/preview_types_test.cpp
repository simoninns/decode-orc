/*
 * File:        preview_types_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for Phase 1 preview-refactor foundation types:
 *              - VideoDataType taxonomy enum coverage
 *              - ColorimetricMetadata defaults, round-trip, and equality
 *              - PreviewCoordinate construction, equality, and validity
 *              - PreviewNavigationExtent and PreviewGeometry validity
 *              - StagePreviewCapability schema and validity
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include <gtest/gtest.h>

#include <climits>

#include "../../../orc/core/include/stage_preview_capability.h"
#include "../../../orc/view-types/orc_preview_types.h"
#include "../../../orc/view-types/orc_preview_views.h"
#include "../../../orc/view-types/orc_vectorscope.h"

namespace orc_unit_test {

// =============================================================================
// VideoDataType — taxonomy enum coverage
// =============================================================================

TEST(VideoDataTypeTest, allSixValuesAreDistinct) {
  using T = orc::VideoDataType;
  EXPECT_NE(T::CompositeNTSC, T::CompositePAL);
  EXPECT_NE(T::YC_NTSC, T::YC_PAL);
  EXPECT_NE(T::ColourNTSC, T::ColourPAL);
  EXPECT_NE(T::CompositeNTSC, T::YC_NTSC);
  EXPECT_NE(T::CompositeNTSC, T::ColourNTSC);
  EXPECT_NE(T::YC_NTSC, T::ColourNTSC);
}

TEST(VideoDataTypeTest, signalDomainTypesAreDistinctFromColourDomainTypes) {
  // Signal-domain types (Composite*, YC_*) must be distinguishable
  // from colour-domain types (Colour*).
  using T = orc::VideoDataType;
  EXPECT_NE(T::CompositeNTSC, T::ColourNTSC);
  EXPECT_NE(T::CompositeNTSC, T::ColourPAL);
  EXPECT_NE(T::CompositePAL, T::ColourNTSC);
  EXPECT_NE(T::CompositePAL, T::ColourPAL);
  EXPECT_NE(T::YC_NTSC, T::ColourNTSC);
  EXPECT_NE(T::YC_NTSC, T::ColourPAL);
  EXPECT_NE(T::YC_PAL, T::ColourNTSC);
  EXPECT_NE(T::YC_PAL, T::ColourPAL);
}

TEST(VideoDataTypeTest, ntscVariantsAreDistinctFromPalVariants) {
  using T = orc::VideoDataType;
  EXPECT_NE(T::CompositeNTSC, T::CompositePAL);
  EXPECT_NE(T::YC_NTSC, T::YC_PAL);
  EXPECT_NE(T::ColourNTSC, T::ColourPAL);
}

// =============================================================================
// ColorimetricMatrixCoefficients — enum sanity
// =============================================================================

TEST(ColorimetricMatrixCoefficientsTest,
     unspecifiedIsDistinctFromAllConcreteValues) {
  using M = orc::ColorimetricMatrixCoefficients;
  EXPECT_NE(M::Unspecified, M::NTSC1953_FCC);
  EXPECT_NE(M::Unspecified, M::BT601_625);
  EXPECT_NE(M::Unspecified, M::BT601_525);
}

TEST(ColorimetricMatrixCoefficientsTest, ntscAndPalMatricesAreDistinct) {
  using M = orc::ColorimetricMatrixCoefficients;
  EXPECT_NE(M::BT601_525, M::BT601_625);
}

// =============================================================================
// ColorimetricPrimaries — enum sanity
// =============================================================================

TEST(ColorimetricPrimariesTest, unspecifiedIsDistinctFromAllConcreteValues) {
  using P = orc::ColorimetricPrimaries;
  EXPECT_NE(P::Unspecified, P::NTSC1953);
  EXPECT_NE(P::Unspecified, P::SMPTE_C);
  EXPECT_NE(P::Unspecified, P::EBU_BT470_PAL);
  EXPECT_NE(P::Unspecified, P::BT709);
}

TEST(ColorimetricPrimariesTest, ntscAndPalPrimariesAreDistinct) {
  EXPECT_NE(orc::ColorimetricPrimaries::SMPTE_C,
            orc::ColorimetricPrimaries::EBU_BT470_PAL);
}

// =============================================================================
// ColorimetricTransferCharacteristics — enum sanity
// =============================================================================

TEST(ColorimetricTransferCharacteristicsTest,
     unspecifiedIsDistinctFromAllConcreteValues) {
  using TC = orc::ColorimetricTransferCharacteristics;
  EXPECT_NE(TC::Unspecified, TC::Gamma22);
  EXPECT_NE(TC::Unspecified, TC::Gamma28);
  EXPECT_NE(TC::Unspecified, TC::BT709);
  EXPECT_NE(TC::Unspecified, TC::BT1886);
  EXPECT_NE(TC::Unspecified, TC::BT1886App1);
}

TEST(ColorimetricTransferCharacteristicsTest, ntscAndPalGammasAreDistinct) {
  EXPECT_NE(orc::ColorimetricTransferCharacteristics::Gamma22,
            orc::ColorimetricTransferCharacteristics::Gamma28);
}

// =============================================================================
// ColorimetricMetadata — defaults
// =============================================================================

TEST(ColorimetricMetadataTest,
     defaultConstructedHasUnspecifiedMatrixCoefficients) {
  orc::ColorimetricMetadata meta{};
  EXPECT_EQ(meta.matrix_coefficients,
            orc::ColorimetricMatrixCoefficients::Unspecified);
}

TEST(ColorimetricMetadataTest, defaultConstructedHasUnspecifiedPrimaries) {
  orc::ColorimetricMetadata meta{};
  EXPECT_EQ(meta.primaries, orc::ColorimetricPrimaries::Unspecified);
}

TEST(ColorimetricMetadataTest,
     defaultConstructedHasUnspecifiedTransferCharacteristics) {
  orc::ColorimetricMetadata meta{};
  EXPECT_EQ(meta.transfer_characteristics,
            orc::ColorimetricTransferCharacteristics::Unspecified);
}

TEST(ColorimetricMetadataTest, defaultNtscHasExpectedMatrixCoefficients) {
  auto meta = orc::ColorimetricMetadata::default_ntsc();
  EXPECT_EQ(meta.matrix_coefficients,
            orc::ColorimetricMatrixCoefficients::BT601_525);
}

TEST(ColorimetricMetadataTest, defaultNtscHasExpectedPrimaries) {
  auto meta = orc::ColorimetricMetadata::default_ntsc();
  EXPECT_EQ(meta.primaries, orc::ColorimetricPrimaries::SMPTE_C);
}

TEST(ColorimetricMetadataTest, defaultNtscHasExpectedTransferCharacteristics) {
  auto meta = orc::ColorimetricMetadata::default_ntsc();
  EXPECT_EQ(meta.transfer_characteristics,
            orc::ColorimetricTransferCharacteristics::Gamma22);
}

TEST(ColorimetricMetadataTest, defaultPalHasExpectedMatrixCoefficients) {
  auto meta = orc::ColorimetricMetadata::default_pal();
  EXPECT_EQ(meta.matrix_coefficients,
            orc::ColorimetricMatrixCoefficients::BT601_625);
}

TEST(ColorimetricMetadataTest, defaultPalHasExpectedPrimaries) {
  auto meta = orc::ColorimetricMetadata::default_pal();
  EXPECT_EQ(meta.primaries, orc::ColorimetricPrimaries::EBU_BT470_PAL);
}

TEST(ColorimetricMetadataTest, defaultPalHasExpectedTransferCharacteristics) {
  auto meta = orc::ColorimetricMetadata::default_pal();
  EXPECT_EQ(meta.transfer_characteristics,
            orc::ColorimetricTransferCharacteristics::Gamma28);
}

// =============================================================================
// ColorimetricMetadata — equality and round-trip
// =============================================================================

TEST(ColorimetricMetadataTest, defaultNtscAndDefaultPalAreNotEqual) {
  EXPECT_NE(orc::ColorimetricMetadata::default_ntsc(),
            orc::ColorimetricMetadata::default_pal());
}

TEST(ColorimetricMetadataTest, equalityIsReflexive) {
  auto meta = orc::ColorimetricMetadata::default_ntsc();
  EXPECT_EQ(meta, meta);
}

TEST(ColorimetricMetadataTest, copyPreservesAllFields) {
  auto original = orc::ColorimetricMetadata::default_pal();
  auto copy = original;
  EXPECT_EQ(copy, original);
}

TEST(ColorimetricMetadataTest, modifiedMatrixIsNotEqualToOriginal) {
  auto original = orc::ColorimetricMetadata::default_ntsc();
  auto modified = original;
  modified.matrix_coefficients =
      orc::ColorimetricMatrixCoefficients::NTSC1953_FCC;
  EXPECT_NE(modified, original);
}

TEST(ColorimetricMetadataTest, modifiedPrimariesIsNotEqualToOriginal) {
  auto original = orc::ColorimetricMetadata::default_ntsc();
  auto modified = original;
  modified.primaries = orc::ColorimetricPrimaries::NTSC1953;
  EXPECT_NE(modified, original);
}

TEST(ColorimetricMetadataTest, modifiedTransferIsNotEqualToOriginal) {
  auto original = orc::ColorimetricMetadata::default_ntsc();
  auto modified = original;
  modified.transfer_characteristics =
      orc::ColorimetricTransferCharacteristics::BT709;
  EXPECT_NE(modified, original);
}

TEST(ColorimetricMetadataTest, unspecifiedInstanceIsNotEqualToDefaultNtsc) {
  orc::ColorimetricMetadata unspecified{};
  EXPECT_NE(unspecified, orc::ColorimetricMetadata::default_ntsc());
}

// =============================================================================
// PreviewCoordinate — construction and defaults
// =============================================================================

TEST(PreviewCoordinateTest, defaultConstructedHasZeroFieldIndex) {
  orc::PreviewCoordinate coord{};
  EXPECT_EQ(coord.field_index, 0u);
}

TEST(PreviewCoordinateTest, defaultConstructedHasZeroLineIndex) {
  orc::PreviewCoordinate coord{};
  EXPECT_EQ(coord.line_index, 0u);
}

TEST(PreviewCoordinateTest, defaultConstructedHasZeroSampleOffset) {
  orc::PreviewCoordinate coord{};
  EXPECT_EQ(coord.sample_offset, 0u);
}

TEST(PreviewCoordinateTest,
     defaultConstructedHasCompositeNtscDataTypeContext) {
  orc::PreviewCoordinate coord{};
  EXPECT_EQ(coord.data_type_context, orc::VideoDataType::CompositeNTSC);
}

TEST(PreviewCoordinateTest,
     defaultConstructedVectorscopeDefaultsToActiveAreaOnly) {
  orc::PreviewCoordinate coord{};
  EXPECT_TRUE(coord.vectorscope_active_area_only);
}

// =============================================================================
// PreviewCoordinate — validity / bounds
// =============================================================================

TEST(PreviewCoordinateTest, defaultConstructedIsValid) {
  orc::PreviewCoordinate coord{};
  EXPECT_TRUE(coord.is_valid());
}

TEST(PreviewCoordinateTest, representativeFieldIndexIsValid) {
  orc::PreviewCoordinate coord{};
  coord.field_index = 1000u;
  EXPECT_TRUE(coord.is_valid());
}

TEST(PreviewCoordinateTest, maxFieldIndexIsNotValid) {
  orc::PreviewCoordinate coord{};
  coord.field_index = UINT64_MAX;
  EXPECT_FALSE(coord.is_valid());
}

TEST(PreviewCoordinateTest, maxLineIndexIsNotValid) {
  orc::PreviewCoordinate coord{};
  coord.line_index = UINT32_MAX;
  EXPECT_FALSE(coord.is_valid());
}

TEST(PreviewCoordinateTest, maxSampleOffsetIsNotValid) {
  orc::PreviewCoordinate coord{};
  coord.sample_offset = UINT32_MAX;
  EXPECT_FALSE(coord.is_valid());
}

// =============================================================================
// PreviewCoordinate — equality
// =============================================================================

TEST(PreviewCoordinateTest, equalityIsReflexive) {
  orc::PreviewCoordinate coord{42u, 10u, 200u, orc::VideoDataType::YC_PAL};
  EXPECT_EQ(coord, coord);
}

TEST(PreviewCoordinateTest, differentFieldIndexIsNotEqual) {
  orc::PreviewCoordinate a{1u, 10u, 0u, orc::VideoDataType::CompositeNTSC};
  orc::PreviewCoordinate b{2u, 10u, 0u, orc::VideoDataType::CompositeNTSC};
  EXPECT_NE(a, b);
}

TEST(PreviewCoordinateTest, differentLineIndexIsNotEqual) {
  orc::PreviewCoordinate a{0u, 10u, 0u, orc::VideoDataType::CompositePAL};
  orc::PreviewCoordinate b{0u, 20u, 0u, orc::VideoDataType::CompositePAL};
  EXPECT_NE(a, b);
}

TEST(PreviewCoordinateTest, differentSampleOffsetIsNotEqual) {
  orc::PreviewCoordinate a{0u, 0u, 100u, orc::VideoDataType::YC_NTSC};
  orc::PreviewCoordinate b{0u, 0u, 200u, orc::VideoDataType::YC_NTSC};
  EXPECT_NE(a, b);
}

TEST(PreviewCoordinateTest, differentDataTypeContextIsNotEqual) {
  orc::PreviewCoordinate a{0u, 0u, 0u, orc::VideoDataType::CompositeNTSC};
  orc::PreviewCoordinate b{0u, 0u, 0u, orc::VideoDataType::CompositePAL};
  EXPECT_NE(a, b);
}

TEST(PreviewCoordinateTest, copyPreservesAllFields) {
  orc::PreviewCoordinate original{100u, 50u, 300u,
                                  orc::VideoDataType::ColourPAL};
  auto copy = original;
  EXPECT_EQ(copy, original);
}

TEST(PreviewCoordinateTest, differentVectorscopeAreaPreferenceIsNotEqual) {
  orc::PreviewCoordinate active_only{0u, 0u, 0u, orc::VideoDataType::ColourNTSC,
                                     true};
  orc::PreviewCoordinate full_frame{0u, 0u, 0u, orc::VideoDataType::ColourNTSC,
                                    false};
  EXPECT_NE(active_only, full_frame);
}

// =============================================================================
// PreviewNavigationExtent — validity
// =============================================================================

TEST(PreviewNavigationExtentTest, defaultConstructedIsNotValid) {
  orc::PreviewNavigationExtent ext{};
  EXPECT_FALSE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, withPositiveItemCountAndLabelIsValid) {
  orc::PreviewNavigationExtent ext{100, 1, "field"};
  EXPECT_TRUE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, withZeroItemCountIsNotValid) {
  orc::PreviewNavigationExtent ext{0, 1, "field"};
  EXPECT_FALSE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, withZeroGranularityIsNotValid) {
  orc::PreviewNavigationExtent ext{100, 0, "field"};
  EXPECT_FALSE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, withEmptyLabelIsNotValid) {
  orc::PreviewNavigationExtent ext{100, 1, ""};
  EXPECT_FALSE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, frameGranularityIsValid) {
  // Frame-navigating stages expose every-other-field, so granularity == 2
  orc::PreviewNavigationExtent ext{50, 2, "frame"};
  EXPECT_TRUE(ext.is_valid());
}

// =============================================================================
// PreviewGeometry — validity
// =============================================================================

TEST(PreviewGeometryTest, defaultConstructedIsNotValid) {
  orc::PreviewGeometry geo{};
  EXPECT_FALSE(geo.is_valid());
}

TEST(PreviewGeometryTest, withValidPalDimensionsIsValid) {
  orc::PreviewGeometry geo{910, 313, 4.0 / 3.0, 0.7};
  EXPECT_TRUE(geo.is_valid());
}

TEST(PreviewGeometryTest, withValidNtscDimensionsIsValid) {
  orc::PreviewGeometry geo{760, 263, 4.0 / 3.0, 0.7};
  EXPECT_TRUE(geo.is_valid());
}

TEST(PreviewGeometryTest, withZeroWidthIsNotValid) {
  orc::PreviewGeometry geo{0, 313, 4.0 / 3.0, 0.7};
  EXPECT_FALSE(geo.is_valid());
}

TEST(PreviewGeometryTest, withZeroHeightIsNotValid) {
  orc::PreviewGeometry geo{910, 0, 4.0 / 3.0, 0.7};
  EXPECT_FALSE(geo.is_valid());
}

TEST(PreviewGeometryTest, withZeroDisplayAspectRatioIsNotValid) {
  orc::PreviewGeometry geo{910, 313, 0.0, 0.7};
  EXPECT_FALSE(geo.is_valid());
}

TEST(PreviewGeometryTest, withZeroDarCorrectionFactorIsNotValid) {
  orc::PreviewGeometry geo{910, 313, 4.0 / 3.0, 0.0};
  EXPECT_FALSE(geo.is_valid());
}

// =============================================================================
// PreviewTweakableParameter — tweak class values
// =============================================================================

TEST(PreviewTweakableParameterTest, displayPhaseClassIsPreserved) {
  orc::PreviewTweakableParameter param{"chroma_matrix",
                                       orc::PreviewTweakClass::DisplayPhase};
  EXPECT_EQ(param.parameter_name, "chroma_matrix");
  EXPECT_EQ(param.tweak_class, orc::PreviewTweakClass::DisplayPhase);
}

TEST(PreviewTweakableParameterTest, decodePhaseClassIsPreserved) {
  orc::PreviewTweakableParameter param{"chroma_gain",
                                       orc::PreviewTweakClass::DecodePhase};
  EXPECT_EQ(param.parameter_name, "chroma_gain");
  EXPECT_EQ(param.tweak_class, orc::PreviewTweakClass::DecodePhase);
}

TEST(PreviewTweakableParameterTest, displayPhaseAndDecodePhaseAreDistinct) {
  EXPECT_NE(orc::PreviewTweakClass::DisplayPhase,
            orc::PreviewTweakClass::DecodePhase);
}

// =============================================================================
// StagePreviewCapability — schema and validity
// =============================================================================

TEST(StagePreviewCapabilityTest, defaultConstructedIsNotValid) {
  orc::StagePreviewCapability cap{};
  EXPECT_FALSE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, withNoDataTypesIsNotValid) {
  orc::StagePreviewCapability cap{};
  cap.navigation_extent = {100, 1, "field"};
  cap.geometry = {910, 313, 4.0 / 3.0, 0.7};
  // supported_data_types is still empty
  EXPECT_FALSE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, withZeroItemCountIsNotValid) {
  orc::StagePreviewCapability cap{};
  cap.supported_data_types = {orc::VideoDataType::CompositePAL};
  cap.navigation_extent = {0, 1, "field"};  // item_count == 0
  cap.geometry = {910, 313, 4.0 / 3.0, 0.7};
  EXPECT_FALSE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, withZeroGeometryWidthIsNotValid) {
  orc::StagePreviewCapability cap{};
  cap.supported_data_types = {orc::VideoDataType::CompositePAL};
  cap.navigation_extent = {100, 1, "field"};
  cap.geometry = {0, 313, 4.0 / 3.0, 0.7};  // active_width == 0
  EXPECT_FALSE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, minimumValidCapabilityIsValid) {
  orc::StagePreviewCapability cap{};
  cap.supported_data_types = {orc::VideoDataType::CompositePAL};
  cap.navigation_extent = {100, 1, "field"};
  cap.geometry = {910, 313, 4.0 / 3.0, 0.7};
  EXPECT_TRUE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, multipleDataTypesIsValid) {
  orc::StagePreviewCapability cap{};
  cap.supported_data_types = {orc::VideoDataType::YC_PAL,
                              orc::VideoDataType::ColourPAL};
  cap.navigation_extent = {50, 1, "frame"};
  cap.geometry = {928, 576, 4.0 / 3.0, 0.7};
  EXPECT_TRUE(cap.is_valid());
  EXPECT_EQ(cap.supported_data_types.size(), 2u);
}

TEST(StagePreviewCapabilityTest,
     emptyTweakableParametersDoesNotInvalidateCapability) {
  orc::StagePreviewCapability cap{};
  cap.supported_data_types = {orc::VideoDataType::CompositeNTSC};
  cap.navigation_extent = {200, 1, "field"};
  cap.geometry = {760, 263, 4.0 / 3.0, 0.7};
  EXPECT_TRUE(cap.is_valid());
  EXPECT_TRUE(cap.tweakable_parameters.empty());
}

TEST(StagePreviewCapabilityTest, tweakableParametersAreIncluded) {
  orc::StagePreviewCapability cap{};
  cap.supported_data_types = {orc::VideoDataType::ColourNTSC,
                              orc::VideoDataType::YC_NTSC};
  cap.navigation_extent = {100, 1, "frame"};
  cap.geometry = {760, 486, 4.0 / 3.0, 0.7};
  cap.tweakable_parameters = {
      {"chroma_matrix", orc::PreviewTweakClass::DisplayPhase},
      {"chroma_gain", orc::PreviewTweakClass::DecodePhase},
  };
  EXPECT_TRUE(cap.is_valid());
  EXPECT_EQ(cap.tweakable_parameters.size(), 2u);
}

// =============================================================================
// IStagePreviewCapability — interface mock smoke-test
// =============================================================================

namespace {
// Minimal mock to verify the interface can be implemented.
class MockPreviewCapabilityStage : public orc::IStagePreviewCapability {
 public:
  orc::StagePreviewCapability get_preview_capability() const override {
    orc::StagePreviewCapability cap{};
    cap.supported_data_types = {orc::VideoDataType::CompositePAL};
    cap.navigation_extent = {400, 1, "field"};
    cap.geometry = {910, 313, 4.0 / 3.0, 0.7};
    return cap;
  }
};
}  // anonymous namespace

TEST(IStagePreviewCapabilityTest,
     concreteImplementationReturnsValidCapability) {
  MockPreviewCapabilityStage stage{};
  auto cap = stage.get_preview_capability();
  EXPECT_TRUE(cap.is_valid());
}

TEST(IStagePreviewCapabilityTest,
     concreteImplementationReturnsExpectedDataType) {
  MockPreviewCapabilityStage stage{};
  auto cap = stage.get_preview_capability();
  ASSERT_EQ(cap.supported_data_types.size(), 1u);
  EXPECT_EQ(cap.supported_data_types.front(), orc::VideoDataType::CompositePAL);
}

TEST(IStagePreviewCapabilityTest,
     concreteImplementationReturnsExpectedItemCount) {
  MockPreviewCapabilityStage stage{};
  auto cap = stage.get_preview_capability();
  EXPECT_EQ(cap.navigation_extent.item_count, 400u);
}

// =============================================================================
// PreviewViewDataResult — is_valid() correctness
// =============================================================================

namespace {

orc::PreviewImage make_valid_preview_image() {
  orc::PreviewImage img{};
  img.width = 1;
  img.height = 1;
  img.rgb_data = {0, 0, 0};
  return img;
}

}  // anonymous namespace

TEST(PreviewViewDataResultTest, failedResultIsNotValid) {
  orc::PreviewViewDataResult result{};
  result.success = false;
  EXPECT_FALSE(result.is_valid());
}

TEST(PreviewViewDataResultTest, successWithNoneKindIsNotValid) {
  // A successful result with payload kind None has no useful payload.
  orc::PreviewViewDataResult result{};
  result.success = true;
  result.payload_kind = orc::PreviewViewPayloadKind::None;
  EXPECT_FALSE(result.is_valid());
}

TEST(PreviewViewDataResultTest, successWithImageKindAndValidImageIsValid) {
  orc::PreviewViewDataResult result{};
  result.success = true;
  result.payload_kind = orc::PreviewViewPayloadKind::Image;
  result.image = make_valid_preview_image();
  EXPECT_TRUE(result.is_valid());
}

TEST(PreviewViewDataResultTest, successWithImageKindButEmptyImageIsNotValid) {
  orc::PreviewViewDataResult result{};
  result.success = true;
  result.payload_kind = orc::PreviewViewPayloadKind::Image;
  // image left as std::nullopt
  EXPECT_FALSE(result.is_valid());
}

TEST(PreviewViewDataResultTest,
     successWithImageKindButInvalidImageIsNotValid) {
  orc::PreviewImage bad_image{};
  bad_image.width = 2;
  bad_image.height = 2;
  bad_image.rgb_data = {0, 0, 0};  // too small (needs 2*2*3 = 12 bytes)

  orc::PreviewViewDataResult result{};
  result.success = true;
  result.payload_kind = orc::PreviewViewPayloadKind::Image;
  result.image = bad_image;
  EXPECT_FALSE(result.is_valid());
}

TEST(PreviewViewDataResultTest, successWithVectorscopeKindAndDataIsValid) {
  orc::VectorscopeData vs{};
  vs.width = 1;
  vs.height = 1;

  orc::PreviewViewDataResult result{};
  result.success = true;
  result.payload_kind = orc::PreviewViewPayloadKind::Vectorscope;
  result.vectorscope = vs;
  EXPECT_TRUE(result.is_valid());
}

TEST(PreviewViewDataResultTest,
     successWithVectorscopeKindButNoDataIsNotValid) {
  orc::PreviewViewDataResult result{};
  result.success = true;
  result.payload_kind = orc::PreviewViewPayloadKind::Vectorscope;
  // vectorscope left as std::nullopt
  EXPECT_FALSE(result.is_valid());
}

// =============================================================================
// LiveTweakableParameterView — view-types mirror of PreviewTweakableParameter
// =============================================================================

TEST(LiveTweakableParameterViewTest, displayPhaseClassIsPreserved) {
  orc::LiveTweakableParameterView param;
  param.parameter_name = "chroma_matrix";
  param.tweak_class = orc::LiveTweakClass::DisplayPhase;
  EXPECT_EQ(param.parameter_name, "chroma_matrix");
  EXPECT_EQ(param.tweak_class, orc::LiveTweakClass::DisplayPhase);
}

TEST(LiveTweakableParameterViewTest, decodePhaseClassIsPreserved) {
  orc::LiveTweakableParameterView param;
  param.parameter_name = "chroma_gain";
  param.tweak_class = orc::LiveTweakClass::DecodePhase;
  EXPECT_EQ(param.parameter_name, "chroma_gain");
  EXPECT_EQ(param.tweak_class, orc::LiveTweakClass::DecodePhase);
}

TEST(LiveTweakableParameterViewTest, displayPhaseAndDecodePhaseAreDistinct) {
  EXPECT_NE(orc::LiveTweakClass::DisplayPhase,
            orc::LiveTweakClass::DecodePhase);
}

TEST(LiveTweakableParameterViewTest, defaultTweakClassIsDecodePhase) {
  orc::LiveTweakableParameterView param;
  EXPECT_EQ(param.tweak_class, orc::LiveTweakClass::DecodePhase);
}

// =============================================================================
// rgb_to_uv — inline chrominance conversion
// =============================================================================

TEST(RgbToUvTest, neutralGrayProducesNearZeroChroma) {
  // Equal R=G=B should have near-zero U and V components.
  const auto sample = orc::rgb_to_uv(32768, 32768, 32768);
  EXPECT_NEAR(sample.u, 0.0, 1.0);
  EXPECT_NEAR(sample.v, 0.0, 1.0);
}

TEST(RgbToUvTest, fullBlueProducesPositiveU) {
  // Pure blue should have positive U (Cb) according to BT.601.
  const auto sample = orc::rgb_to_uv(0, 0, 65535);
  EXPECT_GT(sample.u, 0.0);
}

TEST(RgbToUvTest, fullRedProducesPositiveV) {
  // Pure red should have positive V (Cr) according to BT.601.
  const auto sample = orc::rgb_to_uv(65535, 0, 0);
  EXPECT_GT(sample.v, 0.0);
}

TEST(RgbToUvTest, outputIsInSignedRange) {
  // U and V values should be in approximately [-32768, +32768] range.
  const auto sample = orc::rgb_to_uv(65535, 0, 0);
  EXPECT_GE(sample.u, -32768.0);
  EXPECT_LE(sample.u, 32768.0);
  EXPECT_GE(sample.v, -32768.0);
  EXPECT_LE(sample.v, 32768.0);
}

TEST(RgbToUvTest, fieldIdDefaultsToZero) {
  const auto sample = orc::rgb_to_uv(0, 0, 0);
  EXPECT_EQ(sample.field_id, 0u);
}

}  // namespace orc_unit_test
