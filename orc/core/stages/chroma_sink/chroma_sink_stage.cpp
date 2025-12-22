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
    const std::vector<ArtifactPtr>& inputs [[maybe_unused]],
    const std::map<std::string, ParameterValue>& parameters [[maybe_unused]])
{
    ORC_LOG_INFO("ChromaSink: Trigger called (not implemented yet)");
    
    // TODO: Implement in Step 4
    // For now, just log and return success
    
    trigger_status_ = "Chroma sink triggered (implementation pending)";
    
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

} // namespace orc
