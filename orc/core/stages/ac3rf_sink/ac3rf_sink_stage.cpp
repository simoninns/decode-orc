/*
 * File:        ac3rf_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     AC3 RF Sink Stage - decodes AC3 RF samples and writes AC3 frames
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "ac3rf_sink_stage.h"
#include "logging.h"
#include <common_types.h>
#include <stage_registry.h>
#include <stdexcept>
#include <fstream>
#include <algorithm>

#include <Logger.h>
#include <ac3/Ac3RfDemodulator.h>
#include <filter/FirFilterStage.h>

namespace orc {

// Register this stage with the registry
ORC_REGISTER_STAGE(AC3RFSinkStage)

// Force linker to include this object file
void force_link_AC3RFSinkStage() {}

AC3RFSinkStage::AC3RFSinkStage() = default;

NodeTypeInfo AC3RFSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::SINK,
        "AC3RFSink",
        "AC3 RF Sink",
        "Decodes AC3 RF samples and writes AC3 frames to file",
        1, 1,  // One input
        0, 0,  // No outputs (sink)
        VideoFormatCompatibility::ALL
    };
}

std::vector<ArtifactPtr> AC3RFSinkStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context)
{
    // Sink stages produce no outputs in execute(); work happens in trigger()
    (void)inputs;
    (void)parameters;
    (void)observation_context;
    return {};
}

std::vector<ParameterDescriptor> AC3RFSinkStage::get_parameter_descriptors(
    VideoSystem project_format, SourceType source_type) const
{
    (void)project_format;
    (void)source_type;
    std::vector<ParameterDescriptor> descriptors;

    {
        ParameterDescriptor desc;
        desc.name = "output_path";
        desc.display_name = "Output AC3 File";
        desc.description = "Path to the output AC3 file";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = true;
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".ac3";
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "input_sample_rate";
        desc.display_name = "Input Sample Rate (Hz)";
        desc.description = "Sample rate of the AC3 RF capture (typically 40 MHz for ld-decode)";
        desc.type = ParameterType::DOUBLE;
        desc.constraints.required = false;
        desc.constraints.default_value = 40e6;
        desc.constraints.min_value = 1e6;
        desc.constraints.max_value = 200e6;
        descriptors.push_back(desc);
    }

    return descriptors;
}

std::map<std::string, ParameterValue> AC3RFSinkStage::get_parameters() const {
    return parameters_;
}

bool AC3RFSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    parameters_ = params;
    return true;
}

bool AC3RFSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    IObservationContext& observation_context)
{
    (void)observation_context;
    is_processing_.store(true);
    cancel_requested_.store(false);

    try {
        // Validate input
        if (inputs.empty()) {
            throw std::runtime_error("AC3 RF sink requires one input (VideoFieldRepresentation)");
        }

        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            throw std::runtime_error("Input must be a VideoFieldRepresentation");
        }

        if (!vfr->has_ac3_rf()) {
            throw std::runtime_error(
                "Input VFR does not have AC3 RF data "
                "(no AC3 RF file specified in the source stage?)");
        }

        // Get output path
        auto path_it = parameters.find("output_path");
        if (path_it == parameters.end()) {
            throw std::runtime_error("output_path parameter is required");
        }
        const std::string output_path = std::get<std::string>(path_it->second);

        // Get sample rate (default: 40 MHz)
        double sample_rate = 40e6;
        auto sr_it = parameters.find("input_sample_rate");
        if (sr_it != parameters.end()) {
            sample_rate = std::get<double>(sr_it->second);
        }

        ORC_LOG_INFO("AC3RFSink: Decoding AC3 RF at {:.1f} MHz, writing to {}",
            sample_rate / 1e6, output_path);

        std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("Failed to open output file: " + output_path);
        }

        const auto field_range  = vfr->field_range();
        const FieldID start_field = field_range.start;
        const FieldID end_field   = field_range.end;
        const uint64_t total_fields = end_field.value() - start_field.value();

        // Samples are processed in fixed-size blocks.  kBlockSize is chosen to
        // balance internal filter memory against demodulate() call overhead.
        static constexpr int kBlockSize = 1 << 18; // 262144 samples

        Logger ac3_log(Logger::c_log_warn, std::cerr, false);
        Ac3RfDemodulator demodulator(ac3_log, sample_rate, kBlockSize,
            FirFilterStage::simdSupported());

        // Accumulation buffer: collects samples from successive fields until a
        // full block is available for the demodulator.
        std::vector<float> accum;
        accum.reserve(kBlockSize * 2);

        uint64_t frames_written = 0;

        auto process_block = [&](const float* data) {
            for (const auto& frame : demodulator.demodulate(data)) {
                out.write(reinterpret_cast<const char*>(frame.data()),
                          static_cast<std::streamsize>(frame.size()));
                ++frames_written;
            }
        };

        for (FieldID fid = start_field; fid < end_field; ++fid) {
            if (cancel_requested_.load()) {
                out.close();
                last_status_ = "Cancelled by user";
                ORC_LOG_WARN("AC3RFSink: {}", last_status_);
                is_processing_.store(false);
                return false;
            }

            // Convert int16_t RF samples to float and append to accumulation buffer
            for (auto s : vfr->get_ac3_rf_samples(fid)) {
                accum.push_back(static_cast<float>(s));
            }

            // Process all complete blocks from the accumulation buffer
            size_t consumed = 0;
            while (consumed + kBlockSize <= accum.size()) {
                process_block(accum.data() + consumed);
                consumed += kBlockSize;
            }
            // Remove consumed samples, keeping the partial block at the front
            if (consumed > 0) {
                accum.erase(accum.begin(), accum.begin() + static_cast<ptrdiff_t>(consumed));
            }

            const uint64_t current = fid.value() - start_field.value() + 1;
            if (progress_callback_) {
                progress_callback_(current, total_fields,
                    "Decoding AC3 RF field " + std::to_string(current) +
                        "/" + std::to_string(total_fields));
            }
        }

        // Flush the final partial block, zero-padded to kBlockSize
        if (!accum.empty()) {
            std::vector<float> pad_block(kBlockSize, 0.0f);
            std::copy(accum.begin(), accum.end(), pad_block.begin());
            process_block(pad_block.data());
        }

        out.close();

        ORC_LOG_INFO("AC3RFSink: Wrote {} AC3 frames to {}", frames_written, output_path);
        ORC_LOG_INFO("AC3RFSink: {}", demodulator.reedSolomonStatistics());
        last_status_ = "Success: " + std::to_string(frames_written) + " frames written";
        is_processing_.store(false);
        return true;

    } catch (const std::exception& e) {
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("AC3RFSink: {}", e.what());
        is_processing_.store(false);
        return false;
    }
}

std::string AC3RFSinkStage::get_trigger_status() const {
    return last_status_;
}

} // namespace orc
