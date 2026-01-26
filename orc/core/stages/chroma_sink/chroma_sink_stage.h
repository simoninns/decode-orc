/*
 * File:        chroma_sink_stage.h
 * Module:      orc-core
 * Purpose:     Chroma decoder sink stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#ifndef ORC_CORE_CHROMA_SINK_STAGE_H
#define ORC_CORE_CHROMA_SINK_STAGE_H

#if defined(ORC_GUI_BUILD)
#error "GUI code cannot include core/stages/chroma_sink/chroma_sink_stage.h. Use VectorscopePresenter or RenderPresenter instead."
#endif

#include "dag_executor.h"
#include "stage_parameter.h"
#include <node_type.h>
#include "video_field_representation.h"
#include "tbc_metadata.h"
#include "previewable_stage.h"
#include "preview_renderer.h"  // For PreviewImage definition
#include "../ld_sink/ld_sink_stage.h"  // For TriggerableStage interface

#include <atomic>
#include <thread>

// Forward declarations for decoder classes
struct SourceField;
class Decoder;
class ComponentFrame;
class MonoDecoder;
class PalColour;
class Comb;

#include <string>
#include <memory>
#include <vector>

namespace orc {

/**
 * @brief Chroma Decoder Sink Stage
 * 
 * Decodes composite PAL or NTSC video into component RGB or YUV output.
 * This is a SINK stage - it has inputs but no outputs.
 * 
 * When triggered, it reads all fields from its input and decodes them using
 * the selected chroma decoder, writing the result to an output file.
 * 
 * Optionally, can embed analogue audio into the output file (MP4/MKV formats only)
 * if the input contains PCM audio data (requires pcm_path set in source stage).
 * 
 * Supported Decoders:
 * - PAL: pal2d, transform2d, transform3d
 * - NTSC: ntsc1d, ntsc2d, ntsc3d, ntsc3dnoadapt
 * - Other: mono, auto
 * 
 * This sink supports preview - it decodes fields on-demand for GUI visualization.
 * 
 * Parameters:
 * - output_path: Output file path for video
 * - decoder_type: Which decoder to use (auto, pal2d, ntsc2d, etc.)
 * - output_format: Output format (rgb, yuv, y4m, mp4-h264, mkv-ffv1)
 * - chroma_gain: Chroma gain factor (0.0-10.0, default 1.0)
 * - chroma_phase: Chroma phase rotation in degrees (-180 to 180, default 0)
 * - start_frame: Optional start frame number
 * - length: Optional number of frames to process
 * - reverse_fields: Reverse field order (default: false)
 * - embed_audio: Embed analogue audio in output (MP4/MKV only, default: false)
 */
class ChromaSinkStage : public DAGStage, 
                       public ParameterizedStage, 
                       public TriggerableStage, 
                       public PreviewableStage {
public:
    ChromaSinkStage();
    ~ChromaSinkStage() override;  // Need custom destructor for unique_ptr with incomplete types
    
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
        ObservationContext& observation_context
    ) override;
    
    std::string get_trigger_status() const override;
    
    void set_progress_callback(TriggerProgressCallback callback) override {
        progress_callback_ = callback;
    }
    
    bool is_trigger_in_progress() const override {
        return trigger_in_progress_.load();
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
    mutable std::mutex cached_input_mutex_;  // Protects cached_input_ from race conditions
    mutable std::shared_ptr<const VideoFieldRepresentation> cached_input_;  // For preview
    
    // Cached decoder for preview (avoid recreating expensive FFTW plans)
    struct PreviewDecoderCache {
        std::string decoder_type;
        double chroma_gain;
        double chroma_phase;
        double luma_nr;
        double chroma_nr;
        bool ntsc_phase_comp;
        bool simple_pal;
        bool blackandwhite;
        
        std::unique_ptr<MonoDecoder> mono_decoder;
        std::unique_ptr<PalColour> pal_decoder;
        std::unique_ptr<Comb> ntsc_decoder;
        
        bool matches_config(const std::string& dec_type, double cg, double cp, 
                           double ln, double cn, bool npc, bool sp, bool bw) const {
            return decoder_type == dec_type && chroma_gain == cg && 
                   chroma_phase == cp && luma_nr == ln && chroma_nr == cn &&
                   ntsc_phase_comp == npc && simple_pal == sp && blackandwhite == bw;
        }
    };
    mutable PreviewDecoderCache preview_decoder_cache_;
    
    // Current parameters
    std::string output_path_;
    std::string decoder_type_;
    std::string output_format_;
    double chroma_gain_;
    double chroma_phase_;
    int threads_;
    double luma_nr_;
    double chroma_nr_;
    bool ntsc_phase_comp_;
    bool simple_pal_;
    int output_padding_;
    bool embed_audio_;  // Embed analogue audio in output (MP4/MKV only)
    bool embed_closed_captions_;  // Embed closed captions in MP4 output (MP4 only, converted to mov_text)
    
    // Encoder quality parameters (for FFmpeg output)
    std::string encoder_preset_;   // "fast", "medium", "slow", "veryslow"
    int encoder_crf_;              // 0-51, typically 18-28 for good quality
    int encoder_bitrate_;          // bits per second, 0 = use CRF instead
    
    // Status tracking
    std::string trigger_status_;
    std::atomic<bool> trigger_in_progress_{false};
    std::atomic<bool> cancel_requested_{false};
    TriggerProgressCallback progress_callback_;
    
    // Helper methods for integration
    SourceField convertToSourceField(
        const VideoFieldRepresentation* vfr,
        FieldID field_id
    ) const;
    
    bool writeOutputFile(
        const std::string& output_path,
        const std::string& format,
        const std::vector<ComponentFrame>& frames,
        const void* videoParams,
        const VideoFieldRepresentation* vfr,
        uint64_t start_field_index,
        uint64_t num_fields,
        std::string& error_message  // Output parameter for detailed error
    ) const;
};

} // namespace orc

#endif // ORC_CORE_CHROMA_SINK_STAGE_H
