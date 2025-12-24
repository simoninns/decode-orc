/*
 * File:        chroma_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Chroma decoder sink stage
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "chroma_sink_stage.h"
#include "stage_registry.h"
#include "logging.h"
#include "preview_renderer.h"
#include "preview_helpers.h"

// Decoder includes (relative to this file)
#include "decoders/sourcefield.h"
#include "decoders/monodecoder.h"
#include "decoders/palcolour.h"
#include "decoders/comb.h"
#include "decoders/outputwriter.h"
#include "decoders/componentframe.h"

#include <fstream>
#include <chrono>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>

namespace orc {

// Register stage with registry
static StageRegistration reg([]() {
    return std::make_shared<ChromaSinkStage>();
});

ChromaSinkStage::ChromaSinkStage()
    : output_path_("")
    , decoder_type_("auto")
    , output_format_("rgb")
    , chroma_gain_(1.0)
    , chroma_phase_(0.0)
    , start_frame_(1)
    , length_(-1)
    , threads_(0)  // 0 means auto-detect
    , luma_nr_(0.0)
    , chroma_nr_(0.0)
    , ntsc_phase_comp_(false)
    , simple_pal_(false)
    , output_padding_(8)
    , first_active_frame_line_(-1)
    , last_active_frame_line_(-1)
{
}

ChromaSinkStage::~ChromaSinkStage() {
}

NodeTypeInfo ChromaSinkStage::get_node_type_info() const
{
    return NodeTypeInfo{
        NodeType::SINK,
        "chroma_sink",
        "Chroma Decoder Sink",
        "Decodes composite video to RGB/YUV. Supports PAL and NTSC decoders. Trigger to export.",
        1,  // min_inputs
        1,  // max_inputs
        0,  // min_outputs
        0   // max_outputs
    };
}

std::vector<ArtifactPtr> ChromaSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters [[maybe_unused]])
{
    // Cache input for preview rendering
    if (!inputs.empty()) {
        cached_input_ = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    }
    
    // Sink stages don't produce outputs during normal execution
    // They are triggered manually to write data
    ORC_LOG_DEBUG("ChromaSink execute called (cached input for preview)");
    return {};  // No outputs
}

std::vector<ParameterDescriptor> ChromaSinkStage::get_parameter_descriptors() const
{
    // Determine available decoder types based on input video system
    std::vector<std::string> decoder_options;
    
    if (cached_input_) {
        auto video_params = cached_input_->get_video_parameters();
        if (video_params) {
            if (video_params->system == VideoSystem::PAL || video_params->system == VideoSystem::PAL_M) {
                // PAL-specific decoders
                decoder_options = {"auto", "pal2d", "transform2d", "transform3d", "mono"};
            } else if (video_params->system == VideoSystem::NTSC) {
                // NTSC-specific decoders
                decoder_options = {"auto", "ntsc1d", "ntsc2d", "ntsc3d", "ntsc3dnoadapt", "mono"};
            } else {
                // Unknown system - show all
                decoder_options = {"auto", "pal2d", "transform2d", "transform3d", "ntsc1d", "ntsc2d", "ntsc3d", "ntsc3dnoadapt", "mono"};
            }
        } else {
            // No video params yet - show all
            decoder_options = {"auto", "pal2d", "transform2d", "transform3d", "ntsc1d", "ntsc2d", "ntsc3d", "ntsc3dnoadapt", "mono"};
        }
    } else {
        // No input yet - show all
        decoder_options = {"auto", "pal2d", "transform2d", "transform3d", "ntsc1d", "ntsc2d", "ntsc3d", "ntsc3dnoadapt", "mono"};
    }
    
    return {
        ParameterDescriptor{
            "output_path",
            "Output Path",
            "Path to output file (RGB, YUV, or Y4M format based on output_format)",
            ParameterType::FILE_PATH,
            ParameterConstraints{}
        },
        ParameterDescriptor{
            "decoder_type",
            "Decoder Type",
            "Chroma decoder to use: auto, pal2d, transform2d, transform3d, ntsc1d, ntsc2d, ntsc3d, ntsc3dnoadapt, mono",
            ParameterType::STRING,
            {{}, {}, {}, decoder_options, false}
        },
        ParameterDescriptor{
            "output_format",
            "Output Format",
            "Output pixel format: rgb (RGB48), yuv (YUV444P16), y4m (YUV444P16 with Y4M headers)",
            ParameterType::STRING,
            {{}, {}, {}, {"rgb", "yuv", "y4m"}, false}
        },
        ParameterDescriptor{
            "chroma_gain",
            "Chroma Gain",
            "Gain factor applied to chroma components (color saturation). Range: 0.0-10.0",
            ParameterType::DOUBLE,
            {0.0, 10.0, 1.0, {}, false}
        },
        ParameterDescriptor{
            "chroma_phase",
            "Chroma Phase",
            "Phase rotation applied to chroma components in degrees. Range: -180 to 180",
            ParameterType::DOUBLE,
            {-180.0, 180.0, 0.0, {}, false}
        },
        ParameterDescriptor{
            "start_frame",
            "Start Frame",
            "First frame to process (1-based). Default: 1",
            ParameterType::INT32,
            {1, {}, 1, {}, false}
        },
        ParameterDescriptor{
            "length",
            "Length",
            "Number of frames to process. -1 means process all frames. Default: -1",
            ParameterType::INT32,
            {-1, {}, -1, {}, false}
        },
        ParameterDescriptor{
            "threads",
            "Threads",
            "Number of worker threads. 0 means auto-detect. Default: 0",
            ParameterType::INT32,
            {0, 64, 0, {}, false}
        },
        ParameterDescriptor{
            "luma_nr",
            "Luma Noise Reduction",
            "Luma noise reduction level in dB. 0 = disabled. Range: 0.0-10.0",
            ParameterType::DOUBLE,
            {0.0, 10.0, 0.0, {}, false}
        },
        ParameterDescriptor{
            "chroma_nr",
            "Chroma Noise Reduction",
            "Chroma noise reduction level in dB (NTSC only). 0 = disabled. Range: 0.0-10.0",
            ParameterType::DOUBLE,
            {0.0, 10.0, 0.0, {}, false}
        },
        ParameterDescriptor{
            "ntsc_phase_comp",
            "NTSC Phase Compensation",
            "Adjust phase per-line using burst phase (NTSC only)",
            ParameterType::BOOL,
            {{}, {}, false, {}, false}
        },
        ParameterDescriptor{
            "simple_pal",
            "Simple PAL",
            "Use 1D UV filter for Transform PAL (simpler, faster, lower quality)",
            ParameterType::BOOL,
            {{}, {}, false, {}, false}
        },
        ParameterDescriptor{
            "output_padding",
            "Output Padding",
            "Pad output to multiple of this many pixels on both axes. Range: 1-32",
            ParameterType::INT32,
            {1, 32, 8, {}, false}        },
        ParameterDescriptor{
            "first_active_frame_line",
            "First Active Frame Line",
            "Override first visible line of frame (-1 uses source default). Range: -1 to 620",
            ParameterType::INT32,
            {-1, 620, -1, {}, false}
        },
        ParameterDescriptor{
            "last_active_frame_line",
            "Last Active Frame Line",
            "Override last visible line of frame (-1 uses source default). Range: -1 to 620",
            ParameterType::INT32,
            {-1, 620, -1, {}, false}
        }
    };
}

std::map<std::string, ParameterValue> ChromaSinkStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["output_path"] = output_path_;
    params["decoder_type"] = decoder_type_;
    params["output_format"] = output_format_;
    params["chroma_gain"] = chroma_gain_;
    params["chroma_phase"] = chroma_phase_;
    params["start_frame"] = start_frame_;
    params["length"] = length_;
    params["threads"] = threads_;
    params["luma_nr"] = luma_nr_;
    params["chroma_nr"] = chroma_nr_;
    params["ntsc_phase_comp"] = ntsc_phase_comp_;
    params["simple_pal"] = simple_pal_;
    params["output_padding"] = output_padding_;
    params["first_active_frame_line"] = first_active_frame_line_;
    params["last_active_frame_line"] = last_active_frame_line_;
    return params;
}

bool ChromaSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    bool decoder_config_changed = false;
    
    for (const auto& [key, value] : params) {
        if (key == "output_path") {
            if (std::holds_alternative<std::string>(value)) {
                output_path_ = std::get<std::string>(value);
            }
        } else if (key == "decoder_type") {
            if (std::holds_alternative<std::string>(value)) {
                auto new_val = std::get<std::string>(value);
                if (new_val != decoder_type_) {
                    ORC_LOG_DEBUG("ChromaSink: decoder_type changed from '{}' to '{}'", decoder_type_, new_val);
                    decoder_type_ = new_val;
                    decoder_config_changed = true;
                }
            }
        } else if (key == "output_format") {
            if (std::holds_alternative<std::string>(value)) {
                output_format_ = std::get<std::string>(value);
            }
        } else if (key == "chroma_gain") {
            if (std::holds_alternative<double>(value)) {
                auto new_val = std::get<double>(value);
                if (new_val != chroma_gain_) {
                    ORC_LOG_DEBUG("ChromaSink: chroma_gain changed from {} to {}", chroma_gain_, new_val);
                    chroma_gain_ = new_val;
                    decoder_config_changed = true;
                }
            }
        } else if (key == "chroma_phase") {
            if (std::holds_alternative<double>(value)) {
                auto new_val = std::get<double>(value);
                if (new_val != chroma_phase_) {
                    ORC_LOG_DEBUG("ChromaSink: chroma_phase changed from {} to {}", chroma_phase_, new_val);
                    chroma_phase_ = new_val;
                    decoder_config_changed = true;
                }
            }
        } else if (key == "start_frame") {
            if (std::holds_alternative<int>(value)) {
                start_frame_ = std::get<int>(value);
                ORC_LOG_INFO("ChromaSink: Parameter start_frame set to {}", start_frame_);
            }
        } else if (key == "length") {
            if (std::holds_alternative<int>(value)) {
                length_ = std::get<int>(value);
                ORC_LOG_INFO("ChromaSink: Parameter length set to {}", length_);
            }
        } else if (key == "threads") {
            if (std::holds_alternative<int>(value)) {
                threads_ = std::get<int>(value);
            }

        } else if (key == "luma_nr") {
            if (std::holds_alternative<double>(value)) {
                auto new_val = std::get<double>(value);
                if (new_val != luma_nr_) {
                    ORC_LOG_DEBUG("ChromaSink: luma_nr changed from {} to {}", luma_nr_, new_val);
                    luma_nr_ = new_val;
                    decoder_config_changed = true;
                }
            }
        } else if (key == "chroma_nr") {
            if (std::holds_alternative<double>(value)) {
                auto new_val = std::get<double>(value);
                if (new_val != chroma_nr_) {
                    ORC_LOG_DEBUG("ChromaSink: chroma_nr changed from {} to {}", chroma_nr_, new_val);
                    chroma_nr_ = new_val;
                    decoder_config_changed = true;
                }
            }
        } else if (key == "ntsc_phase_comp") {
            if (std::holds_alternative<bool>(value)) {
                auto new_val = std::get<bool>(value);
                if (new_val != ntsc_phase_comp_) {
                    ORC_LOG_DEBUG("ChromaSink: ntsc_phase_comp changed from {} to {}", ntsc_phase_comp_, new_val);
                    ntsc_phase_comp_ = new_val;
                    decoder_config_changed = true;
                }
            }
        } else if (key == "simple_pal") {
            if (std::holds_alternative<bool>(value)) {
                auto new_val = std::get<bool>(value);
                if (new_val != simple_pal_) {
                    ORC_LOG_DEBUG("ChromaSink: simple_pal changed from {} to {}", simple_pal_, new_val);
                    simple_pal_ = new_val;
                    decoder_config_changed = true;
                }
            }
        } else if (key == "output_padding") {
            if (std::holds_alternative<int>(value)) {
                output_padding_ = std::get<int>(value);
            }        } else if (key == "first_active_frame_line") {
            if (std::holds_alternative<int32_t>(value)) {
                first_active_frame_line_ = std::get<int32_t>(value);
            }
        } else if (key == "last_active_frame_line") {
            if (std::holds_alternative<int32_t>(value)) {
                last_active_frame_line_ = std::get<int32_t>(value);
            }
        }
    }
    
    // Log if decoder configuration was changed
    if (decoder_config_changed) {
        ORC_LOG_INFO("ChromaSink: Decoder configuration changed - cached decoder will be recreated on next preview");
    }
    
    return true;
}

bool ChromaSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    ORC_LOG_INFO("ChromaSink: Trigger called - starting decode");
    
    // Apply any parameter updates
    set_parameters(parameters);
    
    // 1. Extract VideoFieldRepresentation from input
    if (inputs.empty()) {
        ORC_LOG_ERROR("ChromaSink: No input provided");
        trigger_status_ = "Error: No input";
        return false;
    }
    
    auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
    if (!vfr) {
        ORC_LOG_ERROR("ChromaSink: Input is not a VideoFieldRepresentation");
        trigger_status_ = "Error: Invalid input type";
        return false;
    }
    
    // 2. Get video parameters from VFR
    auto video_params_opt = vfr->get_video_parameters();
    if (!video_params_opt) {
        ORC_LOG_ERROR("ChromaSink: Input has no video parameters");
        trigger_status_ = "Error: No video parameters";
        return false;
    }
    
    // 3. Use orc-core VideoParameters directly
    auto videoParams = *video_params_opt;  // Make a copy so we can modify it
    
    // Apply line parameter overrides if specified
    if (first_active_frame_line_ >= 0) {
        videoParams.first_active_frame_line = first_active_frame_line_;
        ORC_LOG_INFO("ChromaSink: Overriding first_active_frame_line to {}", first_active_frame_line_);
    }
    if (last_active_frame_line_ >= 0) {
        videoParams.last_active_frame_line = last_active_frame_line_;
        ORC_LOG_INFO("ChromaSink: Overriding last_active_frame_line to {}", last_active_frame_line_);
    }
    
    // Apply padding adjustments to active video region BEFORE configuring decoder
    // This ensures the decoder processes the correct region that will be written to output
    {
        OutputWriter::Configuration writerConfig;
        writerConfig.paddingAmount = 8;  // Same as used later for actual output
        
        ORC_LOG_DEBUG("ChromaSink: BEFORE padding adjustment: first_active_frame_line={}, last_active_frame_line={}", 
                      videoParams.first_active_frame_line, videoParams.last_active_frame_line);
        
        // Create temporary output writer just to apply padding adjustments
        OutputWriter tempWriter;
        tempWriter.updateConfiguration(videoParams, writerConfig);
        // videoParams now has adjusted activeVideoStart/End values
        
        ORC_LOG_DEBUG("ChromaSink: AFTER padding adjustment: first_active_frame_line={}, last_active_frame_line={}", 
                      videoParams.first_active_frame_line, videoParams.last_active_frame_line);
    }
    
    // 4. Create appropriate decoder
    // Note: We'll use the decoder classes directly (synchronously)
    // without the threading infrastructure for now
    
    std::unique_ptr<MonoDecoder> monoDecoder;
    std::unique_ptr<PalColour> palDecoder;
    std::unique_ptr<Comb> ntscDecoder;
    
    bool useMonoDecoder = (decoder_type_ == "mono");
    bool usePalDecoder = (decoder_type_ == "auto" && videoParams.system == orc::VideoSystem::PAL) ||
                         (decoder_type_ == "pal2d" || decoder_type_ == "transform2d" || decoder_type_ == "transform3d");
    bool useNtscDecoder = (decoder_type_ == "auto" && videoParams.system == orc::VideoSystem::NTSC) ||
                          (decoder_type_.find("ntsc") == 0);
    
    if (useMonoDecoder) {
        MonoDecoder::MonoConfiguration config;
        config.yNRLevel = luma_nr_;
        config.videoParameters = videoParams;
        monoDecoder = std::make_unique<MonoDecoder>(config);
        ORC_LOG_INFO("ChromaSink: Using decoder: mono");
    }
    else if (usePalDecoder) {
        PalColour::Configuration config;
        config.chromaGain = chroma_gain_;
        config.chromaPhase = chroma_phase_;
        config.yNRLevel = luma_nr_;
        config.simplePAL = simple_pal_;
        config.showFFTs = false;
        
        // Set filter mode based on decoder type
        std::string filterName;
        if (decoder_type_ == "transform3d") {
            config.chromaFilter = PalColour::transform3DFilter;
            filterName = "transform3d";
        } else if (decoder_type_ == "transform2d") {
            config.chromaFilter = PalColour::transform2DFilter;
            filterName = "transform2d";
        } else if (decoder_type_ == "pal2d" || decoder_type_ == "auto") {
            // pal2d uses the basic PAL colour filter (default)
            config.chromaFilter = PalColour::palColourFilter;
            filterName = decoder_type_ == "auto" ? "pal2d (auto)" : "pal2d";
        } else {
            config.chromaFilter = PalColour::palColourFilter;
            filterName = "pal2d (default)";
        }
        
        palDecoder = std::make_unique<PalColour>();
        palDecoder->updateConfiguration(videoParams, config);
        ORC_LOG_INFO("ChromaSink: Using decoder: {} (PAL)", filterName);
    }
    else if (useNtscDecoder) {
        Comb::Configuration config;
        config.chromaGain = chroma_gain_;
        config.chromaPhase = chroma_phase_;
        config.cNRLevel = chroma_nr_;
        config.yNRLevel = luma_nr_;
        config.phaseCompensation = ntsc_phase_comp_;
        config.showMap = false;
        
        // Set dimensions based on decoder type
        std::string decoderName;
        if (decoder_type_ == "ntsc1d") {
            config.dimensions = 1;
            config.adaptive = false;
            decoderName = "ntsc1d";
        } else if (decoder_type_ == "ntsc3d") {
            config.dimensions = 3;
            config.adaptive = true;
            decoderName = "ntsc3d";
        } else if (decoder_type_ == "ntsc3dnoadapt") {
            config.dimensions = 3;
            config.adaptive = false;
            decoderName = "ntsc3dnoadapt";
        } else {
            config.dimensions = 2;
            config.adaptive = false;
            decoderName = decoder_type_ == "auto" ? "ntsc2d (auto)" : "ntsc2d";
        }
        
        ntscDecoder = std::make_unique<Comb>();
        ntscDecoder->updateConfiguration(videoParams, config);
        ORC_LOG_INFO("ChromaSink: Using decoder: {} (NTSC)", decoderName);
    }
    else {
        ORC_LOG_ERROR("ChromaSink: Unknown decoder type: {}", decoder_type_);
        trigger_status_ = "Error: Unknown decoder type";
        return false;
    }
    
    // 5. Determine frame range to process
    size_t total_fields = vfr->field_count();
    size_t total_frames = total_fields / 2;
    
    ORC_LOG_INFO("ChromaSink: Frame range parameters: start_frame_={}, length_={}, total_frames={}", 
                 start_frame_, length_, total_frames);
    
    size_t start_frame = (start_frame_ > 0) ? (start_frame_ - 1) : 0;  // Convert to 0-based
    size_t num_frames = (length_ > 0) ? length_ : (total_frames - start_frame);
    size_t end_frame = std::min(start_frame + num_frames, total_frames);
    
    ORC_LOG_INFO("ChromaSink: Processing frames {} to {} (of {})", 
                 start_frame + 1, end_frame, total_frames);
    
    // Debug: Check field range
    auto field_range = vfr->field_range();
    // Field range determined
    
    // 6. Field ordering and interlacing structure
    // In interlaced video, each frame consists of two fields captured sequentially.
    // Fields are stored in chronological order: 0, 1, 2, 3, 4, 5...
    // 
    // Field parity is assigned based on field index:
    //   - Even field indices (0, 2, 4...) → FieldParity::Top    → first field
    //   - Odd field indices (1, 3, 5...)  → FieldParity::Bottom → second field
    // 
    // This relationship is consistent across both NTSC and PAL systems.
    // Frame N (1-based) consists of fields (2*N-2, 2*N-1) in 0-based indexing.
    
    // 6. Determine decoder lookbehind/lookahead requirements
    int32_t lookBehindFrames = 0;
    int32_t lookAheadFrames = 0;
    
    if (palDecoder) {
        // PalColour internally uses Transform3D which needs lookbehind/lookahead
        if (decoder_type_ == "transform3d" || decoder_type_ == "transform2d") {
            // Transform PAL decoders need extra fields for FFT overlap
            // These values come from TransformPal3D::getLookBehind/Ahead()
            lookBehindFrames = (decoder_type_ == "transform3d") ? 2 : 0;  // (HALFZTILE + 1) / 2
            lookAheadFrames = (decoder_type_ == "transform3d") ? 4 : 0;   // (ZTILE - 1 + 1) / 2
        }
    } else if (ntscDecoder) {
        // NTSC 3D decoder might need lookbehind/lookahead
        if (decoder_type_ == "ntsc3d" || decoder_type_ == "ntsc3dnoadapt") {
            lookBehindFrames = 1;  // From Comb::Configuration::getLookBehind()
            lookAheadFrames = 2;   // From Comb::Configuration::getLookAhead()
        }
    }
    
    ORC_LOG_INFO("ChromaSink: Decoder requires lookBehind={} frames, lookAhead={} frames",
                 lookBehindFrames, lookAheadFrames);
    
    // 7. Calculate extended frame range including lookbehind/lookahead
    // Note: extended_start_frame can be negative (will use black padding)
    int32_t extended_start_frame = static_cast<int32_t>(start_frame) - lookBehindFrames;
    int32_t extended_end_frame = static_cast<int32_t>(end_frame) + lookAheadFrames;
    
    // 8. Collect fields including lookbehind/lookahead padding
    std::vector<SourceField> inputFields;
    int32_t total_fields_needed = (extended_end_frame - extended_start_frame) * 2;
    inputFields.reserve(total_fields_needed);
    
    ORC_LOG_INFO("ChromaSink: Collecting {} fields (frames {}-{}) for decode",
                 total_fields_needed, extended_start_frame + 1, extended_end_frame);
    
    for (int32_t frame = extended_start_frame; frame < extended_end_frame; frame++) {
        // Determine if this frame is outside the valid range (need black padding)
        // Note: 'frame' is in 0-based indexing after conversion from user's 1-based start_frame_
        // Valid frames are 0 to (total_frames - 1) in 0-based indexing
        // Frames < 0 or >= total_frames need black padding
        bool useBlankFrame = (frame < 0) || (frame >= static_cast<int32_t>(total_frames));
        
        // Convert frame to 1-based for field ID calculation (TBC uses 1-based frame numbering)
        // For metadata lookup, use frame+1 to match TBC's 1-based system
        int32_t frameNumberFor1BasedTBC = frame + 1;
        
        // If outside bounds, use frame 1 (first frame) for metadata but black for data
        int32_t metadataFrameNumber = useBlankFrame ? 1 : frameNumberFor1BasedTBC;
        
        if (frame < 5 || frame > static_cast<int32_t>(start_frame_ + length_ - 3)) {
            // Processing frame
        }
        
        // Frame N (1-based numbering) consists of fields (2*N-2) and (2*N-1) in 0-based indexing
        // Fields are ALWAYS in chronological order in the input array
        // The isFirstField flag in each SourceField indicates logical field order
        FieldID firstFieldId = FieldID((metadataFrameNumber * 2) - 2);   // Even field (chronologically first)
        FieldID secondFieldId = FieldID((metadataFrameNumber * 2) - 1);  // Odd field (chronologically second)
        
        // For blank frames, skip field scanning - just use metadata from frame 1
        if (!useBlankFrame) {
            // Verify the calculated field IDs point to valid fields
            // If not, scan forward to find the next valid field pair
            // (handles dropped/repeated fields in the source)
            FieldID scan_id = firstFieldId;
            int max_scan = 10;  // Don't scan too far
            
            for (int scan = 0; scan < max_scan && scan_id.value() < field_range.end.value(); scan++) {
                if (!vfr->has_field(scan_id)) {
                    scan_id = FieldID(scan_id.value() + 1);
                    continue;
                }
                
                // Check if this field has Top parity (first field)
                auto desc_opt = vfr->get_descriptor(scan_id);
                if (desc_opt.has_value() && desc_opt->parity == FieldParity::Top) {
                    firstFieldId = scan_id;
                    secondFieldId = FieldID(scan_id.value() + 1);
                    break;
                }
                scan_id = FieldID(scan_id.value() + 1);
            }
            
            // Check if fields exist
            if (!vfr->has_field(firstFieldId) || !vfr->has_field(secondFieldId)) {
                ORC_LOG_WARN("ChromaSink: Skipping frame {} (missing fields {}/{})", 
                            frame + 1, firstFieldId.value(), secondFieldId.value());
                continue;
            }
        }
        
        // Convert fields to SourceField format
        SourceField sf1, sf2;
        
        if (useBlankFrame) {
            // Create blank fields with metadata from frame 1 but black data
            sf1 = convertToSourceField(vfr.get(), firstFieldId);
            sf2 = convertToSourceField(vfr.get(), secondFieldId);
            
            // Fill with black
            uint16_t black = videoParams.black_16b_ire;
            size_t field_length = sf1.data.size();
            sf1.data.assign(field_length, black);
            sf2.data.assign(field_length, black);
        } else {
            sf1 = convertToSourceField(vfr.get(), firstFieldId);
            sf2 = convertToSourceField(vfr.get(), secondFieldId);
            
            // Apply PAL subcarrier shift: With subcarrier-locked 4fSC PAL sampling,
            // we have four "extra" samples over the course of the frame, so the two
            // fields will be horizontally misaligned by two samples. Shift the
            // second field to the left to compensate.
            if ((videoParams.system == orc::VideoSystem::PAL || videoParams.system == orc::VideoSystem::PAL_M) && 
                videoParams.is_subcarrier_locked) {
                // Remove first 2 samples and append 2 black samples at the end
                uint16_t black = videoParams.black_16b_ire;
                sf2.data.erase(sf2.data.begin(), sf2.data.begin() + 2);
                sf2.data.push_back(black);
                sf2.data.push_back(black);
            }
        }
        
        inputFields.push_back(sf1);
        inputFields.push_back(sf2);
    }
    
    // 10. Process frames in parallel using worker threads
    // CRITICAL: Transform3D is a 3D temporal FFT filter that processes frames at specific
    // Z-positions (temporal indices). Each frame MUST be at the SAME Z-position (field indices
    // lookBehind*2 to lookBehind*2+2) regardless of its frame number, otherwise the FFT results
    // will differ. Workers process frames independently with proper context.
    //
    // THREAD SAFETY: Each worker thread creates its own decoder instance to avoid state conflicts.
    // Transform PAL decoders use FFT buffers that cannot be shared between threads.
    
    int32_t numFrames = (end_frame - start_frame);
    std::vector<ComponentFrame> outputFrames;
    outputFrames.resize(numFrames);
    
    // Determine number of threads to use
    int32_t numThreads = threads_;
    if (numThreads <= 0) {
        numThreads = std::thread::hardware_concurrency();
        if (numThreads <= 0) numThreads = 4;  // Fallback
    }
    // Don't use more threads than frames
    numThreads = std::min(numThreads, numFrames);
    
    ORC_LOG_INFO("ChromaSink: Processing {} frames using {} worker threads", numFrames, numThreads);
    
    // Shared state for work distribution
    std::atomic<int32_t> nextFrameIdx{0};
    std::atomic<bool> abortFlag{false};
    
    // CRITICAL: FFTW plan creation with FFTW_MEASURE is NOT thread-safe
    // (see FFTW docs: http://www.fftw.org/fftw3_doc/Thread-safety.html)
    // We must serialize all decoder instantiations that create FFTW plans
    std::mutex fftwPlanMutex;
    
    // Worker thread function - each worker creates its own decoder instance
    auto workerFunc = [&]() {
        // Create thread-local decoder instance
        std::unique_ptr<MonoDecoder> threadMonoDecoder;
        std::unique_ptr<PalColour> threadPalDecoder;
        std::unique_ptr<Comb> threadNtscDecoder;
        
        if (monoDecoder) {
            // Clone configuration from main decoder
            MonoDecoder::MonoConfiguration config;
            config.yNRLevel = luma_nr_;
            config.videoParameters = videoParams;
            threadMonoDecoder = std::make_unique<MonoDecoder>(config);
        } else if (palDecoder) {
            // Clone configuration from main decoder
            PalColour::Configuration config;
            config.chromaGain = chroma_gain_;
            config.chromaPhase = chroma_phase_;
            config.yNRLevel = luma_nr_;
            config.simplePAL = simple_pal_;
            config.showFFTs = false;
            
            if (decoder_type_ == "transform3d") {
                config.chromaFilter = PalColour::transform3DFilter;
            } else if (decoder_type_ == "transform2d") {
                config.chromaFilter = PalColour::transform2DFilter;
            } else {
                config.chromaFilter = PalColour::palColourFilter;
            }
            
            // CRITICAL: Protect FFTW plan creation (Transform PAL uses FFTW_MEASURE which is not thread-safe)
            {
                std::lock_guard<std::mutex> lock(fftwPlanMutex);
                threadPalDecoder = std::make_unique<PalColour>();
                threadPalDecoder->updateConfiguration(videoParams, config);
            }
        } else if (ntscDecoder) {
            // Clone configuration from main decoder
            Comb::Configuration config;
            config.chromaGain = chroma_gain_;
            config.chromaPhase = chroma_phase_;
            config.cNRLevel = chroma_nr_;
            config.yNRLevel = luma_nr_;
            config.phaseCompensation = ntsc_phase_comp_;
            config.showMap = false;
            
            if (decoder_type_ == "ntsc1d") {
                config.dimensions = 1;
                config.adaptive = false;
            } else if (decoder_type_ == "ntsc3d") {
                config.dimensions = 3;
                config.adaptive = true;
            } else if (decoder_type_ == "ntsc3dnoadapt") {
                config.dimensions = 3;
                config.adaptive = false;
            } else {
                config.dimensions = 2;
                config.adaptive = false;
            }
            
            threadNtscDecoder = std::make_unique<Comb>();
            threadNtscDecoder->updateConfiguration(videoParams, config);
        }
        
        while (!abortFlag) {
            // Get next frame to process
            int32_t frameIdx = nextFrameIdx.fetch_add(1);
            if (frameIdx >= numFrames) {
                break;  // No more frames to process
            }
            
            // Build a field array for this ONE frame:
            // [lookbehind fields... target frame fields... lookahead fields...]
            std::vector<SourceField> frameFields;
            
            // The actual frame number we're processing
            int32_t actualFrameNum = static_cast<int32_t>(start_frame) + frameIdx;
            
            // Position in inputFields where this frame's fields start
            int32_t frameStartIdx = (actualFrameNum - extended_start_frame) * 2;
            
            // Calculate the range to copy: lookbehind + target + lookahead
            int32_t copyStartIdx = frameStartIdx - (lookBehindFrames * 2);
            int32_t copyEndIdx = frameStartIdx + 2 + (lookAheadFrames * 2);
            
            // Clamp to valid range and copy
            copyStartIdx = std::max(0, copyStartIdx);
            copyEndIdx = std::min(static_cast<int32_t>(inputFields.size()), copyEndIdx);
            
            for (int32_t i = copyStartIdx; i < copyEndIdx; i++) {
                frameFields.push_back(inputFields[i]);
            }
            
            // The target frame's position within frameFields depends on how much lookbehind we actually got
            int32_t actualLookbehindFields = (frameStartIdx - copyStartIdx);
            int32_t frameStartIndex = actualLookbehindFields;
            int32_t frameEndIndex = frameStartIndex + 2;
            
            // Prepare single-frame output buffer
            std::vector<ComponentFrame> singleOutput;
            singleOutput.resize(1);
            
            // Decode this ONE frame using thread-local decoder
            if (threadMonoDecoder) {
                threadMonoDecoder->decodeFrames(frameFields, frameStartIndex, frameEndIndex, singleOutput);
            } else if (threadPalDecoder) {
                threadPalDecoder->decodeFrames(frameFields, frameStartIndex, frameEndIndex, singleOutput);
            } else if (threadNtscDecoder) {
                threadNtscDecoder->decodeFrames(frameFields, frameStartIndex, frameEndIndex, singleOutput);
            }
            
            // Store the result (no mutex needed - each thread writes to a different index)
            outputFrames[frameIdx] = singleOutput[0];
        }
    };
    
    // Create and start worker threads
    std::vector<std::thread> workers;
    workers.reserve(numThreads);
    for (int32_t i = 0; i < numThreads; i++) {
        workers.emplace_back(workerFunc);
    }
    
    // Wait for all workers to finish
    for (auto& worker : workers) {
        worker.join();
    }
    
    ORC_LOG_INFO("ChromaSink: Decoded {} frames", outputFrames.size());
    
    ORC_LOG_DEBUG("ChromaSink: videoParams.first_active_frame_line={}, last_active_frame_line={}", 
                  videoParams.first_active_frame_line, videoParams.last_active_frame_line);
    
    // DEBUG: Log ComponentFrame Y checksums using accessor method
    for (size_t k = 0; k < outputFrames.size() && k < 3; k++) {
        // Access Y data using line accessor
        int32_t firstLine = videoParams.first_active_frame_line;
        ORC_LOG_DEBUG("ChromaSink: About to access ComponentFrame[{}].y({}) (height={})", 
                      k, firstLine, outputFrames[k].getHeight());
        const double* yLinePtr = outputFrames[k].y(firstLine);
        int32_t width = outputFrames[k].getWidth();
        
        if (yLinePtr && width > 0) {
            uint64_t yChecksum = 0;
            for (int i = 0; i < std::min(100, width); i++) {
                yChecksum += static_cast<uint64_t>(yLinePtr[i] * 1000);
            }
            ORC_LOG_INFO("ChromaSink: ComponentFrame[{}] Y line {} checksum (first 100 pixels)={}, width={}, first 4: {:.2f} {:.2f} {:.2f} {:.2f}", 
                         k, firstLine, yChecksum, width,
                         width > 0 ? yLinePtr[0] : 0.0,
                         width > 1 ? yLinePtr[1] : 0.0,
                         width > 2 ? yLinePtr[2] : 0.0,
                         width > 3 ? yLinePtr[3] : 0.0);
        }
    }
    
    // 13. Convert to std::vector for output writer
    std::vector<ComponentFrame> stdOutputFrames;
    stdOutputFrames.reserve(outputFrames.size());
    for (const auto& frame : outputFrames) {
        stdOutputFrames.push_back(frame);
    }
    
    // 14. Write output file
    if (!writeOutputFile(output_path_, output_format_, stdOutputFrames, &videoParams)) {
        ORC_LOG_ERROR("ChromaSink: Failed to write output file: {}", output_path_);
        trigger_status_ = "Error: Failed to write output";
        return false;
    }
    
    ORC_LOG_INFO("ChromaSink: Output written to: {}", output_path_);
    
    trigger_status_ = "Decode complete: " + std::to_string(outputFrames.size()) + " frames";
    return true;
}

std::string ChromaSinkStage::get_trigger_status() const
{
    return trigger_status_;
}

// Helper method: Convert VideoFieldRepresentation field to SourceField
SourceField ChromaSinkStage::convertToSourceField(
    const VideoFieldRepresentation* vfr,
    FieldID field_id) const
{
    SourceField sf;
    
    // Get field descriptor
    auto desc_opt = vfr->get_descriptor(field_id);
    if (!desc_opt) {
        ORC_LOG_WARN("ChromaSink: Field {} has no descriptor", static_cast<uint64_t>(field_id.value()));
        return sf;
    }
    
    const auto& desc = *desc_opt;
    
    // Set field metadata
    // Note: seq_no must be 1-based (ORC uses 0-based FieldID, so add 1)
    sf.field.seq_no = static_cast<int32_t>(field_id.value()) + 1;
    
    // Determine if this is the "first field" or "second field" from field parity
    // Field parity determines field ordering (same for both NTSC and PAL):
    //   - Top field (even field indices)    → first field
    //   - Bottom field (odd field indices)  → second field
    sf.field.is_first_field = (desc.parity == FieldParity::Top);
    
    ORC_LOG_TRACE("ChromaSink: Field {} parity={} → isFirstField={}",
                 field_id.value(),
                 desc.parity == FieldParity::Top ? "Top" : "Bottom",
                 sf.field.is_first_field.value_or(false));
    
    // Get field_phase_id from phase hint (from TBC metadata)
    auto phase_hint = vfr->get_field_phase_hint(field_id);
    if (phase_hint.has_value()) {
        sf.field.field_phase_id = phase_hint->field_phase_id;
        ORC_LOG_TRACE("ChromaSink: Field {} has fieldPhaseID={}", field_id.value(), sf.field.field_phase_id.value());
    }
    
    ORC_LOG_TRACE("ChromaSink: Field {} (1-based seqNo={}) parity={} -> isFirstField={}", 
                  field_id.value(),
                  sf.field.seq_no,
                  (desc.parity == FieldParity::Top ? "Top" : "Bottom"),
                  sf.field.is_first_field.value_or(false));
    
    // Get field data
    std::vector<uint16_t> field_data = vfr->get_field(field_id);
    
    // Copy field data to SourceField
    sf.data = field_data;
    
    // Apply PAL subcarrier-locked field shift (matches standalone decoder behavior)
    // With 4fSC PAL sampling, the two fields are misaligned by 2 samples
    // The second field needs to be shifted left by 2 samples
    const auto& videoParams = *vfr->get_video_parameters();
    bool isPal = (videoParams.system == VideoSystem::PAL || videoParams.system == VideoSystem::PAL_M);
    bool isSecondField = (desc.parity == FieldParity::Bottom);
    
    if (isPal && videoParams.is_subcarrier_locked && isSecondField) {
        // Shift second field left by 2 samples (remove first 2, add 2 black samples at end)
        sf.data.erase(sf.data.begin(), sf.data.begin() + 2);
        uint16_t black = static_cast<uint16_t>(videoParams.black_16b_ire);
        sf.data.push_back(black);
        sf.data.push_back(black);
        ORC_LOG_TRACE("ChromaSink: Applied PAL subcarrier-locked shift to field {}", field_id.value());
    }
    
    // Log complete Field structure for debugging (first 6 fields only)
    if (field_id.value() < 6) {
        ORC_LOG_DEBUG("ChromaSink: Field {} FULL metadata:", field_id.value());
        ORC_LOG_DEBUG("  seq_no={} is_first_field={} field_phase_id={}", 
                      sf.field.seq_no, 
                      sf.field.is_first_field.value_or(false), 
                      sf.field.field_phase_id.value_or(-1));
        ORC_LOG_DEBUG("  data.size()={} first4=[{},{},{},{}]",
                      sf.data.size(),
                      sf.data.size() > 0 ? sf.data[0] : 0,
                      sf.data.size() > 1 ? sf.data[1] : 0,
                      sf.data.size() > 2 ? sf.data[2] : 0,
                      sf.data.size() > 3 ? sf.data[3] : 0);
    }
    

    return sf;
}

// Helper method: Write output frames to file
bool ChromaSinkStage::writeOutputFile(
    const std::string& output_path,
    const std::string& format,
    const std::vector<ComponentFrame>& frames,
    const void* videoParamsPtr) const
{
    const auto& videoParams = *static_cast<const orc::VideoParameters*>(videoParamsPtr);
    if (frames.empty()) {
        ORC_LOG_ERROR("ChromaSink: No frames to write");
        return false;
    }
    
    // Open output file
    std::ofstream outputFile(output_path, std::ios::binary);
    if (!outputFile.is_open()) {
        ORC_LOG_ERROR("ChromaSink: Failed to open output file: {}", output_path);
        return false;
    }
    
    // Determine output format
    OutputWriter::Configuration writerConfig;
    writerConfig.paddingAmount = 8;
    
    if (format == "rgb") {
        writerConfig.pixelFormat = OutputWriter::RGB48;
        writerConfig.outputY4m = false;
    } else if (format == "yuv") {
        writerConfig.pixelFormat = OutputWriter::YUV444P16;
        writerConfig.outputY4m = false;
    } else if (format == "y4m") {
        writerConfig.pixelFormat = OutputWriter::YUV444P16;
        writerConfig.outputY4m = true;
    } else {
        ORC_LOG_ERROR("ChromaSink: Unknown output format: {}", format);
        return false;
    }
    
    // Create output writer
    OutputWriter writer;
    orc::VideoParameters mutableParams = videoParams;
    writer.updateConfiguration(mutableParams, writerConfig);
    writer.printOutputInfo();  // Show output format info
    
    // Write stream header if needed
    std::string streamHeader = writer.getStreamHeader();
    if (!streamHeader.empty()) {
        outputFile.write(streamHeader.data(), streamHeader.size());
    }
    
    // Write frames
    int frameIdx = 0;
    for (const auto& frame : frames) {
        // Write frame header if needed
        std::string frameHeader = writer.getFrameHeader();
        if (!frameHeader.empty()) {
            outputFile.write(frameHeader.data(), frameHeader.size());
        }
        
        // Convert frame to output format
        OutputFrame outputFrame;
        writer.convert(frame, outputFrame);
        
        frameIdx++;
        
        // Write output data
        const char* data = reinterpret_cast<const char*>(outputFrame.data());
        std::streamsize size = outputFrame.size() * sizeof(uint16_t);
        outputFile.write(data, size);
        
        if (!outputFile.good()) {
            ORC_LOG_ERROR("ChromaSink: Failed to write frame data");
            outputFile.close();
            return false;
        }
    }
    
    outputFile.close();
    
    ORC_LOG_INFO("ChromaSink: Wrote {} frames to {}", frames.size(), output_path);
    return true;
}

std::vector<PreviewOption> ChromaSinkStage::get_preview_options() const
{
    if (!cached_input_) {
        return {};
    }
    
    auto video_params = cached_input_->get_video_parameters();
    if (!video_params) {
        return {};
    }
    
    uint64_t field_count = cached_input_->field_count();
    if (field_count < 2) {
        return {};  // Need at least 2 fields to decode a frame
    }
    
    uint64_t frame_count = field_count / 2;
    
    // Decode a test frame to get the actual full frame dimensions (with padding)
    uint32_t full_width = 0;
    uint32_t full_height = 0;
    
    if (frame_count > 0) {
        auto test_preview = render_preview("frame", 0, PreviewNavigationHint::Random);
        if (test_preview.width > 0 && test_preview.height > 0) {
            full_width = test_preview.width;
            full_height = test_preview.height;
        }
    }
    
    // Fallback to typical dimensions if decode failed
    if (full_width == 0 || full_height == 0) {
        full_width = 1135;  // Typical PAL with padding
        full_height = 625;
        if (video_params->system == VideoSystem::NTSC) {
            full_height = 505;  // Typical NTSC with padding
        }
    }
    
    // Get active picture area dimensions from metadata
    // These are used to calculate the DAR correction, not for the preview dimensions
    uint32_t active_width = 702;   // Fallback PAL active picture width
    uint32_t active_height = 576;  // Fallback PAL active picture height
    
    if (video_params->active_video_start >= 0 && video_params->active_video_end > video_params->active_video_start) {
        active_width = video_params->active_video_end - video_params->active_video_start;
    }
    if (video_params->first_active_frame_line >= 0 && video_params->last_active_frame_line > video_params->first_active_frame_line) {
        active_height = video_params->last_active_frame_line - video_params->first_active_frame_line;
    }
    
    // Calculate DAR correction based on active area for 4:3 display
    // The active picture area should display at 4:3 aspect ratio
    // Example: PAL 702x576 active → target ratio 4:3 = 1.333
    //          Current ratio: 702/576 = 1.219
    //          Need to multiply width by: 1.333/1.219 = 1.094 to reach proper 4:3
    double active_ratio = static_cast<double>(active_width) / static_cast<double>(active_height);
    double target_ratio = 4.0 / 3.0;
    double dar_correction = target_ratio / active_ratio;
    
    ORC_LOG_DEBUG("ChromaSink: Preview dimensions: {}x{} (full frame), active area ~{}x{} (ratio={:.3f}), DAR correction = {:.3f} (target ratio=1.333)", 
                  full_width, full_height, active_width, active_height, active_ratio, dar_correction);
    
    // Only offer Frame mode for chroma decoder (fields are combined into RGB frames)
    std::vector<PreviewOption> options;
    options.push_back(PreviewOption{
        "frame", "Frame (RGB)", false, full_width, full_height, frame_count, dar_correction
    });
    
    return options;
}

PreviewImage ChromaSinkStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint [[maybe_unused]]) const
{
    PreviewImage result;
    
    ORC_LOG_DEBUG("ChromaSink: render_preview called for frame {}", index);
    
    if (!cached_input_ || option_id != "frame") {
        ORC_LOG_WARN("ChromaSink: Invalid preview request (cached_input={}, option='{}')", 
                     cached_input_ ? "valid" : "null", option_id);
        return result;
    }
    
    // Get video parameters
    auto video_params_opt = cached_input_->get_video_parameters();
    if (!video_params_opt) {
        return result;
    }
    const VideoParameters& videoParams = *video_params_opt;
    
    // Calculate first field offset
    uint64_t first_field_offset = 0;
    auto parity_hint = cached_input_->get_field_parity_hint(FieldID(0));
    if (parity_hint.has_value() && !parity_hint->is_first_field) {
        first_field_offset = 1;
    }
    
    // Get the two fields for this frame
    uint64_t field_a_index = first_field_offset + (index * 2);
    uint64_t field_b_index = field_a_index + 1;
    
    FieldID field_a(field_a_index);
    FieldID field_b(field_b_index);
    
    if (!cached_input_->has_field(field_a) || !cached_input_->has_field(field_b)) {
        return result;
    }
    
    // Convert both fields to SourceFields
    SourceField sourceField_a = convertToSourceField(cached_input_.get(), field_a);
    SourceField sourceField_b = convertToSourceField(cached_input_.get(), field_b);
    
    if (sourceField_a.data.empty() || sourceField_b.data.empty()) {
        return result;
    }
    
    // Determine decoder type
    std::string effectiveDecoderType = decoder_type_;
    if (effectiveDecoderType == "auto") {
        if (videoParams.system == VideoSystem::PAL || videoParams.system == VideoSystem::PAL_M) {
            effectiveDecoderType = "transform2d";
        } else {
            effectiveDecoderType = "ntsc2d";
        }
    }
    
    ORC_LOG_DEBUG("ChromaSink: decoder_type_='{}', effectiveDecoderType='{}'", decoder_type_, effectiveDecoderType);
    
    // Check if cached decoder matches current configuration
    if (!preview_decoder_cache_.matches_config(effectiveDecoderType, chroma_gain_, 
                                                chroma_phase_, luma_nr_, chroma_nr_,
                                                ntsc_phase_comp_, simple_pal_)) {
        // Configuration changed - clear old decoders and create new ones
        ORC_LOG_DEBUG("ChromaSink: Decoder config changed, recreating '{}' decoder", effectiveDecoderType);
        preview_decoder_cache_.mono_decoder.reset();
        preview_decoder_cache_.pal_decoder.reset();
        preview_decoder_cache_.ntsc_decoder.reset();
        preview_decoder_cache_.decoder_type = effectiveDecoderType;
        preview_decoder_cache_.chroma_gain = chroma_gain_;
        preview_decoder_cache_.chroma_phase = chroma_phase_;
        preview_decoder_cache_.luma_nr = luma_nr_;
        preview_decoder_cache_.chroma_nr = chroma_nr_;
        preview_decoder_cache_.ntsc_phase_comp = ntsc_phase_comp_;
        preview_decoder_cache_.simple_pal = simple_pal_;
        
        // Create appropriate decoder based on type
        if (effectiveDecoderType == "mono") {
            MonoDecoder::MonoConfiguration config;
            config.yNRLevel = luma_nr_;
            config.videoParameters = videoParams;
            preview_decoder_cache_.mono_decoder = std::make_unique<MonoDecoder>(config);
        } else if (effectiveDecoderType == "pal2d" || effectiveDecoderType == "transform2d" || effectiveDecoderType == "transform3d") {
            PalColour::Configuration config;
            config.chromaGain = chroma_gain_;
            config.chromaPhase = chroma_phase_;
            config.yNRLevel = luma_nr_;
            config.simplePAL = simple_pal_;
            config.showFFTs = false;
            
            if (effectiveDecoderType == "transform3d") {
                config.chromaFilter = PalColour::transform3DFilter;
            } else if (effectiveDecoderType == "transform2d") {
                config.chromaFilter = PalColour::transform2DFilter;
            } else {
                config.chromaFilter = PalColour::palColourFilter;
            }
            
            preview_decoder_cache_.pal_decoder = std::make_unique<PalColour>();
            preview_decoder_cache_.pal_decoder->updateConfiguration(videoParams, config);
        } else {
            // NTSC decoders
            Comb::Configuration config;
            config.chromaGain = chroma_gain_;
            config.chromaPhase = chroma_phase_;
            config.cNRLevel = chroma_nr_;
            config.yNRLevel = luma_nr_;
            config.phaseCompensation = ntsc_phase_comp_;
            config.showMap = false;
            
            if (effectiveDecoderType == "ntsc1d") {
                config.dimensions = 1;
                config.adaptive = false;
            } else if (effectiveDecoderType == "ntsc3d") {
                config.dimensions = 3;
                config.adaptive = true;
            } else if (effectiveDecoderType == "ntsc3dnoadapt") {
                config.dimensions = 3;
                config.adaptive = false;
            } else {
                config.dimensions = 2;
                config.adaptive = false;
            }
            
            preview_decoder_cache_.ntsc_decoder = std::make_unique<Comb>();
            preview_decoder_cache_.ntsc_decoder->updateConfiguration(videoParams, config);
        }
        ORC_LOG_DEBUG("ChromaSink: Created new '{}' decoder for preview", effectiveDecoderType);
    } else {
        ORC_LOG_DEBUG("ChromaSink: Reusing cached '{}' decoder", effectiveDecoderType);
    }
    
    // Decode the field pair using cached decoder
    std::vector<SourceField> fields = {sourceField_a, sourceField_b};
    std::vector<ComponentFrame> outputFrames(1);
    
    auto decode_start = std::chrono::high_resolution_clock::now();
    
    std::string active_decoder = "none";
    if (preview_decoder_cache_.mono_decoder) {
        active_decoder = "mono";
        preview_decoder_cache_.mono_decoder->decodeFrames(fields, 0, 2, outputFrames);
    } else if (preview_decoder_cache_.pal_decoder) {
        active_decoder = "pal";
        preview_decoder_cache_.pal_decoder->decodeFrames(fields, 0, 2, outputFrames);
    } else if (preview_decoder_cache_.ntsc_decoder) {
        active_decoder = "ntsc";
        preview_decoder_cache_.ntsc_decoder->decodeFrames(fields, 0, 2, outputFrames);
    }
    
    auto decode_end = std::chrono::high_resolution_clock::now();
    auto decode_ms = std::chrono::duration_cast<std::chrono::milliseconds>(decode_end - decode_start).count();
    ORC_LOG_DEBUG("ChromaSink: Frame {} decoded using '{}' decoder in {} ms", index, active_decoder, decode_ms);
    
    // Convert ComponentFrame YUV to RGB
    ComponentFrame& frame = outputFrames[0];
    int32_t width = frame.getWidth();
    int32_t height = frame.getHeight();
    
    if (width == 0 || height == 0) {
        ORC_LOG_WARN("ChromaSink: Frame {} decode failed ({}x{})", index, width, height);
        return result;
    }
    
    ORC_LOG_DEBUG("ChromaSink: Converting frame {} ({}x{}) YUV->RGB", index, width, height);
    
    // Get IRE levels for proper scaling
    double blackIRE = videoParams.black_16b_ire;
    double whiteIRE = videoParams.white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    
    // Create preview image
    result.width = width;
    result.height = height;
    result.rgb_data.resize(width * height * 3);
    
    // Convert YUV to RGB (8-bit)
    for (int32_t y = 0; y < height; y++) {
        const double* yLine = frame.y(y);
        const double* uLine = frame.u(y);
        const double* vLine = frame.v(y);
        
        for (int32_t x = 0; x < width; x++) {
            double Y = yLine[x];
            double U = uLine[x];
            double V = vLine[x];
            
            // Scale Y'UV to 0-1 (from IRE range)
            const double yScale = 1.0 / ireRange;
            const double uvScale = 1.0 / ireRange;
            
            Y = (Y - blackIRE) * yScale;
            U = U * uvScale;
            V = V * uvScale;
            
            // BT.601 YUV to RGB conversion
            double R = Y + 1.402 * V;
            double G = Y - 0.344136 * U - 0.714136 * V;
            double B = Y + 1.772 * U;
            
            // Clamp to 0-1 and convert to 8-bit
            R = std::max(0.0, std::min(1.0, R));
            G = std::max(0.0, std::min(1.0, G));
            B = std::max(0.0, std::min(1.0, B));
            
            size_t pixel_offset = (y * width + x) * 3;
            result.rgb_data[pixel_offset + 0] = static_cast<uint8_t>(R * 255.0);
            result.rgb_data[pixel_offset + 1] = static_cast<uint8_t>(G * 255.0);
            result.rgb_data[pixel_offset + 2] = static_cast<uint8_t>(B * 255.0);
        }
    }
    
    return result;
}

} // namespace orc
