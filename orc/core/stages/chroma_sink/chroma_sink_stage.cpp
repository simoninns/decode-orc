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
    , adaptive_preview_quality_(true)  // Enabled by default
{
}

ChromaSinkStage::~ChromaSinkStage() {
    // Cancel any pending quality upgrade
    quality_upgrade_pending_ = false;
    if (quality_upgrade_thread_.joinable()) {
        quality_upgrade_thread_.join();
    }
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
            {{}, {}, {}, {"auto", "pal2d", "transform2d", "transform3d", "ntsc1d", "ntsc2d", "ntsc3d", "ntsc3dnoadapt", "mono"}, false}
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
        },
        ParameterDescriptor{
            "adaptive_preview_quality",
            "Adaptive Preview Quality",
            "Use fast decoder (2D) during navigation, upgrade to high quality (3D) after 250ms pause",
            ParameterType::BOOL,
            {{}, {}, true, {}, false}
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
    params["adaptive_preview_quality"] = adaptive_preview_quality_;
    return params;
}

bool ChromaSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    for (const auto& [key, value] : params) {
        if (key == "output_path") {
            if (std::holds_alternative<std::string>(value)) {
                output_path_ = std::get<std::string>(value);
            }
        } else if (key == "decoder_type") {
            if (std::holds_alternative<std::string>(value)) {
                decoder_type_ = std::get<std::string>(value);
            }
        } else if (key == "output_format") {
            if (std::holds_alternative<std::string>(value)) {
                output_format_ = std::get<std::string>(value);
            }
        } else if (key == "chroma_gain") {
            if (std::holds_alternative<double>(value)) {
                chroma_gain_ = std::get<double>(value);
            }
        } else if (key == "chroma_phase") {
            if (std::holds_alternative<double>(value)) {
                chroma_phase_ = std::get<double>(value);
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
                luma_nr_ = std::get<double>(value);
            }
        } else if (key == "chroma_nr") {
            if (std::holds_alternative<double>(value)) {
                chroma_nr_ = std::get<double>(value);
            }
        } else if (key == "ntsc_phase_comp") {
            if (std::holds_alternative<bool>(value)) {
                ntsc_phase_comp_ = std::get<bool>(value);
            }
        } else if (key == "simple_pal") {
            if (std::holds_alternative<bool>(value)) {
                simple_pal_ = std::get<bool>(value);
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
        } else if (key == "adaptive_preview_quality") {
            if (std::holds_alternative<bool>(value)) {
                adaptive_preview_quality_ = std::get<bool>(value);
            }
        }
    }
    
    // Parameters updated
    
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
    
    uint32_t width = video_params->field_width;
    uint32_t height = video_params->field_height;
    uint64_t frame_count = field_count / 2;
    double dar_correction = 0.7;
    
    // Only offer Frame mode for chroma decoder (fields are combined into RGB frames)
    std::vector<PreviewOption> options;
    options.push_back(PreviewOption{
        "frame", "Frame (RGB)", false, width, height * 2, frame_count, dar_correction
    });
    
    return options;
}

