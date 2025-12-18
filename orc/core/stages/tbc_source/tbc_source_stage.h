/******************************************************************************
 * tbc_source_stage.h
 *
 * TBC Source Stage - Loads TBC files as source input
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#ifndef TBC_SOURCE_STAGE_H
#define TBC_SOURCE_STAGE_H

#include <dag_executor.h>
#include <tbc_video_field_representation.h>
#include <string>

namespace orc {

/**
 * @brief TBC Source Stage - Loads TBC files as DAG input
 * 
 * This stage loads a TBC file and its associated database, creating
 * a VideoFieldRepresentation that can be used as input to the processing DAG.
 * 
 * Parameters:
 * - tbc_path: Path to the .tbc file
 * - db_path: Path to the .tbc.json database file (optional, defaults to tbc_path + ".json")
 * 
 * This is a source stage with no inputs.
 */
class TBCSourceStage : public DAGStage {
public:
    TBCSourceStage() = default;
    ~TBCSourceStage() override = default;

    // DAGStage interface
    std::string version() const override { return "1.0.0"; }
    
    NodeTypeInfo get_node_type_info() const override {
        return NodeTypeInfo{
            NodeType::SOURCE,
            "TBCSource",
            "TBC Source",
            "TBC input source - loads TBC files with metadata",
            0, 0,  // No inputs
            1, 1,  // Exactly one output
            false  // Not user-creatable
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, std::string>& parameters
    ) override;
    
    size_t required_input_count() const override { return 0; }  // Source has no inputs
    size_t output_count() const override { return 1; }

private:
    // Cache the loaded representation to avoid reloading
    mutable std::string cached_tbc_path_;
    mutable std::shared_ptr<TBCVideoFieldRepresentation> cached_representation_;
};

} // namespace orc

#endif // TBC_SOURCE_STAGE_H
