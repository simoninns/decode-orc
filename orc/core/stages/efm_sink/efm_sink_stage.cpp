/*
 * File:        efm_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     EFM Decoder Sink Stage - decodes EFM t-values to audio WAV or data sectors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "efm_sink_stage.h"
#include "efm_processor.h"
#include "logging.h"
#include <common_types.h>
#include <stage_registry.h>
#include <stdexcept>

namespace orc {

// Register this stage with the registry
ORC_REGISTER_STAGE(EFMSinkStage)

// Force linker to include this object file
void force_link_EFMSinkStage() {}

EFMSinkStage::EFMSinkStage() = default;

NodeTypeInfo EFMSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::SINK,
        "EFMSink",
        "EFM Decoder Sink",
        "Decodes EFM t-values from the VFR using the full EFM decode pipeline "
        "(EFM -> audio WAV or ECMA-130 binary sector data)",
        1, 1,  // One input
        0, 0,  // No outputs (sink)
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> EFMSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context
) {
    // Sink stages don't produce outputs in execute()
    // Actual work happens in trigger()
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    return {};
}

std::vector<ParameterDescriptor> EFMSinkStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType source_type) const {
    (void)project_format;
    (void)source_type;
    std::vector<ParameterDescriptor> descriptors;

    // output_path
    {
        ParameterDescriptor desc;
        desc.name = "output_path";
        desc.display_name = "Output File";
        desc.description = "Path to the decoded output file (.wav for audio mode, .bin for data mode)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = true;
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".wav";
        descriptors.push_back(desc);
    }

    // decode_mode  (STRING with allowed_strings acts as an enum combo-box in the GUI)
    {
        ParameterDescriptor desc;
        desc.name = "decode_mode";
        desc.display_name = "Decode Mode";
        desc.description = "Select audio decoding (outputs WAV/PCM) or data decoding (outputs ECMA-130 sectors)";
        desc.type = ParameterType::STRING;
        desc.constraints.required = true;
        desc.constraints.allowed_strings = {"audio", "data"};
        desc.constraints.default_value = std::string("audio");
        descriptors.push_back(desc);
    }

    // no_timecodes  (audio + data)
    {
        ParameterDescriptor desc;
        desc.name = "no_timecodes";
        desc.display_name = "No Timecodes";
        desc.description = "Disable timecode output";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        descriptors.push_back(desc);
    }

    // audacity_labels  (audio only)
    {
        ParameterDescriptor desc;
        desc.name = "audacity_labels";
        desc.display_name = "Audacity Labels";
        desc.description = "Write an Audacity label file alongside the audio output";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"audio"}};
        descriptors.push_back(desc);
    }

    // no_audio_concealment  (audio only)
    {
        ParameterDescriptor desc;
        desc.name = "no_audio_concealment";
        desc.display_name = "Disable Audio Concealment";
        desc.description = "Disable interpolation-based audio error concealment";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"audio"}};
        descriptors.push_back(desc);
    }

    // zero_pad  (audio only)
    {
        ParameterDescriptor desc;
        desc.name = "zero_pad";
        desc.display_name = "Zero-Pad Audio";
        desc.description = "Zero-pad missing or short audio samples instead of skipping";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"audio"}};
        descriptors.push_back(desc);
    }

    // no_wav_header  (audio only)
    {
        ParameterDescriptor desc;
        desc.name = "no_wav_header";
        desc.display_name = "Raw PCM (No WAV Header)";
        desc.description = "Output raw PCM samples without a WAV header";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"audio"}};
        descriptors.push_back(desc);
    }

    // output_metadata  (data only)
    {
        ParameterDescriptor desc;
        desc.name = "output_metadata";
        desc.display_name = "Output Bad Sector Map";
        desc.description = "Write a bad-sector map metadata file alongside the sector output";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"data"}};
        descriptors.push_back(desc);
    }

    // report
    {
        ParameterDescriptor desc;
        desc.name = "report";
        desc.display_name = "Write Decode Report";
        desc.description = "Write a detailed decode statistics report file";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        descriptors.push_back(desc);
    }

    return descriptors;
}

std::map<std::string, ParameterValue> EFMSinkStage::get_parameters() const {
    return parameters_;
}

bool EFMSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

std::string EFMSinkStage::get_trigger_status() const {
    return last_status_;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool get_bool_param(const std::map<std::string, ParameterValue>& params,
                            const std::string& name, bool default_val = false) {
    auto it = params.find(name);
    if (it == params.end()) return default_val;
    if (const bool* b = std::get_if<bool>(&it->second)) return *b;
    return default_val;
}

static std::string get_string_param(const std::map<std::string, ParameterValue>& params,
                                     const std::string& name,
                                     const std::string& default_val = "") {
    auto it = params.find(name);
    if (it == params.end()) return default_val;
    if (const std::string* s = std::get_if<std::string>(&it->second)) return *s;
    return default_val;
}

// ---------------------------------------------------------------------------
// trigger()
// ---------------------------------------------------------------------------

bool EFMSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context) {
    (void)observation_context;
    is_processing_.store(true);
    cancel_requested_.store(false);

    try {
        // ------------------------------------------------------------------
        // 1. Validate input
        // ------------------------------------------------------------------
        if (inputs.empty()) {
            throw std::runtime_error("EFMSink requires one input (VideoFieldRepresentation)");
        }
        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            throw std::runtime_error("EFMSink input must be a VideoFieldRepresentation");
        }
        if (!vfr->has_efm()) {
            throw std::runtime_error("EFMSink: input VFR has no EFM data (no EFM file in source?)");
        }

        // ------------------------------------------------------------------
        // 2. Extract parameters
        // ------------------------------------------------------------------
        const std::string output_path = get_string_param(parameters, "output_path");
        if (output_path.empty()) {
            throw std::runtime_error("EFMSink: output_path parameter is required");
        }

        const std::string decode_mode  = get_string_param(parameters, "decode_mode", "audio");
        const bool audio_mode          = (decode_mode != "data");

        const bool no_timecodes         = get_bool_param(parameters, "no_timecodes");
        const bool audacity_labels      = get_bool_param(parameters, "audacity_labels");
        const bool no_audio_concealment = get_bool_param(parameters, "no_audio_concealment");
        const bool zero_pad             = get_bool_param(parameters, "zero_pad");
        const bool no_wav_header        = get_bool_param(parameters, "no_wav_header");
        const bool output_metadata      = get_bool_param(parameters, "output_metadata");
        const bool report               = get_bool_param(parameters, "report");

        ORC_LOG_INFO("EFMSink: mode={}, output={}", audio_mode ? "audio" : "data", output_path);

        // ------------------------------------------------------------------
        // 3. Count total EFM t-values (no buffer allocation)
        // ------------------------------------------------------------------
        const auto field_range  = vfr->field_range();
        const FieldID start_fid = field_range.start;
        const FieldID end_fid   = field_range.end;
        const uint64_t total_fields = end_fid.value() - start_fid.value();

        ORC_LOG_DEBUG("EFMSink: Counting EFM t-values across {} fields", total_fields);

        uint64_t total_tvalues = 0;
        for (FieldID fid = start_fid; fid < end_fid; ++fid) {
            total_tvalues += vfr->get_efm_sample_count(fid);
        }

        if (total_tvalues == 0) {
            throw std::runtime_error("EFMSink: no EFM t-values found in field range");
        }
        ORC_LOG_DEBUG("EFMSink: Total EFM t-values: {}", total_tvalues);

        // ------------------------------------------------------------------
        // 4. Accumulate all T-values into a flat contiguous buffer.
        //
        // Per-field streaming (1024-byte strides within each field) leaves a
        // partial chunk at the end of every field whose size is field_size%1024.
        // TvaluesToChannel's state machine has a >382-byte guard; if it is in
        // ExpectingSync state when a <382-byte tail chunk arrives it will wait
        // for the next chunk, which belongs to the NEXT field and therefore
        // starts at a different byte offset than the standalone's file-read path.
        // This misalignment can cause the second sync header to appear in a
        // different chunk than in the standalone, subtly changing the pipeline
        // state near CIRC delay-line gap boundaries and producing a few sectors
        // that differ from the golden reference.
        //
        // Accumulating ALL T-values first and then handing them to
        // processFromBuffer() guarantees strictly-aligned 1024-byte strides
        // across the full byte sequence, identical to the standalone's
        // processAllPipelines() file-read loop.
        // ------------------------------------------------------------------
        ORC_LOG_DEBUG("EFMSink: Accumulating {} T-values from {} fields", total_tvalues, total_fields);

        std::vector<uint8_t> efm_buffer;
        efm_buffer.reserve(total_tvalues);

        uint64_t tvalues_accumulated = 0;
        for (FieldID fid = start_fid; fid < end_fid; ++fid) {
            if (cancel_requested_.load()) {
                last_status_ = "Cancelled by user";
                ORC_LOG_WARN("EFMSink: {}", last_status_);
                is_processing_.store(false);
                return false;
            }

            auto samples = vfr->get_efm_samples(fid);
            efm_buffer.insert(efm_buffer.end(), samples.begin(), samples.end());
            tvalues_accumulated += samples.size();

            const uint64_t fields_done = fid.value() - start_fid.value() + 1;
            if (fields_done % 10 == 0 && progress_callback_) {
                progress_callback_(tvalues_accumulated, total_tvalues,
                                   "Buffering EFM: field " +
                                   std::to_string(fields_done) + "/" +
                                   std::to_string(total_fields));
            }
            if (fields_done % 200 == 0) {
                ORC_LOG_DEBUG("EFMSink: Buffered {}/{} fields ({} t-values)", fields_done, total_fields, tvalues_accumulated);
            }
        }

        ORC_LOG_DEBUG("EFMSink: Buffer complete: {} bytes", efm_buffer.size());

        // ------------------------------------------------------------------
        // 5. Configure EfmProcessor and run the full decode pipeline.
        //    processFromBuffer() feeds the buffer in strict 1024-byte strides
        //    through the same pipeline as the standalone reference, then
        //    flushes and closes all output writers.
        // ------------------------------------------------------------------
        EfmProcessor processor;
        processor.setAudioMode(audio_mode);
        processor.setNoTimecodes(no_timecodes);
        processor.setAudacityLabels(audacity_labels);
        processor.setNoAudioConcealment(no_audio_concealment);
        processor.setZeroPad(zero_pad);
        processor.setNoWavHeader(no_wav_header);
        processor.setOutputMetadata(output_metadata);
        processor.setReportOutput(report);

        processor.beginStream(output_path, static_cast<int64_t>(efm_buffer.size()));

        constexpr size_t CHUNK_SIZE = 1024;
        size_t offset = 0;
        while (offset < efm_buffer.size()) {
            if (cancel_requested_.load()) {
                last_status_ = "Cancelled by user";
                ORC_LOG_WARN("EFMSink: {}", last_status_);
                is_processing_.store(false);
                return false;
            }

            const size_t count = std::min(CHUNK_SIZE, efm_buffer.size() - offset);
            processor.pushChunk({efm_buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                                  efm_buffer.begin() + static_cast<std::ptrdiff_t>(offset + count)});
            offset += count;

            if (progress_callback_ && (offset % (CHUNK_SIZE * 64) == 0 || offset == efm_buffer.size())) {
                progress_callback_(offset, efm_buffer.size(), "Decoding EFM...");
            }
        }

        const bool ok = processor.finishStream();

        if (!ok) {
            throw std::runtime_error("EFMSink: EfmProcessor::finishStream() returned false");
        }

        if (progress_callback_) {
            progress_callback_(total_tvalues, total_tvalues, "Done");
        }

        last_status_ = "Success: EFM decoded to " + output_path;
        ORC_LOG_INFO("EFMSink: {}", last_status_);
        is_processing_.store(false);
        return true;

    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("EFMSink: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

} // namespace orc
