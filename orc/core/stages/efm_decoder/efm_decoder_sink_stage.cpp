/*
 * File:        efm_decoder_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     EFM Decoder Sink Stage - decodes EFM t-values to audio/data
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "efm_decoder_sink_stage.h"
#include "logging.h"
#include "stage_parameter.h"
#include "buffered_file_io.h"
#include "vendor/unified_decoder.h"
#include <stage_registry.h>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <utility>
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace orc {

namespace {

std::string create_temp_efm_path()
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    std::ostringstream file_name;
    file_name << "orc-efm-decoder-" << now << ".efm";
    return (std::filesystem::temp_directory_path() / file_name.str()).string();
}

bool write_efm_input_file(
    const VideoFieldRepresentation& vfr,
    const std::string& temp_input_path,
    std::atomic<bool>& cancel_requested,
    TriggerProgressCallback progress_callback,
    uint64_t& written_tvalues,
    std::string& error_message
)
{
    auto field_range = vfr.field_range();
    FieldID start_field = field_range.start;
    FieldID end_field = field_range.end;

    const uint64_t total_fields = end_field.value() - start_field.value();
    if (total_fields == 0) {
        error_message = "Input VFR field range is empty";
        return false;
    }

    BufferedFileWriter<uint8_t> writer(4 * 1024 * 1024);
    if (!writer.open(temp_input_path)) {
        error_message = "Failed to open temporary EFM input file: " + temp_input_path;
        return false;
    }

    written_tvalues = 0;

    for (FieldID field_id = start_field; field_id < end_field; ++field_id) {
        if (cancel_requested.load()) {
            writer.close();
            std::filesystem::remove(temp_input_path);
            error_message = "Cancelled by user";
            return false;
        }

        auto tvalues = vfr.get_efm_samples(field_id);
        if (!tvalues.empty()) {
            writer.write(tvalues);
            written_tvalues += tvalues.size();
        }

        const uint64_t current_field = field_id.value() - start_field.value();
        if ((current_field % 10 == 0 || current_field + 1 == total_fields) && progress_callback) {
            progress_callback(
                static_cast<size_t>(current_field + 1),
                static_cast<size_t>(total_fields),
                "Extracting EFM t-values from VFR"
            );
        }
    }

    writer.close();

    if (written_tvalues == 0) {
        std::filesystem::remove(temp_input_path);
        error_message = "No EFM t-values found in field range";
        return false;
    }

    error_message.clear();
    return true;
}

} // namespace

// Register this stage with the registry
ORC_REGISTER_STAGE(EFMDecoderSinkStage)

// Force linker to include this object file
void force_link_EFMDecoderSinkStage() {}

EFMDecoderSinkStage::EFMDecoderSinkStage()
    : parameters_(efm_decoder_config::default_parameters())
{
    efm_decoder_config::ParsedParameters parsed;
    std::string error_message;
    if (efm_decoder_config::parse_parameters(parameters_, parsed, error_message)) {
        parsed_parameters_ = parsed;
    }
}

NodeTypeInfo EFMDecoderSinkStage::get_node_type_info() const {
    return NodeTypeInfo{
        NodeType::SINK,
        "EFMDecoderSink",
        "EFM Decoder Sink",
        "Decodes EFM t-values from VFR to audio/data outputs with configurable decode parameters",
        1, 1,  // One input (VideoFieldRepresentation)
        0, 0,  // No outputs (sink writes to disk)
        VideoFormatCompatibility::ALL
    };
}

std::vector<ParameterDescriptor> EFMDecoderSinkStage::get_parameter_descriptors(
    VideoSystem project_format, 
    SourceType source_type
) const {
    (void)project_format;
    (void)source_type;

    return efm_decoder_config::get_parameter_descriptors();
}

std::map<std::string, ParameterValue> EFMDecoderSinkStage::get_parameters() const {
    return parameters_;
}

bool EFMDecoderSinkStage::set_parameters(const std::map<std::string, ParameterValue>& params) {
    efm_decoder_config::ParsedParameters parsed;
    std::string error_message;
    if (!efm_decoder_config::parse_parameters(params, parsed, error_message)) {
        last_status_ = std::string("Error: ") + error_message;
        ORC_LOG_ERROR("EFMDecoderSink: {}", error_message);
        return false;
    }

    parameters_ = parsed.normalized_parameters;
    parsed_parameters_ = parsed;
    return true;
}

std::vector<ArtifactPtr> EFMDecoderSinkStage::execute(
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

bool EFMDecoderSinkStage::trigger(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters,
    ObservationContext& observation_context
) {
    (void)observation_context;

    std::string temp_input_path;
    
    is_processing_.store(true);
    cancel_requested_.store(false);
    
    try {
        efm_decoder_config::ParsedParameters parsed;
        std::string error_message;
        if (!efm_decoder_config::parse_parameters(parameters, parsed, error_message)) {
            throw std::runtime_error(error_message);
        }

        parsed_parameters_ = parsed;

        if (inputs.empty()) {
            throw std::runtime_error("EFM Decoder sink requires one input (VideoFieldRepresentation)");
        }

        auto vfr = std::dynamic_pointer_cast<VideoFieldRepresentation>(inputs[0]);
        if (!vfr) {
            throw std::runtime_error("Input must be a VideoFieldRepresentation");
        }

        if (!vfr->has_efm()) {
            throw std::runtime_error("Input VFR does not have EFM data (no EFM file specified in source?)");
        }

        if (progress_callback_) {
            progress_callback_(0, 100, "Preparing EFM decoder input");
        }

        temp_input_path = create_temp_efm_path();
        uint64_t written_tvalues = 0;

        if (!write_efm_input_file(
            *vfr,
            temp_input_path,
            cancel_requested_,
            progress_callback_,
            written_tvalues,
            error_message
        )) {
            throw std::runtime_error(error_message);
        }

        DecoderConfig decoder_config = parsed.decoder_config;
        decoder_config.global.inputPath = temp_input_path;

        const auto previous_logger = spdlog::default_logger();
        struct LoggerGuard {
            explicit LoggerGuard(std::shared_ptr<spdlog::logger> logger)
                : previous_logger(std::move(logger)) {}
            ~LoggerGuard() {
                if (previous_logger) {
                    spdlog::set_default_logger(previous_logger);
                }
            }
            std::shared_ptr<spdlog::logger> previous_logger;
        } logger_guard(previous_logger);

        if (!configureLogging(
            decoder_config.global.logLevel,
            false,
            decoder_config.global.logFile
        )) {
            ORC_LOG_WARN(
                "EFMDecoderSink: Failed to configure decoder logging for level '{}' (continuing with current logger)",
                decoder_config.global.logLevel
            );
        }

        UnifiedDecoder decoder(decoder_config);
        decoder.setCancellationCallback([this]() {
            return cancel_requested_.load();
        });
        decoder.setProgressCallback([this](size_t current, size_t total, const std::string& message) {
            if (progress_callback_) {
                progress_callback_(current, total, message);
            }
        });

        ORC_LOG_INFO("EFMDecoderSink: Starting decode pipeline using {} extracted t-values", written_tvalues);
        const int decode_exit_code = decoder.run();

        if (!temp_input_path.empty()) {
            std::filesystem::remove(temp_input_path);
        }

        if (cancel_requested_.load()) {
            last_status_ = "Cancelled by user";
            ORC_LOG_WARN("EFMDecoderSink: {}", last_status_);
            is_processing_.store(false);
            return false;
        }

        if (decode_exit_code != 0) {
            throw std::runtime_error("Decoder pipeline failed");
        }

        last_status_ = "Success: decode pipeline completed";
        ORC_LOG_INFO("EFMDecoderSink: {}", last_status_);
        is_processing_.store(false);
        return true;
        
    } catch (const std::exception& e) {
        if (!temp_input_path.empty()) {
            std::filesystem::remove(temp_input_path);
        }
        last_status_ = std::string("Error: ") + e.what();
        ORC_LOG_ERROR("EFMDecoderSink: {}", last_status_);
        is_processing_.store(false);
        return false;
    }
}

std::string EFMDecoderSinkStage::get_trigger_status() const {
    return last_status_;
}

} // namespace orc