PreviewImage ChromaSinkStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    PreviewImage result;
    
    ORC_LOG_DEBUG("ChromaSink render_preview: option_id='{}', index={}, hint={}", 
                 option_id, index, (hint == PreviewNavigationHint::Sequential ? "Sequential" : "Random"));
    
    if (!cached_input_ || option_id != "frame") {
        ORC_LOG_WARN("ChromaSink render_preview: Invalid input or option");
        return result;
    }
    
    // Check if input changed (invalidate cache)
    uint64_t current_input_id = reinterpret_cast<uint64_t>(cached_input_.get());
    if (current_input_id != preview_cache_input_id_) {
        preview_cache_.clear();
        preview_cache_input_id_ = current_input_id;
        last_preview_frame_ = UINT64_MAX;
        ORC_LOG_DEBUG("ChromaSink: Preview cache cleared (input changed)");
    }
    
    uint64_t total_frames = cached_input_->field_count() / 2;
    
    // Determine navigation direction from previous frame for sequential hint
    int navigation_direction = 0;  // -1 = backward, 0 = first/unknown, +1 = forward
    
    if (hint == PreviewNavigationHint::Sequential && last_preview_frame_ != UINT64_MAX) {
        int64_t frame_delta = static_cast<int64_t>(index) - static_cast<int64_t>(last_preview_frame_);
        if (frame_delta > 0) {
            navigation_direction = 1;  // Forward
        } else if (frame_delta < 0) {
            navigation_direction = -1;  // Backward
        }
    }
    
    last_preview_frame_ = index;
    
    // Check cache first
    bool cache_hit = false;
    PreviewImage cached_result;
    for (const auto& entry : preview_cache_) {
        if (entry.frame_index == index) {
            cache_hit = true;
            cached_result = entry.image;
            ORC_LOG_DEBUG("ChromaSink: Preview cache HIT for frame {}", index);
            break;
        }
    }
    
    // Simple strategy based on explicit hint from GUI:
    // - Random hint: decode just this one frame (responsive scrubbing)
    // - Sequential hint + cache miss: decode batch ahead/behind (prepare for continued navigation)
    // - Sequential hint + cache hit: pre-fetch next batch when getting close to edge
    
    if (!cache_hit) {
        if (hint == PreviewNavigationHint::Sequential && navigation_direction == 1) {
            // Sequential forward: decode batch ahead
            uint64_t batch_count = std::min(PREVIEW_BATCH_SIZE, total_frames - index);
            ORC_LOG_DEBUG("ChromaSink: Cache miss + forward navigation, decoding batch from frame {} (count={})", index, batch_count);
            decode_preview_batch(index, batch_count);
        } else if (hint == PreviewNavigationHint::Sequential && navigation_direction == -1) {
            // Sequential backward: decode batch behind
            uint64_t batch_start = (index >= PREVIEW_BATCH_SIZE) ? index - PREVIEW_BATCH_SIZE + 1 : 0;
            uint64_t batch_count = index - batch_start + 1;
            ORC_LOG_DEBUG("ChromaSink: Cache miss + backward navigation, decoding batch from frame {} (count={})", batch_start, batch_count);
            decode_preview_batch(batch_start, batch_count);
        } else {
            // Random scrubbing or first sequential request: decode only the requested frame for instant response
            ORC_LOG_DEBUG("ChromaSink: Cache miss + random/scrubbing, decoding single frame {}", index);
            decode_preview_batch(index, 1);
        }
    } else if (hint == PreviewNavigationHint::Sequential) {
        // Cache hit with sequential navigation - pre-fetch the next batch
        if (navigation_direction == 1) {
            // Forward navigation: find maximum cached frame and pre-fetch ahead
            uint64_t max_cached_frame = 0;
            for (const auto& entry : preview_cache_) {
                max_cached_frame = std::max(max_cached_frame, entry.frame_index);
            }
            
            uint64_t frames_ahead = max_cached_frame - index;
            if (frames_ahead < PREVIEW_PREFETCH_THRESHOLD && max_cached_frame + 1 < total_frames) {
                uint64_t batch_count = std::min(PREVIEW_BATCH_SIZE, total_frames - max_cached_frame - 1);
                ORC_LOG_DEBUG("ChromaSink: Pre-fetching next batch (current={}, max_cached={}, frames_ahead={}, count={})",
                             index, max_cached_frame, frames_ahead, batch_count);
                decode_preview_batch(max_cached_frame + 1, batch_count);
            }
        } else if (navigation_direction == -1) {
            // Backward navigation: find minimum cached frame and pre-fetch behind
            uint64_t min_cached_frame = UINT64_MAX;
            for (const auto& entry : preview_cache_) {
                min_cached_frame = std::min(min_cached_frame, entry.frame_index);
            }
            
            uint64_t frames_behind = (min_cached_frame != UINT64_MAX) ? index - min_cached_frame : 0;
            if (min_cached_frame != UINT64_MAX && frames_behind < PREVIEW_PREFETCH_THRESHOLD && min_cached_frame > 0) {
                uint64_t prefetch_start = (min_cached_frame >= PREVIEW_BATCH_SIZE) ? 
                                         min_cached_frame - PREVIEW_BATCH_SIZE : 0;
                uint64_t batch_count = min_cached_frame - prefetch_start;
                ORC_LOG_DEBUG("ChromaSink: Pre-fetching previous batch (current={}, min_cached={}, frames_behind={}, start={}, count={})",
                             index, min_cached_frame, frames_behind, prefetch_start, batch_count);
                decode_preview_batch(prefetch_start, batch_count);
            }
        }
    }
    
    // Return the result
    if (cache_hit) {
        // Start adaptive quality upgrade timer if enabled
        if (adaptive_preview_quality_ && hint == PreviewNavigationHint::Sequential) {
            schedule_quality_upgrade(index);
        }
        return cached_result;
    }
    
    // If it was a cache miss, the decode should have populated it - check again
    for (const auto& entry : preview_cache_) {
        if (entry.frame_index == index) {
            // Start adaptive quality upgrade timer if enabled
            if (adaptive_preview_quality_ && hint == PreviewNavigationHint::Sequential) {
                schedule_quality_upgrade(index);
            }
            return entry.image;
        }
    }
    
    // Still not in cache - decode failed
    ORC_LOG_WARN("ChromaSink render_preview: Frame decode failed for frame {}", index);
    return result;
}

