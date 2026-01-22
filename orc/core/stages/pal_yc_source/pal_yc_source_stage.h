/*
 * File:        pal_yc_source_stage.h
 * Module:      orc-core
 * Purpose:     PAL YC source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#ifndef PAL_YC_SOURCE_STAGE_H
#define PAL_YC_SOURCE_STAGE_H

#include <dag_executor.h>
#include <tbc_yc_video_field_representation.h>
#include <stage_parameter.h>
#include <previewable_stage.h>
#include <string>

namespace orc {

/**
 * @brief PAL YC Source Stage - Loads PAL YC (separate Y and C) files
 * 
 * This stage loads separate Y (luma) and C (chroma) TBC files for PAL video,
 * creating a VideoFieldRepresentation for PAL YC video processing.
 * 
 * YC sources are typically from color-under formats like VHS or Betamax,
 * where Y and C are recorded separately. This provides better quality
 * than composite sources:
 * - Clean luma (no comb filter artifacts)
 * - Simpler chroma decoding (no Y/C separation needed)
 * 
 * Parameters:
 * - y_path: Path to the .tbcy (luma) file
 * - c_path: Path to the .tbcc (chroma) file
 * - db_path: Path to the .tbc.db database file
 * - pcm_path: Optional path to .pcm audio file
 * - efm_path: Optional path to .efm EFM data file
 * 
 * This is a source stage with no inputs.
 */
class PALYCSourceStage : public DAGStage, public ParameterizedStage, public PreviewableStage {
public:
    PALYCSourceStage() = default;
    ~PALYCSourceStage() override = default;

    // DAGStage interface
    std::string version() const override { return "1.0.0"; }
    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::SOURCE,
            "PAL_YC_Source",
            "PAL YC Source",
            "PAL YC input source - loads separate Y and C TBC files (color-under formats like VHS)",
            0, 0,  // No inputs
            1, UINT32_MAX,  // Many outputs
            VideoFormatCompatibility::PAL_ONLY
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context) override;
    
    size_t required_input_count() const override { return 0; }  // Source has no inputs
    size_t output_count() const override { return 1; }

    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown, SourceType source_type = SourceType::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
    // PreviewableStage interface
    bool supports_preview() const override;
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(const std::string& option_id, uint64_t index,
                               PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;
    
    // Stage inspection
    std::optional<StageReport> generate_report() const override;

private:
    // Cache the loaded representation to avoid reloading
    mutable std::string cached_y_path_;
    mutable std::string cached_c_path_;
    mutable std::shared_ptr<VideoFieldRepresentation> cached_representation_;
    
    // Current parameters
    std::string y_path_;
    std::string c_path_;
    std::string db_path_;
    std::string pcm_path_;
    std::string efm_path_;
};

} // namespace orc

#endif // PAL_YC_SOURCE_STAGE_H
