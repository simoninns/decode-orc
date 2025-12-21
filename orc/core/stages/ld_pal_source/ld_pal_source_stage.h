/*
 * File:        ld_pal_source_stage.h
 * Module:      orc-core
 * Purpose:     LaserDisc PAL source loading stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#ifndef LD_PAL_SOURCE_STAGE_H
#define LD_PAL_SOURCE_STAGE_H

#include <dag_executor.h>
#include <tbc_video_field_representation.h>
#include <stage_parameter.h>
#include <string>

namespace orc {

/**
 * @brief LaserDisc PAL Source Stage - Loads PAL TBC files from ld-decode
 * 
 * This stage loads a PAL TBC file and its associated database from ld-decode,
 * creating a VideoFieldRepresentation for PAL video processing.
 * 
 * Parameters:
 * - tbc_path: Path to the .tbc file
 * - db_path: Path to the .tbc.db database file (optional, defaults to tbc_path + ".db")
 * 
 * This is a source stage with no inputs.
 */
class LDPALSourceStage : public DAGStage, public ParameterizedStage {
public:
    LDPALSourceStage() = default;
    ~LDPALSourceStage() override = default;

    // DAGStage interface
    std::string version() const override { return "1.0.0"; }
    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::SOURCE,
            "LDPALSource",
            "LD PAL Source",
            "LaserDisc PAL input source - loads PAL TBC files from ld-decode",
            0, 0,  // No inputs
            1, 1   // Exactly one output
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters
    ) override;
    
    size_t required_input_count() const override { return 0; }  // Source has no inputs
    size_t output_count() const override { return 1; }

    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
    // Stage inspection
    std::optional<StageReport> generate_report() const override;

private:
    // Cache the loaded representation to avoid reloading
    mutable std::string cached_tbc_path_;
    mutable std::shared_ptr<TBCVideoFieldRepresentation> cached_representation_;
    
    // Store parameters for inspection
    std::map<std::string, ParameterValue> parameters_;
};

} // namespace orc

#endif // LD_PAL_SOURCE_STAGE_H
