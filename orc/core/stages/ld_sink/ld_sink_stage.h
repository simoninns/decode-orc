/*
 * File:        ld_sink_stage.h
 * Module:      orc-core
 * Purpose:     ld-decode sink Stage - writes TBC and metadata to disk
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_LD_SINK_STAGE_H
#define ORC_CORE_LD_SINK_STAGE_H

#include "dag_executor.h"
#include "stage_parameter.h"
#include <node_type.h>
#include "video_field_representation.h"
#include <tbc_metadata.h>
#include "../triggerable_stage.h"
#include "previewable_stage.h"
#include "observation_context.h"
#include "observation_schema.h"
#include <string>
#include <memory>
#include <functional>
#include <atomic>

namespace orc {

/**
 * @brief ld-decode Sink Stage
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
 * - output_path: Output file path (metadata will be output_path + ".db")
 */
class LDSinkStage : public DAGStage, public ParameterizedStage, public TriggerableStage, public PreviewableStage {
public:
    LDSinkStage();
    ~LDSinkStage() override = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override;
    std::vector<ObservationKey> get_provided_observations() const override {
        return {
            ObservationKey{"export", "seq_no", ObservationType::INT64, "1-based sequence number for field", false},
            ObservationKey{"export", "is_first_field", ObservationType::BOOL, "Field parity: first field (true) or second (false)", true}
        };
    }
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context
    ) override;
    
    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 0; }  // Sink has no outputs
    
    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(VideoSystem project_format = VideoSystem::Unknown, SourceType source_type = SourceType::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;
    
    // TriggerableStage interface
    bool trigger(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        IObservationContext& observation_context
    ) override;
    
    std::string get_trigger_status() const override;
    
    void set_progress_callback(TriggerProgressCallback callback) override {
        progress_callback_ = callback;
    }
    
    bool is_trigger_in_progress() const override {
        return is_processing_.load();
    }
    
    void cancel_trigger() override {
        cancel_requested_.store(true);
    }
    
    // PreviewableStage interface
    bool supports_preview() const override { return true; }
    std::vector<PreviewOption> get_preview_options() const override;
    PreviewImage render_preview(const std::string& option_id, uint64_t index,
                               PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;
    
private:
    std::string output_path_;
    std::string trigger_status_;
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_input_;  // For preview
    TriggerProgressCallback progress_callback_;  // Progress callback for trigger operations
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};
    
    // Helper methods
    bool write_tbc_and_metadata(
        const VideoFieldRepresentation* representation,
        const std::string& tbc_path,
        IObservationContext& observation_context
    );
};

} // namespace orc

#endif // ORC_CORE_LD_SINK_STAGE_H
