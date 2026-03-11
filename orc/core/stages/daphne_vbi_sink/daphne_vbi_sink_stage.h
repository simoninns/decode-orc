/*
 * File:        daphne_vbi_sink_stage.h
 * Module:      orc-core
 * Purpose:     Generate .VBI binary files
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Matt Ownby
 */

#ifndef ORC_CORE_VBI_SINK_STAGE_H
#define ORC_CORE_VBI_SINK_STAGE_H

#include "dag_executor.h"
#include "stage_parameter.h"
#include <node_type.h>
#include "video_field_representation.h"
#include "../triggerable_stage.h"
#include "observation_context.h"
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <utility>
#include "../../factories_interface.h"

namespace orc
{

/**
 * @brief .VBI binary Sink Stage
 *
 * Writes VBI data from each field to a binary file according to the specification at https://www.daphne-emu.com:9443/mediawiki/index.php/VBIInfo .
 * This is a SINK stage - it has inputs but no outputs.
 *
 * When triggered, it reads all fields from its input and writes them to:
 * - .vbi file: Binary data.
 *
 * This sink does not support preview.
 *
 * Parameters:
 * - output_path: Output file path
 */
class DaphneVBISinkStage : public DAGStage, public ParameterizedStage, public TriggerableStage {
public:
    DaphneVBISinkStage(std::shared_ptr<IFactories> factories) : factories_(std::move(factories)) {}
    ~DaphneVBISinkStage() override = default;

    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override;

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
        ObservationContext& observation_context
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

private:
    std::string output_path_;
    std::string trigger_status_;
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_input_;  // For preview
    TriggerProgressCallback progress_callback_;  // Progress callback for trigger operations
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};
    std::shared_ptr<IFactories> factories_;
};
} // orc

#endif //ORC_CORE_VBI_SINK_STAGE_H