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
            }
        } else if (key == "length") {
            if (std::holds_alternative<int>(value)) {
                length_ = std::get<int>(value);
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
    
    ORC_LOG_DEBUG("ChromaSink: Parameters updated (decoder={}, format={})", 
                  decoder_type_, output_format_);
    
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
    
    ORC_LOG_DEBUG("ChromaSink: Video parameters: {}x{}, active_video {}-{}, active_frame_lines {}-{}", 
                  ldVideoParams.fieldWidth, ldVideoParams.fieldHeight,
                  ldVideoParams.activeVideoStart, ldVideoParams.activeVideoEnd,
                  ldVideoParams.firstActiveFrameLine, ldVideoParams.lastActiveFrameLine);
    
    ORC_LOG_DEBUG("ChromaSink: Color burst: {}-{}, IRE levels: white={} black={}", 
                  ldVideoParams.colourBurstStart, ldVideoParams.colourBurstEnd,
                  ldVideoParams.white16bIre, ldVideoParams.black16bIre);
    
    ORC_LOG_DEBUG("ChromaSink: Sample rate: {}, fSC: {}, isSubcarrierLocked: {}", 
                  ldVideoParams.sampleRate, ldVideoParams.fSC, ldVideoParams.isSubcarrierLocked);
    
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
    
    size_t start_frame = (start_frame_ > 0) ? (start_frame_ - 1) : 0;  // Convert to 0-based
    size_t num_frames = (length_ > 0) ? length_ : (total_frames - start_frame);
    size_t end_frame = std::min(start_frame + num_frames, total_frames);
    
    ORC_LOG_INFO("ChromaSink: Processing frames {} to {} (of {})", 
                 start_frame + 1, end_frame, total_frames);
    
    // Debug: Check field range
    auto field_range = vfr->field_range();
    ORC_LOG_DEBUG("ChromaSink: Field range is {}-{}", field_range.start.value(), field_range.end.value());
    
    // 6. Determine field ordering from first field's parity hint
    bool is_first_field_first = true;  // Default assumption
    if (field_range.start.value() < field_range.end.value()) {
        auto first_parity_hint = vfr->get_field_parity_hint(field_range.start);
        if (first_parity_hint.has_value()) {
            // Check if field 0 is marked as "first field"
            is_first_field_first = first_parity_hint->is_first_field;
            ORC_LOG_DEBUG("ChromaSink: Field ordering determined from metadata: is_first_field_first={}", 
                         is_first_field_first);
        } else {
            ORC_LOG_WARN("ChromaSink: No field parity hint available, assuming first field first");
        }
    }
    
    // 7. Create dummy LdDecodeMetaData for decoder compatibility
    LdDecodeMetaData metadata;
    // Note: In Step 4, we'll remove this and use VFR directly
    
    // 7. Collect all fields for batch decoding
    QVector<SourceField> inputFields;
    inputFields.reserve((end_frame - start_frame) * 2);
    
    ORC_LOG_INFO("ChromaSink: Collecting {} fields for decode", (end_frame - start_frame) * 2);
    
    for (size_t frame = start_frame; frame < end_frame; frame++) {
        // Determine field IDs using the same algorithm as LdDecodeMetaData::getFieldNumber()
        // Frame numbering is 1-based in the algorithm, but our frame variable is 0-based
        int32_t frameNumber = frame + 1;  // Convert to 1-based
        
        FieldID firstFieldId, secondFieldId;
        
        if (is_first_field_first) {
            // Standard order: frame N uses fields (N*2-1, N*2) in 1-based numbering
            // Convert to 0-based: fields (N*2-2, N*2-1)
            firstFieldId = FieldID((frameNumber * 2) - 2);
            secondFieldId = FieldID((frameNumber * 2) - 1);
        } else {
            // Reversed order: frame N uses fields (N*2, N*2-1) in 1-based numbering
            // Convert to 0-based: fields (N*2-1, N*2-2)
            secondFieldId = FieldID((frameNumber * 2) - 2);
            firstFieldId = FieldID((frameNumber * 2) - 1);
        }
        
        // Scan forward to find actual field with is_first_field=true
        // (handles dropped/repeated fields in the source)
        bool found_first_field = false;
        FieldID scan_id = firstFieldId;
        int max_scan = 10;  // Don't scan too far
        
        for (int scan = 0; scan < max_scan && scan_id.value() < field_range.end.value(); scan++) {
            if (!vfr->has_field(scan_id)) {
                scan_id = FieldID(scan_id.value() + 1);
                continue;
            }
            
            auto parity_hint = vfr->get_field_parity_hint(scan_id);
            if (parity_hint.has_value() && parity_hint->is_first_field) {
                firstFieldId = scan_id;
                secondFieldId = FieldID(scan_id.value() + 1);
                found_first_field = true;
                break;
            }
            scan_id = FieldID(scan_id.value() + 1);
        }
        
        if (!found_first_field) {
            ORC_LOG_DEBUG("ChromaSink: Could not find first field via scan, using calculated IDs for frame {}", 
                         frame + 1);
        }
        
        ORC_LOG_DEBUG("ChromaSink: User frame {} -> fields {},{}", 
                      frame + 1, firstFieldId.value(), secondFieldId.value());
        
        // Check if fields exist
        if (!vfr->has_field(firstFieldId) || !vfr->has_field(secondFieldId)) {
            ORC_LOG_WARN("ChromaSink: Skipping frame {} (missing fields {}/{})", 
                        frame + 1, firstFieldId.value(), secondFieldId.value());
            continue;
        }
        
        // Convert fields to SourceField format
        SourceField sf1 = convertToSourceField(vfr.get(), firstFieldId, metadata);
        SourceField sf2 = convertToSourceField(vfr.get(), secondFieldId, metadata);
        
        inputFields.append(sf1);
        inputFields.append(sf2);
    }
    
    // 8. Prepare output buffer
    qint32 numFrames = inputFields.size() / 2;
    QVector<ComponentFrame> outputFrames;
    outputFrames.resize(numFrames);  // Just resize, don't init - decodeFrames will do that
    
    ORC_LOG_INFO("ChromaSink: Decoding {} frames ({} fields)", numFrames, inputFields.size());
    
    // Debug: Log all fields being decoded
    for (int i = 0; i < inputFields.size(); i += 2) {
        if (i + 1 < inputFields.size()) {
            // Calculate checksum of first field in pair
            uint32_t checksum = 0;
            for (int j = 0; j < inputFields[i].data.size(); j++) {
                checksum += inputFields[i].data[j];
            }
            ORC_LOG_DEBUG("ChromaSink: Frame {} will be decoded from fields seqNo={},{} (checksum={}, first 4: {} {} {} {})", 
                          i/2, inputFields[i].field.seqNo, inputFields[i+1].field.seqNo,
                          checksum,
                          inputFields[i].data.size() > 0 ? inputFields[i].data[0] : 0,
                          inputFields[i].data.size() > 1 ? inputFields[i].data[1] : 0,
                          inputFields[i].data.size() > 2 ? inputFields[i].data[2] : 0,
                          inputFields[i].data.size() > 3 ? inputFields[i].data[3] : 0);
        }
    }
    
    // 9. Decode all frames in one batch
    if (monoDecoder) {
        monoDecoder->decodeFrames(inputFields, 0, inputFields.size(), outputFrames);
    } else if (palDecoder) {
        palDecoder->decodeFrames(inputFields, 0, inputFields.size(), outputFrames);
    } else if (ntscDecoder) {
        ntscDecoder->decodeFrames(inputFields, 0, inputFields.size(), outputFrames);
    }
    
    ORC_LOG_INFO("ChromaSink: Decoded {} frames", outputFrames.size());
    
    // 10. Convert to std::vector for output writer
    std::vector<ComponentFrame> stdOutputFrames;
    stdOutputFrames.reserve(outputFrames.size());
    for (const auto& frame : outputFrames) {
        stdOutputFrames.push_back(frame);
    }
    
    // 11. Write output file
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
    ORC_LOG_DEBUG("ChromaSink: Preview requested (returning input unchanged)");
    
    return input;
}

// Helper method: Convert VideoFieldRepresentation field to SourceField
SourceField ChromaSinkStage::convertToSourceField(
    const VideoFieldRepresentation* vfr,
    FieldID field_id,
    LdDecodeMetaData& metadata) const
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
    sf.field.isFirstField = (desc.parity == FieldParity::Top);
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
    
    ORC_LOG_DEBUG("ChromaSink: Converted field {} ({} samples)", 
                  static_cast<uint64_t>(field_id.value()), sf.data.size());
    
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
        
        ORC_LOG_DEBUG("ChromaSink: Writing frame {} (index {}), output size = {}", frameIdx + 1, frameIdx, outputFrame.size());
        
        // Debug: Log first RGB values (pixel 0) and a middle pixel
        if (outputFrame.size() >= 3) {
            ORC_LOG_DEBUG("ChromaSink: Frame {} Pixel 0: R={} G={} B={}", 
                          frameIdx + 1, outputFrame[0], outputFrame[1], outputFrame[2]);
        }
        if (outputFrame.size() >= 3000) {
            ORC_LOG_DEBUG("ChromaSink: Frame {} Pixel 1000: R={} G={} B={}", 
                          frameIdx + 1, outputFrame[3000], outputFrame[3001], outputFrame[3002]);
        }
        
        frameIdx++;
        
        // Write output data
        const char* data = reinterpret_cast<const char*>(outputFrame.constData());
        qint64 size = outputFrame.size() * sizeof(quint16);
        qint64 written = outputFile.write(data, size);
        
        ORC_LOG_DEBUG("ChromaSink: Wrote {} of {} bytes", written, size);
        
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
