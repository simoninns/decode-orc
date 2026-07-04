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

#include <orc/plugin/orc_stage_runtime.h>
#include <orc/stage/node_type.h>
#include <orc/stage/stage_parameter.h>
#include <orc/stage/triggerable_stage.h>
#include <orc/stage/video_frame_representation.h>

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace orc {

class IAudioSinkStageDeps;

// Allowed values for the sample_rate_mode parameter (NTSC/PAL-M projects).
inline constexpr const char* kSampleRateModeLocked = "locked_44056";
inline constexpr const char* kSampleRateModeFreeRunning = "free_running_44100";

/**
 * @brief Analogue Audio Sink Stage
 *
 * Extracts PCM audio samples from TBC metadata and writes them to a WAV file.
 * This is a SINK stage - it has inputs but no outputs.
 *
 * The audio data flows through the VideoFrameRepresentation from the source
 * stage, which reads the .pcm file (if specified in the source stage
 * parameters).
 *
 * The audio format is:
 * - Raw 16-bit signed integer PCM
 * - Little endian
 * - 2 channels (stereo)
 * - PAL: 44,100 Hz; NTSC/PAL-M: frame-locked 44100000/1001 Hz ≈ 44,055.94 Hz
 *
 * This stage extracts the audio data from the VFR and writes it to a standard
 * WAV file with proper RIFF headers. For NTSC/PAL-M projects the
 * sample_rate_mode parameter selects between writing the frame-locked samples
 * unmodified (header rate 44,056 Hz) and resampling to standard free-running
 * 44,100 Hz.
 *
 * Parameters:
 * - output_path: Output WAV file path
 * - sample_rate_mode: locked_44056 (default) or free_running_44100
 *   (NTSC/PAL-M projects only)
 */
class AudioSinkStage : public DAGStage,
                       public ParameterizedStage,
                       public TriggerableStage {
 public:
  AudioSinkStage();
  /// Testing seam: inject a pre-built deps instance to substitute concrete dep
  /// creation in trigger().
  void set_deps_override(std::shared_ptr<IAudioSinkStageDeps> deps) {
    deps_override_ = std::move(deps);
  }
  ~AudioSinkStage() override = default;

  // DAGStage interface
  std::string version() const override { return "1.0"; }
  ORC_STAGE_INSTRUCTIONS_MD
  NodeTypeInfo get_node_type_info() const override;

  std::vector<ArtifactPtr> execute(
      const std::vector<ArtifactPtr>& inputs,
      const std::map<std::string, ParameterValue>& parameters,
      ObservationContext& observation_context) override;

  size_t required_input_count() const override { return 1; }
  size_t output_count() const override { return 0; }  // Sink has no outputs

  // ParameterizedStage interface
  std::vector<ParameterDescriptor> get_parameter_descriptors(
      VideoSystem project_format, SourceType source_type) const override;
  using ParameterizedStage::get_parameter_descriptors;
  std::map<std::string, ParameterValue> get_parameters() const override;
  bool set_parameters(
      const std::map<std::string, ParameterValue>& params) override;

  // TriggerableStage interface
  bool trigger(const std::vector<ArtifactPtr>& inputs,
               const std::map<std::string, ParameterValue>& parameters,
               IObservationContext& observation_context) override;

  std::string get_trigger_status() const override;

  void set_progress_callback(TriggerProgressCallback callback) override {
    progress_callback_ = callback;
  }

  bool is_trigger_in_progress() const override { return is_processing_.load(); }

  void cancel_trigger() override { cancel_requested_.store(true); }

 private:
  // Store parameters for inspection
  std::map<std::string, ParameterValue> parameters_;

  // Progress tracking
  TriggerProgressCallback progress_callback_;
  std::atomic<bool> is_processing_{false};
  std::atomic<bool> cancel_requested_{false};
  std::string last_status_;
  std::shared_ptr<IAudioSinkStageDeps> deps_override_;
};

}  // namespace orc

#endif  // ORC_CORE_AUDIO_SINK_STAGE_H