std::shared_ptr<VideoFieldRepresentation> ChromaSinkStage::decode_field_pair_to_rgb(
    FieldID field_a,
    FieldID field_b) const
{
    if (!cached_input_) {
        return nullptr;
    }
    
    // Get video parameters
    auto video_params_opt = cached_input_->get_video_parameters();
    if (!video_params_opt) {
        return nullptr;
    }
    const VideoParameters& videoParams = *video_params_opt;
    
    // Convert both fields to SourceFields
    SourceField sourceField_a = convertToSourceField(cached_input_.get(), field_a);
    SourceField sourceField_b = convertToSourceField(cached_input_.get(), field_b);
    
    if (sourceField_a.data.empty() || sourceField_b.data.empty()) {
        return nullptr;
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
    
    // Check if cached decoder matches current configuration
    if (!preview_decoder_cache_.matches_config(effectiveDecoderType, chroma_gain_, 
                                                chroma_phase_, luma_nr_, chroma_nr_,
                                                ntsc_phase_comp_, simple_pal_)) {
        // Configuration changed - clear old decoders and create new ones
        ORC_LOG_DEBUG("ChromaSink: Decoder configuration changed, recreating decoder (type={})", effectiveDecoderType);
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
    }
    
    // Decode the field pair using cached decoder
    std::vector<SourceField> fields = {sourceField_a, sourceField_b};
    std::vector<ComponentFrame> outputFrames(1);
    
    if (preview_decoder_cache_.mono_decoder) {
        preview_decoder_cache_.mono_decoder->decodeFrames(fields, 0, 2, outputFrames);
    } else if (preview_decoder_cache_.pal_decoder) {
        preview_decoder_cache_.pal_decoder->decodeFrames(fields, 0, 2, outputFrames);
    } else if (preview_decoder_cache_.ntsc_decoder) {
        preview_decoder_cache_.ntsc_decoder->decodeFrames(fields, 0, 2, outputFrames);
    }
    
    // Convert ComponentFrame YUV to RGB
    ComponentFrame& frame = outputFrames[0];
    int32_t width = frame.getWidth();
    int32_t height = frame.getHeight();
    
    // Get IRE levels for proper scaling
    double blackIRE = videoParams.black_16b_ire;
    double whiteIRE = videoParams.white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    
    // Create RGB data as 16-bit samples (R,G,B triplets)
    std::vector<uint16_t> rgbData(width * height * 3);
    
    // Convert YUV to RGB
    for (int32_t y = 0; y < height; y++) {
        const double* yLine = frame.y(y);
        const double* uLine = frame.u(y);
        const double* vLine = frame.v(y);
        
        for (int32_t x = 0; x < width; x++) {
            double Y = yLine[x];
            double U = uLine[x];
            double V = vLine[x];
            
            // Scale Y'UV to 0-65535 (from IRE range)
            const double yScale = 65535.0 / ireRange;
            const double uvScale = 65535.0 / ireRange;
            
            const double rY = std::max(0.0, std::min(65535.0, (Y - blackIRE) * yScale));
            const double rU = U * uvScale;
            const double rV = V * uvScale;
            
            // Convert Y'UV to R'G'B'
            double R = rY + (1.139883 * rV);
            double G = rY + (-0.394642 * rU) + (-0.580622 * rV);
            double B = rY + (2.032062 * rU);
            
            // Clamp and store as 16-bit
            R = std::max(0.0, std::min(65535.0, R));
            G = std::max(0.0, std::min(65535.0, G));
            B = std::max(0.0, std::min(65535.0, B));
            
            int offset = (y * width + x) * 3;
            rgbData[offset + 0] = static_cast<uint16_t>(R);
            rgbData[offset + 1] = static_cast<uint16_t>(G);
            rgbData[offset + 2] = static_cast<uint16_t>(B);
        }
    }
    
    // Create a simple RGB field representation (inline class)
    class RGBFieldRepresentation : public VideoFieldRepresentation {
    private:
        FieldID field_a_;
        FieldID field_b_;
        FieldDescriptor descriptor_;
        std::vector<uint16_t> data_;
        VideoParameters video_params_;
        
    public:
        RGBFieldRepresentation(FieldID fa, FieldID fb, const FieldDescriptor& desc, 
                              std::vector<uint16_t> data, const VideoParameters& vp)
            : VideoFieldRepresentation(
                ArtifactID("chroma_preview_" + std::to_string(fa.value())),
                Provenance{
                    "ChromaSinkStage",
                    "1.0",
                    {},
                    {},
                    std::chrono::system_clock::now(),
                    "",
                    "",
                    {}
                }
              ),
              field_a_(fa), field_b_(fb), descriptor_(desc), data_(std::move(data)), video_params_(vp) {}
        
        FieldIDRange field_range() const override {
            return {field_a_, FieldID(field_b_.value() + 1)};
        }
        
        size_t field_count() const override { return 2; }
        
        bool has_field(FieldID id) const override { 
            return id == field_a_ || id == field_b_; 
        }
        
        std::optional<FieldDescriptor> get_descriptor(FieldID id) const override {
            if (id == field_a_ || id == field_b_) return descriptor_;
            return std::nullopt;
        }
        
        const sample_type* get_line(FieldID id, size_t line) const override {
            if ((id != field_a_ && id != field_b_) || line >= descriptor_.height) return nullptr;
            return &data_[line * descriptor_.width * 3];
        }
        
        std::vector<sample_type> get_field(FieldID id) const override {
            if (id == field_a_ || id == field_b_) return data_;
            return {};
        }
        
        std::optional<VideoParameters> get_video_parameters() const override {
            return video_params_;
        }
        
        std::string type_name() const override { return "RGBFieldRepresentation"; }
    };
    
    // Create field descriptor
    auto desc_opt = cached_input_->get_descriptor(field_a);
    if (!desc_opt) {
        return nullptr;
    }
    
    FieldDescriptor rgbDesc = *desc_opt;
    rgbDesc.width = width;
    rgbDesc.height = height;
    
    return std::make_shared<RGBFieldRepresentation>(field_a, field_b, rgbDesc, std::move(rgbData), videoParams);
}

void ChromaSinkStage::decode_preview_batch(uint64_t start_frame, uint64_t frame_count) const
{
    if (!cached_input_ || frame_count == 0) {
        return;
    }
    
    auto batch_start_time = std::chrono::high_resolution_clock::now();
    
    ORC_LOG_INFO("ChromaSink: Decoding preview batch of {} frames starting at frame {}", 
                 frame_count, start_frame);
    
    // Get video parameters
    auto video_params_opt = cached_input_->get_video_parameters();
    if (!video_params_opt) {
        return;
    }
    const VideoParameters& videoParams = *video_params_opt;
    
    // Calculate first field offset
    uint64_t first_field_offset = 0;
    auto parity_hint = cached_input_->get_field_parity_hint(FieldID(0));
    if (parity_hint.has_value() && !parity_hint->is_first_field) {
        first_field_offset = 1;
    }
    
    auto field_conversion_start = std::chrono::high_resolution_clock::now();
    
    // Collect all source fields for the batch
    std::vector<SourceField> sourceFields;
    sourceFields.reserve(frame_count * 2);
    
    for (uint64_t i = 0; i < frame_count; ++i) {
        uint64_t frame_index = start_frame + i;
        uint64_t field_a_index = first_field_offset + (frame_index * 2);
        uint64_t field_b_index = field_a_index + 1;
        
        FieldID field_a(field_a_index);
        FieldID field_b(field_b_index);
        
        if (!cached_input_->has_field(field_a) || !cached_input_->has_field(field_b)) {
            continue;
        }
        
        sourceFields.push_back(convertToSourceField(cached_input_.get(), field_a));
        sourceFields.push_back(convertToSourceField(cached_input_.get(), field_b));
    }
    
    if (sourceFields.empty()) {
        return;
    }
    
    auto field_conversion_end = std::chrono::high_resolution_clock::now();
    auto field_conversion_ms = std::chrono::duration_cast<std::chrono::milliseconds>(field_conversion_end - field_conversion_start).count();
    ORC_LOG_DEBUG("ChromaSink: Field conversion took {} ms for {} fields", field_conversion_ms, sourceFields.size());
    
    auto decoder_creation_start = std::chrono::high_resolution_clock::now();
    
    // Create decoder instance based on type
    std::unique_ptr<MonoDecoder> monoDecoder;
    std::unique_ptr<PalColour> palDecoder;
    std::unique_ptr<Comb> ntscDecoder;
    
    // Determine decoder type
    std::string effectiveDecoderType = decoder_type_;
    if (effectiveDecoderType == "auto") {
        if (videoParams.system == VideoSystem::PAL || videoParams.system == VideoSystem::PAL_M) {
            effectiveDecoderType = "transform2d";
        } else {
            effectiveDecoderType = "ntsc2d";
        }
    }
    
    // Apply adaptive quality: use fast decoder for batch previews if enabled
    std::string requestedDecoder = effectiveDecoderType;
    if (adaptive_preview_quality_ && is_slow_decoder(effectiveDecoderType)) {
        std::string fastDecoder = get_fast_decoder_type(effectiveDecoderType);
        ORC_LOG_INFO("ChromaSink PREVIEW: Using FALLBACK decoder '{}' (requested: '{}') for responsive preview",
                     fastDecoder, requestedDecoder);
        effectiveDecoderType = fastDecoder;
    } else {
        ORC_LOG_INFO("ChromaSink PREVIEW: Using REQUESTED decoder '{}'", requestedDecoder);
    }
    
    // Create appropriate decoder
    if (effectiveDecoderType == "mono") {
        MonoDecoder::MonoConfiguration config;
        config.yNRLevel = luma_nr_;
        config.videoParameters = videoParams;
        monoDecoder = std::make_unique<MonoDecoder>(config);
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
        
        palDecoder = std::make_unique<PalColour>();
        palDecoder->updateConfiguration(videoParams, config);
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
        
        ntscDecoder = std::make_unique<Comb>();
        ntscDecoder->updateConfiguration(videoParams, config);
    }
    
    auto decoder_creation_end = std::chrono::high_resolution_clock::now();
    auto decoder_creation_ms = std::chrono::duration_cast<std::chrono::milliseconds>(decoder_creation_end - decoder_creation_start).count();
    ORC_LOG_DEBUG("ChromaSink: Decoder creation took {} ms", decoder_creation_ms);
    
    auto decoding_start = std::chrono::high_resolution_clock::now();
    
    // Decode all frames in one call
    size_t num_frames = sourceFields.size() / 2;
    std::vector<ComponentFrame> outputFrames(num_frames);
    
    if (monoDecoder) {
        monoDecoder->decodeFrames(sourceFields, 0, sourceFields.size(), outputFrames);
    } else if (palDecoder) {
        palDecoder->decodeFrames(sourceFields, 0, sourceFields.size(), outputFrames);
    } else if (ntscDecoder) {
        ntscDecoder->decodeFrames(sourceFields, 0, sourceFields.size(), outputFrames);
    }
    
    auto decoding_end = std::chrono::high_resolution_clock::now();
    auto decoding_ms = std::chrono::duration_cast<std::chrono::milliseconds>(decoding_end - decoding_start).count();
    ORC_LOG_DEBUG("ChromaSink: Actual decoding took {} ms for {} frames", decoding_ms, num_frames);
    
    auto rgb_conversion_start = std::chrono::high_resolution_clock::now();
    
    // Get IRE levels for proper scaling
    double blackIRE = videoParams.black_16b_ire;
    double whiteIRE = videoParams.white_16b_ire;
    double ireRange = whiteIRE - blackIRE;
    
    // Convert each decoded frame to RGB and cache
    for (size_t i = 0; i < num_frames; ++i) {
        uint64_t frame_index = start_frame + i;
        ComponentFrame& frame = outputFrames[i];
        
        int32_t width = frame.getWidth();
        int32_t height = frame.getHeight();
        
        if (width == 0 || height == 0) {
            ORC_LOG_WARN("ChromaSink: Failed to decode frame {} in batch", frame_index);
            continue;
        }
        
        // Create preview image
        PreviewImage preview;
        preview.width = width;
        preview.height = height;
        preview.rgb_data.resize(width * height * 3);
        
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
                preview.rgb_data[pixel_offset + 0] = static_cast<uint8_t>(R * 255.0);
                preview.rgb_data[pixel_offset + 1] = static_cast<uint8_t>(G * 255.0);
                preview.rgb_data[pixel_offset + 2] = static_cast<uint8_t>(B * 255.0);
            }
        }
        
        // Add to cache
        preview_cache_.emplace_back(PreviewCacheEntry{frame_index, std::move(preview)});
        
        ORC_LOG_DEBUG("ChromaSink: Cached preview for frame {}", frame_index);
    }
    
    auto rgb_conversion_end = std::chrono::high_resolution_clock::now();
    auto rgb_conversion_ms = std::chrono::duration_cast<std::chrono::milliseconds>(rgb_conversion_end - rgb_conversion_start).count();
    ORC_LOG_DEBUG("ChromaSink: RGB conversion took {} ms", rgb_conversion_ms);
    
    auto batch_end_time = std::chrono::high_resolution_clock::now();
    auto total_ms = std::chrono::duration_cast<std::chrono::milliseconds>(batch_end_time - batch_start_time).count();
    
    ORC_LOG_INFO("ChromaSink: Batch decode complete, {} frames now in cache (total time: {} ms)", 
                 preview_cache_.size(), total_ms);
}

