/*
 * File:        nn_ntsc_chroma_sink_stage.h
 * Module:      orc-core
 * Purpose:     NN NTSC Chroma Sink Stage - neural network NTSC chroma decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_NN_NTSC_CHROMA_SINK_STAGE_H
#define ORC_CORE_NN_NTSC_CHROMA_SINK_STAGE_H

#include "dag_executor.h"
#include "stage_parameter.h"
#include <node_type.h>
#include "video_field_representation.h"
#include "../triggerable_stage.h"
#include "previewable_stage.h"
#include "stage_preview_capability.h"
#include "colour_preview_provider.h"
#include <string>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>

// Forward declaration needed for PreviewDecoderCache unique_ptr member
namespace orc::nn_ntsc { class NtscDecoder; }

namespace orc {

/**
 * @brief NN NTSC Chroma Sink Stage
 *
 * Neural network-based NTSC chroma decoder using a fully 3D
 * (spatial × spatial × temporal) comb-filter approach. Outputs raw
 * RGB/YUV/Y4M video files for NTSC composite and Y/C sources.
 *
 * Stage category: SinkCategory::THIRD_PARTY — isolated from CORE chroma
 * decoders so the NN approach can be validated independently.
 *
 * Parameters:
 * - output_path:           Output file path (.rgb / .yuv / .y4m)
 * - output_format:         Output format: rgb, yuv, y4m (default: rgb)
 * - threads:               Worker threads; 0 = auto, capped at 4 (default: 0)
 * - chroma_gain:           Chroma gain factor (default: 1.0)
 * - chroma_phase:          Chroma phase rotation in degrees (default: 0.0)
 * - deterministic_output:  Force single-threaded reproducible output
 *                          (default: false)
 * - chroma_nr:             Chroma noise reduction level (default: 0.0)
 * - luma_nr:               Luma noise reduction level (default: 0.0)
 *
 * Video system:  NTSC only — PAL/PAL-M inputs are rejected at execute-time.
 * Preview:       Frame preview supported via IColourPreviewProvider; the NN
 *                decoder cache avoids re-creating FFTW plans and the ONNX
 *                session on each seek.
 */
class NnNtscChromaSinkStage : public DAGStage,
                               public ParameterizedStage,
                               public TriggerableStage,
                               public PreviewableStage,
                               public IStagePreviewCapability,
                               public IColourPreviewProvider {
public:
    NnNtscChromaSinkStage();
    ~NnNtscChromaSinkStage() override;  // Non-trivial: preview cache holds unique_ptr to incomplete type

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
    std::vector<ParameterDescriptor> get_parameter_descriptors(
        VideoSystem project_format = VideoSystem::Unknown,
        SourceType source_type = SourceType::Unknown) const override;
    std::map<std::string, ParameterValue> get_parameters() const override;
    bool set_parameters(const std::map<std::string, ParameterValue>& params) override;

    // TriggerableStage interface
    bool trigger(
        const std::vector<ArtifactPtr>& inputs,
        const std::map<std::string, ParameterValue>& parameters,
        IObservationContext& observation_context) override;

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

    // IStagePreviewCapability interface
    StagePreviewCapability get_preview_capability() const override;

    // IColourPreviewProvider interface
    std::optional<ColourFrameCarrier> get_colour_preview_carrier(
        uint64_t frame_index,
        PreviewNavigationHint hint = PreviewNavigationHint::Random) const override;

private:
    // Parameters (stored as individual members for clarity)
    std::string output_path_;
    std::string output_format_{"rgb"};
    int32_t threads_{0};
    double chroma_gain_{1.0};
    double chroma_phase_{0.0};
    bool deterministic_output_{false};
    double chroma_nr_{0.0};
    double luma_nr_{0.0};

    std::string trigger_status_;
    mutable std::mutex cached_input_mutex_;  // Protects cached_input_ from race conditions
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_input_;
    TriggerProgressCallback progress_callback_;
    std::atomic<bool> is_processing_{false};
    std::atomic<bool> cancel_requested_{false};

    // Preview decoder cache — avoids recreating FFTW plans and ONNX session on each seek
    struct PreviewDecoderCache {
        double chroma_gain{-9999.0};   // Sentinel: forces rebuild on first use
        double chroma_phase{-9999.0};
        double chroma_nr{-9999.0};
        double luma_nr{-9999.0};
        std::unique_ptr<nn_ntsc::NtscDecoder> decoder;

        bool matches_config(double cg, double cp, double cn, double ln) const {
            return decoder != nullptr
                && chroma_gain  == cg
                && chroma_phase == cp
                && chroma_nr    == cn
                && luma_nr      == ln;
        }
    };
    mutable PreviewDecoderCache preview_decoder_cache_;
};

} // namespace orc

// Force linker to include this object file
namespace orc { void force_link_NnNtscChromaSinkStage(); }

#endif // ORC_CORE_NN_NTSC_CHROMA_SINK_STAGE_H
