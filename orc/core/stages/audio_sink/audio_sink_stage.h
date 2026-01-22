/*
 * File:        audio_sink_stage.h
 * Module:      orc-core
 * Purpose:     Analogue Audio Sink Stage - writes PCM audio to WAV file
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_AUDIO_SINK_STAGE_H
#define ORC_CORE_AUDIO_SINK_STAGE_H

#include "dag_executor.h"
#include "stage_parameter.h"
#include <node_type.h>
#include "video_field_representation.h"
#include "../ld_sink/ld_sink_stage.h"  // For TriggerableStage interface
#include <string>
#include <memory>
#include <functional>
#include <atomic>

namespace orc {

/**
 * @brief Analogue Audio Sink Stage
 * 
 * Extracts PCM audio samples from TBC metadata and writes them to a WAV file.
 * This is a SINK stage - it has inputs but no outputs.
 * 
 * The audio data flows through the VideoFieldRepresentation from the source stage,
 * which reads the .pcm file (if specified in the source stage parameters).
 * 
 * The audio format is:
 * - Raw 16-bit signed integer PCM
 * - Little endian
 * - 2 channels (stereo)
 * - 44,100 Hz sample rate
 * 
 * This stage extracts the audio data from the VFR and writes it to a standard
 * WAV file with proper RIFF headers.
 * 
 * Parameters:
 * - output_path: Output WAV file path
 */
class AudioSinkStage : public DAGStage, 
                      public ParameterizedStage, 
                      public TriggerableStage {
public:
    AudioSinkStage();
    ~AudioSinkStage() override = default;
    
    // DAGStage interface
    std::string version() const override { return "1.0"; }
    NodeTypeInfo get_node_type_info() const override;
    
    std::vector<ArtifactPtr> execute(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        ObservationContext& observation_context) override;
    
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
    // Helper to write WAV file header
    bool write_wav_header(std::ofstream& out, uint32_t num_samples, uint32_t sample_rate, uint16_t num_channels, uint16_t bits_per_sample);
    
    // Store parameters for inspection
    std::map<std::string, ParameterValue> parameters_;
    
    // Progress tracking
    TriggerProgressCallback progress_callback_;
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};
    std::string last_status_;
};

} // namespace orc

#endif // ORC_CORE_AUDIO_SINK_STAGE_H