// Helper: Determine fast decoder type for adaptive preview
std::string ChromaSinkStage::get_fast_decoder_type(const std::string& requested_type) const
{
    // Map slow 3D decoders to their fast 2D equivalents
    if (requested_type == "transform3d") {
        return "transform2d";
    } else if (requested_type == "ntsc3d" || requested_type == "ntsc3dnoadapt") {
        return "ntsc2d";
    }
    // Already fast or unknown - return as-is
    return requested_type;
}

// Helper: Check if decoder is a slow 3D variant
bool ChromaSinkStage::is_slow_decoder(const std::string& decoder_type) const
{
    return decoder_type == "transform3d" || 
           decoder_type == "ntsc3d" || 
           decoder_type == "ntsc3dnoadapt";
}

// Helper: Schedule quality upgrade after navigation stops
void ChromaSinkStage::schedule_quality_upgrade(uint64_t frame_index) const
{
    // Only upgrade if using a slow decoder
    if (!is_slow_decoder(decoder_type_)) {
        return;
    }
    
    // Cancel any pending upgrade
    quality_upgrade_pending_ = false;
    if (quality_upgrade_thread_.joinable()) {
        quality_upgrade_thread_.join();
    }
    
    // Record this preview request
    last_preview_request_frame_ = frame_index;
    last_preview_request_time_ = std::chrono::steady_clock::now();
    
    // Start upgrade thread
    quality_upgrade_pending_ = true;
    quality_upgrade_thread_ = std::thread([this, frame_index]() {
        // Wait for the delay period
        std::this_thread::sleep_for(std::chrono::milliseconds(QUALITY_UPGRADE_DELAY_MS));
        
        // Check if we should still upgrade (no new requests came in)
        if (!quality_upgrade_pending_) {
            return;  // Cancelled
        }
        
        auto now = std::chrono::steady_clock::now();
        auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_preview_request_time_).count();
        
        // Verify this is still the current frame and enough time has passed
        if (last_preview_request_frame_ != frame_index || elapsed_ms < QUALITY_UPGRADE_DELAY_MS) {
            quality_upgrade_pending_ = false;
            return;  // User navigated away or timing issue
        }
        
        ORC_LOG_DEBUG("ChromaSink: Upgrading frame {} to high quality after {}ms pause", 
                     frame_index, elapsed_ms);
        
        // Clear only this frame from cache so it gets re-decoded with high quality
        auto& cache = const_cast<std::vector<PreviewCacheEntry>&>(preview_cache_);
        cache.erase(std::remove_if(cache.begin(), cache.end(),
                                   [frame_index](const PreviewCacheEntry& entry) {
                                       return entry.frame_index == frame_index;
                                   }), cache.end());
        
        // Force re-decode with high quality decoder (temporarily disable adaptive mode)
        bool original_adaptive = const_cast<ChromaSinkStage*>(this)->adaptive_preview_quality_;
        const_cast<ChromaSinkStage*>(this)->adaptive_preview_quality_ = false;
        
        decode_preview_batch(frame_index, 1);
        
        const_cast<ChromaSinkStage*>(this)->adaptive_preview_quality_ = original_adaptive;
        quality_upgrade_pending_ = false;
        
        ORC_LOG_INFO("ChromaSink PREVIEW: Quality upgrade complete - frame {} now using REQUESTED decoder", frame_index);
    });
}

} // namespace orc
