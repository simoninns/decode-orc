/*
 * File:        ld_ntsc_source_stage.h
 * Module:      orc-core
 * Purpose:     LaserDisc NTSC source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#ifndef LD_NTSC_SOURCE_STAGE_H
#define LD_NTSC_SOURCE_STAGE_H

#include <dag_executor.h>
#include <tbc_video_field_representation.h>
#include <stage_parameter.h>
#include <previewable_stage.h>
#include <string>

namespace orc {

/**
 * @brief LaserDisc NTSC Source Stage - Loads NTSC TBC files from ld-decode
 * 
 * This stage loads an NTSC TBC file and its associated database from ld-decode,
 * creating a VideoFieldRepresentation for NTSC video processing.
 * 
 * Parameters:
 * - input_path: Path to the .tbc file
 * - db_path: Path to the .tbc.db database file (optional, defaults to input_path + ".db")
 * 
 * This is a source stage with no inputs.
 */
class LDNTSCSourceStage : public DAGStage, public ParameterizedStage, public PreviewableStage {
public:
    LDNTSCSourceStage() = default;
    ~LDNTSCSourceStage() override = default;

    // DAGStage interface
    std::string version() const override { return "1.0.0"; }
    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::SOURCE,
            "LDNTSCSource",
            "LD NTSC Source",
            "LaserDisc NTSC input source - loads NTSC TBC files from ld-decode",
            0, 0,  // No inputs
            1, UINT32_MAX,  // Many outputs
            VideoFormatCompatibility::NTSC_ONLY
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters
    ) override;
    
    size_t required_input_count() const override { return 0; }  // Source has no inputs
    size_t output_count() const override { return 1; }

    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
    // Stage inspection
    std::optional<StageReport> generate_report() const override;
    
    // PreviewableStage interface
    bool supports_preview() const override { return true; }
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(const std::string& option_id, uint64_t index,
                               PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;

private:
    // Cache the loaded representation to avoid reloading
    mutable std::string cached_input_path_;
    mutable std::shared_ptr<TBCVideoFieldRepresentation> cached_representation_;
    
    // Store parameters for inspection
    std::map<std::string, ParameterValue> parameters_;
};

} // namespace orc

#endif // LD_NTSC_SOURCE_STAGE_H
