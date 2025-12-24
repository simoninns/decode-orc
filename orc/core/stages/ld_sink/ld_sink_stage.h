/*
 * File:        ld_sink_stage.h
 * Module:      orc-core
 * Purpose:     LaserDisc Sink Stage - writes TBC and metadata to disk
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_LD_SINK_STAGE_H
#define ORC_CORE_LD_SINK_STAGE_H

#include "dag_executor.h"
#include "stage_parameter.h"
#include "node_type.h"
#include "video_field_representation.h"
#include "tbc_metadata.h"
#include "previewable_stage.h"
#include <string>
#include <memory>

namespace orc {

/**
 * @brief Triggerable interface for stages that can be manually executed
 * 
 * Stages that implement this interface can be triggered from the GUI,
 * causing them to process their entire input range and perform their action.
 */
class TriggerableStage {
public:
    virtual ~TriggerableStage() = default;
    
    /**
     * @brief Trigger the stage to process its input
     * 
     * For sinks, this means reading all fields from input and writing to output file.
     * 
     * @param inputs Input artifacts (typically one VideoFieldRepresentation)
     * @param parameters Stage parameters
     * @return True if trigger succeeded, false otherwise
     */
    virtual bool trigger(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters
    ) = 0;
    
    /**
     * @brief Get status message after trigger
     * @return Status message describing what was done
     */
    virtual std::string get_trigger_status() const = 0;
};

/**
 * @brief LaserDisc Sink Stage
 * 
 * Writes TBC fields and metadata to disk in format compatible with legacy tools.
 * This is a SINK stage - it has inputs but no outputs.
 * 
 * When triggered, it reads all fields from its input and writes them to:
 * - TBC file: Raw field data
 * - .db file: Metadata including all observations and hints
 * 
 * This sink supports preview - it shows what will be written to disk.
 * 
 * Parameters:
 * - tbc_path: Output file path (metadata will be tbc_path + ".db")
 */
class LDSinkStage : public DAGStage, public ParameterizedStage, public TriggerableStage, public PreviewableStage {
public:
    LDSinkStage();
    ~LDSinkStage() override = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override;
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters
    ) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 0; }  // Sink has no outputs
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors() const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
    // TriggerableStage interface
    bool trigger(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters
    ) override;
    
    std::string get_trigger_status() const override;
    
    // PreviewableStage interface
    bool supports_preview() const override { return true; }
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(const std::string& option_id, uint64_t index,
                               PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;
    
private:
    std::string tbc_path_;
    std::string trigger_status_;
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_input_;  // For preview
    
    // Helper methods
    bool write_tbc_and_metadata(
        const VideoFieldRepresentation* representation,
        const std::string& tbc_path
    );
    
    bool write_tbc_file(
        const VideoFieldRepresentation* representation,
        const std::string& tbc_path
    );
    
    bool write_metadata_file(
        const VideoFieldRepresentation* representation,
        const std::string& db_path
    );
};

} // namespace orc

#endif // ORC_CORE_LD_SINK_STAGE_H
