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

#include "../../../orc/view-types/orc_preview_types.h"
#include "../../../orc/core/include/stage_preview_capability.h"

namespace orc_unit_test
{

// =============================================================================
// VideoDataType — taxonomy enum coverage
// =============================================================================

TEST(VideoDataTypeTest, allSixValuesAreDistinct)
{
    using T = orc::VideoDataType;
    EXPECT_NE(T::CompositeNTSC, T::CompositePAL);
    EXPECT_NE(T::YC_NTSC,       T::YC_PAL);
    EXPECT_NE(T::ColourNTSC,    T::ColourPAL);
    EXPECT_NE(T::CompositeNTSC, T::YC_NTSC);
    EXPECT_NE(T::CompositeNTSC, T::ColourNTSC);
    EXPECT_NE(T::YC_NTSC,       T::ColourNTSC);
}

TEST(VideoDataTypeTest, signalDomainTypes_areDistinctFromColourDomainTypes)
{
    // Signal-domain types (Composite*, YC_*) must be distinguishable
    // from colour-domain types (Colour*).
    using T = orc::VideoDataType;
    EXPECT_NE(T::CompositeNTSC, T::ColourNTSC);
    EXPECT_NE(T::CompositeNTSC, T::ColourPAL);
    EXPECT_NE(T::CompositePAL,  T::ColourNTSC);
    EXPECT_NE(T::CompositePAL,  T::ColourPAL);
    EXPECT_NE(T::YC_NTSC,       T::ColourNTSC);
    EXPECT_NE(T::YC_NTSC,       T::ColourPAL);
    EXPECT_NE(T::YC_PAL,        T::ColourNTSC);
    EXPECT_NE(T::YC_PAL,        T::ColourPAL);
}

TEST(VideoDataTypeTest, ntscVariants_areDistinctFromPalVariants)
{
    using T = orc::VideoDataType;
    EXPECT_NE(T::CompositeNTSC, T::CompositePAL);
    EXPECT_NE(T::YC_NTSC,       T::YC_PAL);
    EXPECT_NE(T::ColourNTSC,    T::ColourPAL);
}

// =============================================================================
// ColorimetricMatrixCoefficients — enum sanity
// =============================================================================

TEST(ColorimetricMatrixCoefficientsTest, unspecified_isDistinctFromAllConcreteValues)
{
    using M = orc::ColorimetricMatrixCoefficients;
    EXPECT_NE(M::Unspecified, M::NTSC1953_FCC);
    EXPECT_NE(M::Unspecified, M::BT601_625);
    EXPECT_NE(M::Unspecified, M::BT601_525);
}

TEST(ColorimetricMatrixCoefficientsTest, ntscAndPalMatrices_areDistinct)
{
    using M = orc::ColorimetricMatrixCoefficients;
    EXPECT_NE(M::BT601_525, M::BT601_625);
}

// =============================================================================
// ColorimetricPrimaries — enum sanity
// =============================================================================

TEST(ColorimetricPrimariesTest, unspecified_isDistinctFromAllConcreteValues)
{
    using P = orc::ColorimetricPrimaries;
    EXPECT_NE(P::Unspecified, P::NTSC1953);
    EXPECT_NE(P::Unspecified, P::SMPTE_C);
    EXPECT_NE(P::Unspecified, P::EBU_BT470_PAL);
    EXPECT_NE(P::Unspecified, P::BT709);
}

TEST(ColorimetricPrimariesTest, ntscAndPalPrimaries_areDistinct)
{
    EXPECT_NE(orc::ColorimetricPrimaries::SMPTE_C,
              orc::ColorimetricPrimaries::EBU_BT470_PAL);
}

// =============================================================================
// ColorimetricTransferCharacteristics — enum sanity
// =============================================================================

TEST(ColorimetricTransferCharacteristicsTest, unspecified_isDistinctFromAllConcreteValues)
{
    using TC = orc::ColorimetricTransferCharacteristics;
    EXPECT_NE(TC::Unspecified, TC::Gamma22);
    EXPECT_NE(TC::Unspecified, TC::Gamma28);
    EXPECT_NE(TC::Unspecified, TC::BT709);
    EXPECT_NE(TC::Unspecified, TC::BT1886);
    EXPECT_NE(TC::Unspecified, TC::BT1886App1);
}

TEST(ColorimetricTransferCharacteristicsTest, ntscAndPalGammas_areDistinct)
{
    EXPECT_NE(orc::ColorimetricTransferCharacteristics::Gamma22,
              orc::ColorimetricTransferCharacteristics::Gamma28);
}

// =============================================================================
// ColorimetricMetadata — defaults
// =============================================================================

TEST(ColorimetricMetadataTest, defaultConstructed_hasUnspecifiedMatrixCoefficients)
{
    orc::ColorimetricMetadata meta{};
    EXPECT_EQ(meta.matrix_coefficients,
              orc::ColorimetricMatrixCoefficients::Unspecified);
}

TEST(ColorimetricMetadataTest, defaultConstructed_hasUnspecifiedPrimaries)
{
    orc::ColorimetricMetadata meta{};
    EXPECT_EQ(meta.primaries, orc::ColorimetricPrimaries::Unspecified);
}

TEST(ColorimetricMetadataTest, defaultConstructed_hasUnspecifiedTransferCharacteristics)
{
    orc::ColorimetricMetadata meta{};
    EXPECT_EQ(meta.transfer_characteristics,
              orc::ColorimetricTransferCharacteristics::Unspecified);
}

TEST(ColorimetricMetadataTest, defaultNtsc_hasExpectedMatrixCoefficients)
{
    auto meta = orc::ColorimetricMetadata::default_ntsc();
    EXPECT_EQ(meta.matrix_coefficients,
              orc::ColorimetricMatrixCoefficients::BT601_525);
}

TEST(ColorimetricMetadataTest, defaultNtsc_hasExpectedPrimaries)
{
    auto meta = orc::ColorimetricMetadata::default_ntsc();
    EXPECT_EQ(meta.primaries, orc::ColorimetricPrimaries::SMPTE_C);
}

TEST(ColorimetricMetadataTest, defaultNtsc_hasExpectedTransferCharacteristics)
{
    auto meta = orc::ColorimetricMetadata::default_ntsc();
    EXPECT_EQ(meta.transfer_characteristics,
              orc::ColorimetricTransferCharacteristics::Gamma22);
}

TEST(ColorimetricMetadataTest, defaultPal_hasExpectedMatrixCoefficients)
{
    auto meta = orc::ColorimetricMetadata::default_pal();
    EXPECT_EQ(meta.matrix_coefficients,
              orc::ColorimetricMatrixCoefficients::BT601_625);
}

TEST(ColorimetricMetadataTest, defaultPal_hasExpectedPrimaries)
{
    auto meta = orc::ColorimetricMetadata::default_pal();
    EXPECT_EQ(meta.primaries, orc::ColorimetricPrimaries::EBU_BT470_PAL);
}

TEST(ColorimetricMetadataTest, defaultPal_hasExpectedTransferCharacteristics)
{
    auto meta = orc::ColorimetricMetadata::default_pal();
    EXPECT_EQ(meta.transfer_characteristics,
              orc::ColorimetricTransferCharacteristics::Gamma28);
}

// =============================================================================
// ColorimetricMetadata — equality and round-trip
// =============================================================================

TEST(ColorimetricMetadataTest, defaultNtscAndDefaultPal_areNotEqual)
{
    EXPECT_NE(orc::ColorimetricMetadata::default_ntsc(),
              orc::ColorimetricMetadata::default_pal());
}

TEST(ColorimetricMetadataTest, equalityIsReflexive)
{
    auto meta = orc::ColorimetricMetadata::default_ntsc();
    EXPECT_EQ(meta, meta);
}

TEST(ColorimetricMetadataTest, copyPreservesAllFields)
{
    auto original = orc::ColorimetricMetadata::default_pal();
    auto copy = original;
    EXPECT_EQ(copy, original);
}

TEST(ColorimetricMetadataTest, modifiedMatrix_isNotEqualToOriginal)
{
    auto original = orc::ColorimetricMetadata::default_ntsc();
    auto modified = original;
    modified.matrix_coefficients = orc::ColorimetricMatrixCoefficients::NTSC1953_FCC;
    EXPECT_NE(modified, original);
}

TEST(ColorimetricMetadataTest, modifiedPrimaries_isNotEqualToOriginal)
{
    auto original = orc::ColorimetricMetadata::default_ntsc();
    auto modified = original;
    modified.primaries = orc::ColorimetricPrimaries::NTSC1953;
    EXPECT_NE(modified, original);
}

TEST(ColorimetricMetadataTest, modifiedTransfer_isNotEqualToOriginal)
{
    auto original = orc::ColorimetricMetadata::default_ntsc();
    auto modified = original;
    modified.transfer_characteristics = orc::ColorimetricTransferCharacteristics::BT709;
    EXPECT_NE(modified, original);
}

TEST(ColorimetricMetadataTest, unspecifiedInstance_isNotEqualToDefaultNtsc)
{
    orc::ColorimetricMetadata unspecified{};
    EXPECT_NE(unspecified, orc::ColorimetricMetadata::default_ntsc());
}

// =============================================================================
// PreviewCoordinate — construction and defaults
// =============================================================================

TEST(PreviewCoordinateTest, defaultConstructed_hasZeroFieldIndex)
{
    orc::PreviewCoordinate coord{};
    EXPECT_EQ(coord.field_index, 0u);
}

TEST(PreviewCoordinateTest, defaultConstructed_hasZeroLineIndex)
{
    orc::PreviewCoordinate coord{};
    EXPECT_EQ(coord.line_index, 0u);
}

TEST(PreviewCoordinateTest, defaultConstructed_hasZeroSampleOffset)
{
    orc::PreviewCoordinate coord{};
    EXPECT_EQ(coord.sample_offset, 0u);
}

TEST(PreviewCoordinateTest, defaultConstructed_hasCompositeNtscDataTypeContext)
{
    orc::PreviewCoordinate coord{};
    EXPECT_EQ(coord.data_type_context, orc::VideoDataType::CompositeNTSC);
}

// =============================================================================
// PreviewCoordinate — validity / bounds
// =============================================================================

TEST(PreviewCoordinateTest, defaultConstructed_isValid)
{
    orc::PreviewCoordinate coord{};
    EXPECT_TRUE(coord.is_valid());
}

TEST(PreviewCoordinateTest, representativeFieldIndex_isValid)
{
    orc::PreviewCoordinate coord{};
    coord.field_index = 1000u;
    EXPECT_TRUE(coord.is_valid());
}

TEST(PreviewCoordinateTest, maxFieldIndex_isNotValid)
{
    orc::PreviewCoordinate coord{};
    coord.field_index = UINT64_MAX;
    EXPECT_FALSE(coord.is_valid());
}

TEST(PreviewCoordinateTest, maxLineIndex_isNotValid)
{
    orc::PreviewCoordinate coord{};
    coord.line_index = UINT32_MAX;
    EXPECT_FALSE(coord.is_valid());
}

TEST(PreviewCoordinateTest, maxSampleOffset_isNotValid)
{
    orc::PreviewCoordinate coord{};
    coord.sample_offset = UINT32_MAX;
    EXPECT_FALSE(coord.is_valid());
}

// =============================================================================
// PreviewCoordinate — equality
// =============================================================================

TEST(PreviewCoordinateTest, equalityIsReflexive)
{
    orc::PreviewCoordinate coord{42u, 10u, 200u, orc::VideoDataType::YC_PAL};
    EXPECT_EQ(coord, coord);
}

TEST(PreviewCoordinateTest, differentFieldIndex_isNotEqual)
{
    orc::PreviewCoordinate a{1u, 10u, 0u, orc::VideoDataType::CompositeNTSC};
    orc::PreviewCoordinate b{2u, 10u, 0u, orc::VideoDataType::CompositeNTSC};
    EXPECT_NE(a, b);
}

TEST(PreviewCoordinateTest, differentLineIndex_isNotEqual)
{
    orc::PreviewCoordinate a{0u, 10u, 0u, orc::VideoDataType::CompositePAL};
    orc::PreviewCoordinate b{0u, 20u, 0u, orc::VideoDataType::CompositePAL};
    EXPECT_NE(a, b);
}

TEST(PreviewCoordinateTest, differentSampleOffset_isNotEqual)
{
    orc::PreviewCoordinate a{0u, 0u, 100u, orc::VideoDataType::YC_NTSC};
    orc::PreviewCoordinate b{0u, 0u, 200u, orc::VideoDataType::YC_NTSC};
    EXPECT_NE(a, b);
}

TEST(PreviewCoordinateTest, differentDataTypeContext_isNotEqual)
{
    orc::PreviewCoordinate a{0u, 0u, 0u, orc::VideoDataType::CompositeNTSC};
    orc::PreviewCoordinate b{0u, 0u, 0u, orc::VideoDataType::CompositePAL};
    EXPECT_NE(a, b);
}

TEST(PreviewCoordinateTest, copyPreservesAllFields)
{
    orc::PreviewCoordinate original{100u, 50u, 300u, orc::VideoDataType::ColourPAL};
    auto copy = original;
    EXPECT_EQ(copy, original);
}

// =============================================================================
// PreviewNavigationExtent — validity
// =============================================================================

TEST(PreviewNavigationExtentTest, defaultConstructed_isNotValid)
{
    orc::PreviewNavigationExtent ext{};
    EXPECT_FALSE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, withPositiveItemCountAndLabel_isValid)
{
    orc::PreviewNavigationExtent ext{100, 1, "field"};
    EXPECT_TRUE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, withZeroItemCount_isNotValid)
{
    orc::PreviewNavigationExtent ext{0, 1, "field"};
    EXPECT_FALSE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, withZeroGranularity_isNotValid)
{
    orc::PreviewNavigationExtent ext{100, 0, "field"};
    EXPECT_FALSE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, withEmptyLabel_isNotValid)
{
    orc::PreviewNavigationExtent ext{100, 1, ""};
    EXPECT_FALSE(ext.is_valid());
}

TEST(PreviewNavigationExtentTest, frameGranularity_isValid)
{
    // Frame-navigating stages expose every-other-field, so granularity == 2
    orc::PreviewNavigationExtent ext{50, 2, "frame"};
    EXPECT_TRUE(ext.is_valid());
}

// =============================================================================
// PreviewGeometry — validity
// =============================================================================

TEST(PreviewGeometryTest, defaultConstructed_isNotValid)
{
    orc::PreviewGeometry geo{};
    EXPECT_FALSE(geo.is_valid());
}

TEST(PreviewGeometryTest, withValidPalDimensions_isValid)
{
    orc::PreviewGeometry geo{910, 313, 4.0/3.0, 0.7};
    EXPECT_TRUE(geo.is_valid());
}

TEST(PreviewGeometryTest, withValidNtscDimensions_isValid)
{
    orc::PreviewGeometry geo{760, 263, 4.0/3.0, 0.7};
    EXPECT_TRUE(geo.is_valid());
}

TEST(PreviewGeometryTest, withZeroWidth_isNotValid)
{
    orc::PreviewGeometry geo{0, 313, 4.0/3.0, 0.7};
    EXPECT_FALSE(geo.is_valid());
}

TEST(PreviewGeometryTest, withZeroHeight_isNotValid)
{
    orc::PreviewGeometry geo{910, 0, 4.0/3.0, 0.7};
    EXPECT_FALSE(geo.is_valid());
}

TEST(PreviewGeometryTest, withZeroDisplayAspectRatio_isNotValid)
{
    orc::PreviewGeometry geo{910, 313, 0.0, 0.7};
    EXPECT_FALSE(geo.is_valid());
}

TEST(PreviewGeometryTest, withZeroDarCorrectionFactor_isNotValid)
{
    orc::PreviewGeometry geo{910, 313, 4.0/3.0, 0.0};
    EXPECT_FALSE(geo.is_valid());
}

// =============================================================================
// PreviewTweakableParameter — tweak class values
// =============================================================================

TEST(PreviewTweakableParameterTest, displayPhaseClass_isPreserved)
{
    orc::PreviewTweakableParameter param{"chroma_matrix", orc::PreviewTweakClass::DisplayPhase};
    EXPECT_EQ(param.parameter_name, "chroma_matrix");
    EXPECT_EQ(param.tweak_class, orc::PreviewTweakClass::DisplayPhase);
}

TEST(PreviewTweakableParameterTest, decodePhaseClass_isPreserved)
{
    orc::PreviewTweakableParameter param{"chroma_gain", orc::PreviewTweakClass::DecodePhase};
    EXPECT_EQ(param.parameter_name, "chroma_gain");
    EXPECT_EQ(param.tweak_class, orc::PreviewTweakClass::DecodePhase);
}

TEST(PreviewTweakableParameterTest, displayPhaseAndDecodePhase_areDistinct)
{
    EXPECT_NE(orc::PreviewTweakClass::DisplayPhase,
              orc::PreviewTweakClass::DecodePhase);
}

// =============================================================================
// StagePreviewCapability — schema and validity
// =============================================================================

TEST(StagePreviewCapabilityTest, defaultConstructed_isNotValid)
{
    orc::StagePreviewCapability cap{};
    EXPECT_FALSE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, withNoDataTypes_isNotValid)
{
    orc::StagePreviewCapability cap{};
    cap.navigation_extent = {100, 1, "field"};
    cap.geometry          = {910, 313, 4.0/3.0, 0.7};
    // supported_data_types is still empty
    EXPECT_FALSE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, withZeroItemCount_isNotValid)
{
    orc::StagePreviewCapability cap{};
    cap.supported_data_types = {orc::VideoDataType::CompositePAL};
    cap.navigation_extent    = {0, 1, "field"};  // item_count == 0
    cap.geometry             = {910, 313, 4.0/3.0, 0.7};
    EXPECT_FALSE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, withZeroGeometryWidth_isNotValid)
{
    orc::StagePreviewCapability cap{};
    cap.supported_data_types = {orc::VideoDataType::CompositePAL};
    cap.navigation_extent    = {100, 1, "field"};
    cap.geometry             = {0, 313, 4.0/3.0, 0.7};  // active_width == 0
    EXPECT_FALSE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, minimumValidCapability_isValid)
{
    orc::StagePreviewCapability cap{};
    cap.supported_data_types = {orc::VideoDataType::CompositePAL};
    cap.navigation_extent    = {100, 1, "field"};
    cap.geometry             = {910, 313, 4.0/3.0, 0.7};
    EXPECT_TRUE(cap.is_valid());
}

TEST(StagePreviewCapabilityTest, multipleDataTypes_isValid)
{
    orc::StagePreviewCapability cap{};
    cap.supported_data_types = {orc::VideoDataType::YC_PAL, orc::VideoDataType::ColourPAL};
    cap.navigation_extent    = {50, 1, "frame"};
    cap.geometry             = {928, 576, 4.0/3.0, 0.7};
    EXPECT_TRUE(cap.is_valid());
    EXPECT_EQ(cap.supported_data_types.size(), 2u);
}

TEST(StagePreviewCapabilityTest, emptyTweakableParameters_doesNotInvalidateCapability)
{
    orc::StagePreviewCapability cap{};
    cap.supported_data_types = {orc::VideoDataType::CompositeNTSC};
    cap.navigation_extent    = {200, 1, "field"};
    cap.geometry             = {760, 263, 4.0/3.0, 0.7};
    EXPECT_TRUE(cap.is_valid());
    EXPECT_TRUE(cap.tweakable_parameters.empty());
}

TEST(StagePreviewCapabilityTest, tweakableParameters_areIncluded)
{
    orc::StagePreviewCapability cap{};
    cap.supported_data_types   = {orc::VideoDataType::ColourNTSC, orc::VideoDataType::YC_NTSC};
    cap.navigation_extent      = {100, 1, "frame"};
    cap.geometry               = {760, 486, 4.0/3.0, 0.7};
    cap.tweakable_parameters   = {
        {"chroma_matrix", orc::PreviewTweakClass::DisplayPhase},
        {"chroma_gain",   orc::PreviewTweakClass::DecodePhase},
    };
    EXPECT_TRUE(cap.is_valid());
    EXPECT_EQ(cap.tweakable_parameters.size(), 2u);
}

// =============================================================================
// IStagePreviewCapability — interface mock smoke-test
// =============================================================================

namespace
{
    // Minimal mock to verify the interface can be implemented.
    class MockPreviewCapabilityStage : public orc::IStagePreviewCapability {
    public:
        orc::StagePreviewCapability get_preview_capability() const override
        {
            orc::StagePreviewCapability cap{};
            cap.supported_data_types = {orc::VideoDataType::CompositePAL};
            cap.navigation_extent    = {400, 1, "field"};
            cap.geometry             = {910, 313, 4.0/3.0, 0.7};
            return cap;
        }
    };
} // anonymous namespace

TEST(IStagePreviewCapabilityTest, concreteImplementation_returnsValidCapability)
{
    MockPreviewCapabilityStage stage{};
    auto cap = stage.get_preview_capability();
    EXPECT_TRUE(cap.is_valid());
}

TEST(IStagePreviewCapabilityTest, concreteImplementation_returnsExpectedDataType)
{
    MockPreviewCapabilityStage stage{};
    auto cap = stage.get_preview_capability();
    ASSERT_EQ(cap.supported_data_types.size(), 1u);
    EXPECT_EQ(cap.supported_data_types.front(), orc::VideoDataType::CompositePAL);
}

TEST(IStagePreviewCapabilityTest, concreteImplementation_returnsExpectedItemCount)
{
    MockPreviewCapabilityStage stage{};
    auto cap = stage.get_preview_capability();
    EXPECT_EQ(cap.navigation_extent.item_count, 400u);
}

} // namespace orc_unit_test
