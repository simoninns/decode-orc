/*
 * File:        hackdac_sink_stage.h
 * Module:      orc-core
 * Purpose:     Hackdac sink stage - writes signed 16-bit field data without half-line padding
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_CORE_HACKDAC_SINK_STAGE_H
#define ORC_CORE_HACKDAC_SINK_STAGE_H

#include "dag_executor.h"
#include "stage_parameter.h"
#include "node_type.h"
#include "video_field_representation.h"
#include "../ld_sink/ld_sink_stage.h"  // For TriggerableStage interface
#include <atomic>
#include <map>
#include <optional>
#include <string>

namespace orc {

/**
 * @brief Hackdac Sink Stage
 *
 * Exports raw video field samples as signed 16-bit values with the
 * half-line padding removed (4fsc aligned). Also emits a text report
 * describing levels and format.
 */
class HackdacSinkStage : public DAGStage,
                         public ParameterizedStage,
                         public TriggerableStage {
public:
    HackdacSinkStage();
    ~HackdacSinkStage() override = default;

    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override;

    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;

    size_t required_input_count() const override { return 1; }
    size_t output_count() const override { return 0; }

    // ParameterizedStage interface
    std::vector<ParameterDescriptor> get_parameter_descriptors(
        VideoSystem project_format = VideoSystem::Unknown,
        SourceType source_type = SourceType::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

    // TriggerableStage interface
    bool trigger(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters) override;

    std::string get_trigger_status() const override;

    void set_progress_callback(TriggerProgressCallback callback) override { progress_callback_ = callback; }
    bool is_trigger_in_progress() const override { return is_processing_.load(); }
    void cancel_trigger() override { cancel_requested_.store(true); }

private:
    struct ParsedConfig {
        std::string output_path;
        std::string report_path;
    };

    ParsedConfig parse_config(const std::map<std::string, ParameterValue>& parameters) const;

    bool write_report(const std::string& report_path,
                      VideoSystem resolved_system,
                      size_t input_line_width,
                      size_t input_line_count,
                      size_t half_line_samples,
                      size_t output_samples_per_field,
                      size_t processed_fields,
                      const std::optional<VideoParameters>& video_params) const;

    static std::string system_to_string(VideoSystem system);
    static int16_t to_signed_sample(uint16_t sample);

    std::map<std::string, ParameterValue> parameters_;
    TriggerProgressCallback progress_callback_;
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};
    std::string last_status_;
};

} // namespace orc

// Force linker to include this object file
namespace orc { void force_link_HackdacSinkStage(); }

#endif // ORC_CORE_HACKDAC_SINK_STAGE_H
