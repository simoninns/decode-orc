/*
 * File:        chroma_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     Chroma Decoder Sink Stage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "chroma_sink_stage.h"
#include "stage_registry.h"
#include "logging.h"

// Decoder includes (relative to this file)
#include "decoders/sourcefield.h"
#include "decoders/lib/tbc/lddecodemetadata.h"
#include "decoders/monodecoder.h"
#include "decoders/palcolour.h"
#include "decoders/comb.h"
#include "decoders/outputwriter.h"
#include "decoders/componentframe.h"

#include <QFile>
#include <fstream>

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
    , reverse_fields_(false)
    , luma_nr_(0.0)
    , chroma_nr_(0.0)
    , ntsc_phase_comp_(false)
    , output_padding_(8)
{
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
    const std::vector<ArtifactPtr>& inputs [[maybe_unused]],
    const std::map<std::string, ParameterValue>& parameters [[maybe_unused]])
{
    // Sink stages don't produce outputs during normal execution
    // They are triggered manually to write data
    ORC_LOG_DEBUG("ChromaSink execute called (no-op - use trigger to export)");
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
            "reverse_fields",
            "Reverse Fields",
            "Reverse field order to second/first (default is first/second)",
            ParameterType::BOOL,
            {{}, {}, false, {}, false}
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
            "output_padding",
            "Output Padding",
            "Pad output to multiple of this many pixels on both axes. Range: 1-32",
            ParameterType::INT32,
            {1, 32, 8, {}, false}
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
    params["reverse_fields"] = reverse_fields_;
    params["luma_nr"] = luma_nr_;
    params["chroma_nr"] = chroma_nr_;
    params["ntsc_phase_comp"] = ntsc_phase_comp_;
    params["output_padding"] = output_padding_;
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
        } else if (key == "reverse_fields") {
            if (std::holds_alternative<bool>(value)) {
                reverse_fields_ = std::get<bool>(value);
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
        } else if (key == "output_padding") {
            if (std::holds_alternative<int>(value)) {
                output_padding_ = std::get<int>(value);
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
    
    // 3. Convert orc-core VideoParameters to LdDecodeMetaData::VideoParameters
    LdDecodeMetaData::VideoParameters ldVideoParams;
    const auto& orcParams = *video_params_opt;
    
    // Map video system
    if (orcParams.system == VideoSystem::PAL) {
        ldVideoParams.system = PAL;
    } else if (orcParams.system == VideoSystem::NTSC) {
        ldVideoParams.system = NTSC;
    } else {
        ldVideoParams.system = PAL_M;
    }
    
    ldVideoParams.fieldWidth = orcParams.field_width;
    ldVideoParams.fieldHeight = orcParams.field_height;
    ldVideoParams.sampleRate = orcParams.sample_rate;
    ldVideoParams.fSC = orcParams.fsc;
    ldVideoParams.isSubcarrierLocked = orcParams.is_subcarrier_locked;
    ldVideoParams.isWidescreen = orcParams.is_widescreen;
    
    // Map active region parameters (required for OutputWriter)
    ldVideoParams.activeVideoStart = orcParams.active_video_start;
    ldVideoParams.activeVideoEnd = orcParams.active_video_end;
    ldVideoParams.firstActiveFieldLine = orcParams.first_active_field_line;
    ldVideoParams.lastActiveFieldLine = orcParams.last_active_field_line;
    ldVideoParams.firstActiveFrameLine = orcParams.first_active_frame_line;
    ldVideoParams.lastActiveFrameLine = orcParams.last_active_frame_line;
    
    // Map color burst and IRE levels
    ldVideoParams.colourBurstStart = orcParams.colour_burst_start;
    ldVideoParams.colourBurstEnd = orcParams.colour_burst_end;
    ldVideoParams.white16bIre = orcParams.white_16b_ire;
    ldVideoParams.black16bIre = orcParams.black_16b_ire;
    
    ldVideoParams.isValid = true;
    
    // Apply default line parameters if they're not set (-1)
    // This will compute frame lines from field lines using system defaults
    LdDecodeMetaData::LineParameters lineParams;
    lineParams.applyTo(ldVideoParams);
    
    // Apply padding adjustments to active video region BEFORE configuring decoder
    // This ensures the decoder processes the correct region that will be written to output
    {
        OutputWriter::Configuration writerConfig;
        writerConfig.paddingAmount = 8;  // Same as used later for actual output
        
        // Create temporary output writer just to apply padding adjustments
        OutputWriter tempWriter;
        tempWriter.updateConfiguration(ldVideoParams, writerConfig);
        // ldVideoParams now has adjusted activeVideoStart/End values
    }
    
    // 4. Create appropriate decoder
    // Note: We'll use the decoder classes directly (synchronously)
    // without the threading infrastructure for now
    
    std::unique_ptr<MonoDecoder> monoDecoder;
    std::unique_ptr<PalColour> palDecoder;
    std::unique_ptr<Comb> ntscDecoder;
    
    bool useMonoDecoder = (decoder_type_ == "mono");
    bool usePalDecoder = (decoder_type_ == "auto" && ldVideoParams.system == PAL) ||
                         (decoder_type_ == "pal2d" || decoder_type_ == "transform2d" || decoder_type_ == "transform3d");
    bool useNtscDecoder = (decoder_type_ == "auto" && ldVideoParams.system == NTSC) ||
                          (decoder_type_.find("ntsc") == 0);
    
    if (useMonoDecoder) {
        MonoDecoder::MonoConfiguration config;
        config.yNRLevel = luma_nr_;
        config.videoParameters = ldVideoParams;
        monoDecoder = std::make_unique<MonoDecoder>(config);
        ORC_LOG_INFO("ChromaSink: Using decoder: mono");
    }
    else if (usePalDecoder) {
        PalColour::Configuration config;
        config.chromaGain = chroma_gain_;
        config.chromaPhase = chroma_phase_;
        config.yNRLevel = luma_nr_;
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
        palDecoder->updateConfiguration(ldVideoParams, config);
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
        ntscDecoder->updateConfiguration(ldVideoParams, config);
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
    qint32 lookBehindFrames = 0;
    qint32 lookAheadFrames = 0;
    
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
    qint32 extended_start_frame = static_cast<qint32>(start_frame) - lookBehindFrames;
    qint32 extended_end_frame = static_cast<qint32>(end_frame) + lookAheadFrames;
    
    // 8. Create dummy LdDecodeMetaData for decoder compatibility
    LdDecodeMetaData metadata;
    // Note: In Step 4, we'll remove this and use VFR directly
    
    // 9. Collect fields including lookbehind/lookahead padding
    QVector<SourceField> inputFields;
    qint32 total_fields_needed = (extended_end_frame - extended_start_frame) * 2;
    inputFields.reserve(total_fields_needed);
    
    ORC_LOG_INFO("ChromaSink: Collecting {} fields (frames {}-{}) for decode",
                 total_fields_needed, extended_start_frame + 1, extended_end_frame);
    
    for (qint32 frame = extended_start_frame; frame < extended_end_frame; frame++) {
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
        // Fields are ALWAYS in chronological order: 0, 1, 2, 3, 4, 5...
        // The first field of frame N is at index (2*N-2), second field at (2*N-1)
        FieldID firstFieldId = FieldID((metadataFrameNumber * 2) - 2);
        FieldID secondFieldId = FieldID((metadataFrameNumber * 2) - 1);
        
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
            sf1 = convertToSourceField(vfr.get(), firstFieldId, metadata);
            sf2 = convertToSourceField(vfr.get(), secondFieldId, metadata);
            
            // Fill with black
            uint16_t black = ldVideoParams.black16bIre;
            size_t field_length = sf1.data.size();
            sf1.data.fill(black, field_length);
            sf2.data.fill(black, field_length);
        } else {
            sf1 = convertToSourceField(vfr.get(), firstFieldId, metadata);
            sf2 = convertToSourceField(vfr.get(), secondFieldId, metadata);
            
            // Apply PAL subcarrier shift: With subcarrier-locked 4fSC PAL sampling,
            // we have four "extra" samples over the course of the frame, so the two
            // fields will be horizontally misaligned by two samples. Shift the
            // second field to the left to compensate.
            if ((ldVideoParams.system == PAL || ldVideoParams.system == PAL_M) && 
                ldVideoParams.isSubcarrierLocked) {
                // Remove first 2 samples and append 2 black samples at the end
                uint16_t black = ldVideoParams.black16bIre;
                sf2.data.remove(0, 2);
                sf2.data.append(black);
                sf2.data.append(black);
                if (frame < 3) {
                }
            }
        }
        
        inputFields.append(sf1);
        inputFields.append(sf2);
    }
    
    // 10. Process frames ONE AT A TIME to match standalone behavior
    // CRITICAL: Transform3D is a 3D temporal FFT filter that processes frames at specific
    // Z-positions (temporal indices). Each frame MUST be at the SAME Z-position (field indices
    // lookBehind*2 to lookBehind*2+2) regardless of its frame number, otherwise the FFT results
    // will differ. This matches how the standalone decoder processes frames.
    
    qint32 numFrames = (end_frame - start_frame);
    QVector<ComponentFrame> outputFrames;
    outputFrames.resize(numFrames);
    
    ORC_LOG_INFO("ChromaSink: Processing {} frames one at a time (to match standalone)", numFrames);
    
    for (qint32 frameIdx = 0; frameIdx < numFrames; frameIdx++) {
        // Build a field array for this ONE frame:
        // [lookbehind fields... target frame fields... lookahead fields...]
        QVector<SourceField> frameFields;
        qint32 frameStartIdx = frameIdx * 2;  // Position in inputFields where this frame's data starts
        qint32 lookbehindIdx = frameStartIdx;  // Where lookbehind starts in inputFields
        qint32 lookaheadEndIdx = frameStartIdx + 2 + (lookAheadFrames * 2);  // Where we need to read up to
        
        // Copy lookbehind + target + lookahead fields for this frame
        for (qint32 i = lookbehindIdx; i < lookaheadEndIdx && i < inputFields.size(); i++) {
            frameFields.append(inputFields[i]);
        }
        
        // The target frame is always at the same Z-position: after lookbehind fields
        qint32 frameStartIndex = lookBehindFrames * 2;
        qint32 frameEndIndex = frameStartIndex + 2;
        
        // Prepare single-frame output buffer
        QVector<ComponentFrame> singleOutput;
        singleOutput.resize(1);
        
        // Decoding frame with context fields
        
        // Decode this ONE frame
        if (monoDecoder) {
            monoDecoder->decodeFrames(frameFields, frameStartIndex, frameEndIndex, singleOutput);
        } else if (palDecoder) {
            palDecoder->decodeFrames(frameFields, frameStartIndex, frameEndIndex, singleOutput);
        } else if (ntscDecoder) {
            ntscDecoder->decodeFrames(frameFields, frameStartIndex, frameEndIndex, singleOutput);
        }
        
        // Store the result
        outputFrames[frameIdx] = singleOutput[0];
    }
    
    ORC_LOG_INFO("ChromaSink: Decoded {} frames", outputFrames.size());
    
    // DEBUG: Log ComponentFrame Y checksums using accessor method
    for (int k = 0; k < outputFrames.size() && k < 3; k++) {
        // Access Y data using line accessor
        qint32 firstLine = ldVideoParams.firstActiveFrameLine;
        const double* yLinePtr = outputFrames[k].y(firstLine);
        qint32 width = outputFrames[k].getWidth();
        
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
    if (!writeOutputFile(output_path_, output_format_, stdOutputFrames, &ldVideoParams)) {
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

std::shared_ptr<const VideoFieldRepresentation> 
ChromaSinkStage::render_preview_field(
    std::shared_ptr<const VideoFieldRepresentation> input,
    FieldID field_id [[maybe_unused]]) const
{
    // TODO: Implement in Step 9
    // For now, just return input unchanged (passthrough)
    // Preview not yet implemented - returning input unchanged
    
    return input;
}

// Helper method: Convert VideoFieldRepresentation field to SourceField
SourceField ChromaSinkStage::convertToSourceField(
    const VideoFieldRepresentation* vfr,
    FieldID field_id,
    LdDecodeMetaData& /*metadata*/) const
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
    // Note: seqNo must be 1-based to match LdDecodeMetaData field numbering
    // ORC uses 0-based FieldID, so add 1
    
    // Determine if this is the "first field" or "second field" from field parity
    // Field parity determines field ordering (same for both NTSC and PAL):
    //   - Top field (even field indices)    → first field
    //   - Bottom field (odd field indices)  → second field
    // This relationship is consistent across video systems.
    sf.field.isFirstField = (desc.parity == FieldParity::Top);
    
    ORC_LOG_TRACE("ChromaSink: Field {} parity={} → isFirstField={}",
                 field_id.value(),
                 desc.parity == FieldParity::Top ? "Top" : "Bottom",
                 sf.field.isFirstField);
    
    // Get field_phase_id from phase hint (from TBC metadata)
    auto phase_hint = vfr->get_field_phase_hint(field_id);
    if (phase_hint.has_value()) {
        sf.field.fieldPhaseID = phase_hint->field_phase_id;
        ORC_LOG_TRACE("ChromaSink: Field {} has fieldPhaseID={}", field_id.value(), sf.field.fieldPhaseID);
    } else {
        // Leave as default -1 (unknown)
        sf.field.fieldPhaseID = -1;
    }
    
    sf.field.seqNo = static_cast<qint32>(field_id.value()) + 1;
    
    ORC_LOG_TRACE("ChromaSink: Field {} (1-based seqNo={}) parity={} -> isFirstField={}", 
                  field_id.value(),
                  sf.field.seqNo,
                  (desc.parity == FieldParity::Top ? "Top" : "Bottom"),
                  sf.field.isFirstField);
    
    // Get field data
    std::vector<uint16_t> field_data = vfr->get_field(field_id);
    
    // Convert std::vector<uint16_t> to QVector<quint16>
    sf.data.reserve(field_data.size());
    for (uint16_t sample : field_data) {
        sf.data.append(sample);
    }
    
    // Apply PAL subcarrier-locked field shift (matches standalone decoder behavior)
    // With 4fSC PAL sampling, the two fields are misaligned by 2 samples
    // The second field needs to be shifted left by 2 samples
    const auto& videoParams = *vfr->get_video_parameters();
    bool isPal = (videoParams.system == VideoSystem::PAL || videoParams.system == VideoSystem::PAL_M);
    bool isSecondField = (desc.parity == FieldParity::Bottom);
    
    if (isPal && videoParams.is_subcarrier_locked && isSecondField) {
        // Shift second field left by 2 samples (remove first 2, add 2 black samples at end)
        sf.data.remove(0, 2);
        quint16 black = static_cast<quint16>(videoParams.black_16b_ire);
        sf.data.append(black);
        sf.data.append(black);
        ORC_LOG_TRACE("ChromaSink: Applied PAL subcarrier-locked shift to field {}", field_id.value());
    }
    
    // Log complete Field structure for debugging (first 6 fields only)
    if (field_id.value() < 6) {
        ORC_LOG_DEBUG("ChromaSink: Field {} FULL metadata:", field_id.value());
        ORC_LOG_DEBUG("  seqNo={} isFirstField={} fieldPhaseID={}", 
                      sf.field.seqNo, sf.field.isFirstField, sf.field.fieldPhaseID);
        ORC_LOG_DEBUG("  syncConf={} medianBurstIRE={:.2f} pad={}", 
                      sf.field.syncConf, sf.field.medianBurstIRE, sf.field.pad);
        ORC_LOG_DEBUG("  audioSamples={} diskLoc={:.1f} fileLoc={}", 
                      sf.field.audioSamples, sf.field.diskLoc, sf.field.fileLoc);
        ORC_LOG_DEBUG("  decodeFaults={} efmTValues={}", 
                      sf.field.decodeFaults, sf.field.efmTValues);
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
    const auto& videoParams = *static_cast<const LdDecodeMetaData::VideoParameters*>(videoParamsPtr);
    if (frames.empty()) {
        ORC_LOG_ERROR("ChromaSink: No frames to write");
        return false;
    }
    
    // Open output file
    QFile outputFile(QString::fromStdString(output_path));
    if (!outputFile.open(QIODevice::WriteOnly)) {
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
    LdDecodeMetaData::VideoParameters mutableParams = videoParams;
    writer.updateConfiguration(mutableParams, writerConfig);
    writer.printOutputInfo();  // Show output format info
    
    // Write stream header if needed
    QByteArray streamHeader = writer.getStreamHeader();
    if (!streamHeader.isEmpty()) {
        outputFile.write(streamHeader);
    }
    
    // Write frames
    int frameIdx = 0;
    for (const auto& frame : frames) {
        // Write frame header if needed
        QByteArray frameHeader = writer.getFrameHeader();
        if (!frameHeader.isEmpty()) {
            outputFile.write(frameHeader);
        }
        
        // Convert frame to output format
        OutputFrame outputFrame;
        writer.convert(frame, outputFrame);
        
        frameIdx++;
        
        // Write output data
        const char* data = reinterpret_cast<const char*>(outputFrame.constData());
        qint64 size = outputFrame.size() * sizeof(quint16);
        qint64 written = outputFile.write(data, size);
        
        if (written != size) {
            ORC_LOG_ERROR("ChromaSink: Failed to write frame data (wrote {} of {} bytes)", written, size);
            outputFile.close();
            return false;
        }
    }
    
    outputFile.close();
    
    ORC_LOG_INFO("ChromaSink: Wrote {} frames to {}", frames.size(), output_path);
    return true;
}

// Helper method: Create decoder based on type
} // namespace orc
