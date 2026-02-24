/*
 * File:        efm_decoder_sink_stage.cpp
 * Module:      orc-core
 * Purpose:     EFM Decoder Sink Stage - decodes EFM t-values to audio/data
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "efm_decoder_sink_stage.h"
#include <logging.h>
#include "stage_parameter.h"
#include "buffered_file_io.h"
#include "vendor/unified_decoder.h"
#include <stage_registry.h>
#include <filesystem>
#include <chrono>
#include <sstream>
#include <utility>
#include <stdexcept>

namespace orc {

namespace {

efm_decoder_report::EFMDecoderRunReport build_run_report_from_parameters(
    const efm_decoder_config::ParsedParameters& parsed)
{
    efm_decoder_report::EFMDecoderRunReport report;
    report.status = efm_decoder_report::RunStatus::Failed;
    report.status_message = "Decode did not complete";

    const auto& parameters = parsed.normalized_parameters;
    report.decode_mode = std::get<std::string>(parameters.at("decode_mode"));
    report.output_path = std::get<std::string>(parameters.at("output_path"));
    report.timecode_mode = std::get<std::string>(parameters.at("timecode_mode"));
    report.audio_output_format = std::get<std::string>(parameters.at("audio_output_format"));
    report.write_audacity_labels = std::get<bool>(parameters.at("write_audacity_labels"));
    report.audio_concealment = std::get<bool>(parameters.at("audio_concealment"));
    report.zero_pad_audio = std::get<bool>(parameters.at("zero_pad_audio"));
    report.write_data_metadata = std::get<bool>(parameters.at("write_data_metadata"));
    report.write_report = parsed.write_report;
    report.report_path = parsed.report_path;

    return report;
}

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
    const auto trigger_start = std::chrono::steady_clock::now();
    efm_decoder_report::EFMDecoderRunReport run_report;
    run_report.status = efm_decoder_report::RunStatus::Failed;
    run_report.status_message = "Decode did not complete";
    
    is_processing_.store(true);
    cancel_requested_.store(false);
    
    try {
        efm_decoder_config::ParsedParameters parsed;
        std::string error_message;
        if (!efm_decoder_config::parse_parameters(parameters, parsed, error_message)) {
            throw std::runtime_error(error_message);
        }

        run_report = build_run_report_from_parameters(parsed);
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
        const auto extraction_start = std::chrono::steady_clock::now();

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

        const auto extraction_end = std::chrono::steady_clock::now();
        run_report.extraction_duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(extraction_end - extraction_start).count();
        run_report.extracted_tvalues = written_tvalues;

        DecoderConfig decoder_config = parsed.decoder_config;
        decoder_config.global.inputPath = temp_input_path;

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
        const auto decode_start = std::chrono::steady_clock::now();
        const int decode_exit_code = decoder.run();
        const auto decode_end = std::chrono::steady_clock::now();

        run_report.decode_exit_code = decode_exit_code;
        run_report.decode_duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(decode_end - decode_start).count();

        const UnifiedDecoder::RunStatistics decoder_stats = decoder.getRunStatistics();
        run_report.stats.shared_channel_to_f3_ms = decoder_stats.sharedChannelToF3TimeMs;
        run_report.stats.shared_f3_to_f2_ms = decoder_stats.sharedF3ToF2TimeMs;
        run_report.stats.shared_f2_correction_ms = decoder_stats.sharedF2CorrectionTimeMs;
        run_report.stats.shared_f2_to_f1_ms = decoder_stats.sharedF2ToF1TimeMs;
        run_report.stats.shared_f1_to_data24_ms = decoder_stats.sharedF1ToData24TimeMs;
        run_report.stats.audio_data24_to_audio_ms = decoder_stats.audioData24ToAudioTimeMs;
        run_report.stats.audio_correction_ms = decoder_stats.audioCorrectionTimeMs;
        run_report.stats.data_data24_to_raw_sector_ms = decoder_stats.dataData24ToRawSectorTimeMs;
        run_report.stats.data_raw_sector_to_sector_ms = decoder_stats.dataRawSectorToSectorTimeMs;
        run_report.stats.produced_data24_sections = decoder_stats.data24SectionCount;
        run_report.stats.auto_no_timecodes_enabled = decoder_stats.autoNoTimecodesEnabled;
        run_report.stats.no_timecodes_active = decoder_stats.noTimecodesActive;
        run_report.stats.shared_decode_statistics_text = decoder_stats.sharedDecodeStatisticsText;
        run_report.stats.mode_decode_statistics_text = decoder_stats.modeDecodeStatisticsText;

        if (!temp_input_path.empty()) {
            std::filesystem::remove(temp_input_path);
        }

        const auto trigger_end = std::chrono::steady_clock::now();
        run_report.total_duration_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(trigger_end - trigger_start).count();

        if (cancel_requested_.load()) {
            last_status_ = "Cancelled by user";
            run_report.status = efm_decoder_report::RunStatus::Cancelled;
            run_report.status_message = last_status_;

            std::string report_error;
            if (!efm_decoder_report::write_text_report(run_report, report_error)) {
                last_status_ += std::string(" (report write failed: ") + report_error + ")";
                ORC_LOG_WARN("EFMDecoderSink: {}", last_status_);
            }

            last_run_report_ = run_report;
            ORC_LOG_WARN("EFMDecoderSink: {}", last_status_);
            is_processing_.store(false);
            return false;
        }

        if (decode_exit_code != 0) {
            throw std::runtime_error("Decoder pipeline failed");
        }

        last_status_ = "Success: decode pipeline completed";
        run_report.status = efm_decoder_report::RunStatus::Success;
        run_report.status_message = last_status_;

        std::string report_error;
        if (!efm_decoder_report::write_text_report(run_report, report_error)) {
            throw std::runtime_error("Failed to write decode report: " + report_error);
        }

        if (run_report.write_report) {
            ORC_LOG_INFO("EFMDecoderSink: Decode report written to {}", run_report.report_path);
        }

        last_run_report_ = run_report;
        ORC_LOG_INFO("EFMDecoderSink: {}", last_status_);
        is_processing_.store(false);
        return true;
        
    } catch (const std::exception& e) {
        if (!temp_input_path.empty()) {
            std::filesystem::remove(temp_input_path);
        }
        last_status_ = std::string("Error: ") + e.what();
        run_report.status = cancel_requested_.load()
            ? efm_decoder_report::RunStatus::Cancelled
            : efm_decoder_report::RunStatus::Failed;
        run_report.status_message = last_status_;
        run_report.total_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - trigger_start).count();

        std::string report_error;
        if (!efm_decoder_report::write_text_report(run_report, report_error)) {
            ORC_LOG_WARN("EFMDecoderSink: Failed to write decode report: {}", report_error);
        }

        last_run_report_ = run_report;
        ORC_LOG_ERROR("EFMDecoderSink: {}", last_status_);
        is_processing_.store(false);
        return false;
    }
}

std::string EFMDecoderSinkStage::get_trigger_status() const {
    return last_status_;
}

std::optional<StageReport> EFMDecoderSinkStage::generate_report() const {
    if (last_run_report_.has_value()) {
        return efm_decoder_report::to_stage_report(last_run_report_.value());
    }

    efm_decoder_report::EFMDecoderRunReport empty_report;
    return efm_decoder_report::to_stage_report(empty_report);
}

} // namespace orc
