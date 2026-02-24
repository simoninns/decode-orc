/*
 * File:        efm_decoder_parameter_contract.cpp
 * Module:      orc-core
 * Purpose:     Phase 1 contract for EFM Decoder Sink parameters and validation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "efm_decoder_parameter_contract.h"
#include <unordered_set>
#include <variant>
#include <utility>

namespace orc::efm_decoder_config {

namespace {

bool is_string_in_set(const std::string& value, const std::unordered_set<std::string>& allowed)
{
    return allowed.find(value) != allowed.end();
}

bool get_bool_param(const std::map<std::string, ParameterValue>& params, const std::string& name)
{
    return std::get<bool>(params.at(name));
}

std::string get_string_param(const std::map<std::string, ParameterValue>& params, const std::string& name)
{
    return std::get<std::string>(params.at(name));
}

} // namespace

std::vector<ParameterDescriptor> get_parameter_descriptors()
{
    std::vector<ParameterDescriptor> descriptors;

    {
        ParameterDescriptor desc;
        desc.name = "decode_mode";
        desc.display_name = "Decode Mode";
        desc.description = "Decoder operating mode";
        desc.type = ParameterType::STRING;
        desc.constraints.default_value = std::string("audio");
        desc.constraints.allowed_strings = {"audio", "data"};
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "output_path";
        desc.display_name = "Output File";
        desc.description = "Destination file for decoded output (audio or data)";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.required = true;
        desc.constraints.default_value = std::string("");
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "decoder_log_level";
        desc.display_name = "Decoder Log Level";
        desc.description = "Verbosity for decoder-internal logging";
        desc.type = ParameterType::STRING;
        desc.constraints.default_value = std::string("info");
        desc.constraints.allowed_strings = {
            "trace", "debug", "info", "warn", "error", "critical", "off"
        };
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "decoder_log_file";
        desc.display_name = "Decoder Log File";
        desc.description = "Optional file path for detailed decoder logs";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.default_value = std::string("");
        desc.file_extension_hint = ".log";
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "timecode_mode";
        desc.display_name = "Timecode Mode";
        desc.description = "Timecode handling strategy";
        desc.type = ParameterType::STRING;
        desc.constraints.default_value = std::string("auto");
        desc.constraints.allowed_strings = {"auto", "force_no_timecodes", "force_timecodes"};
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "audio_output_format";
        desc.display_name = "Audio Output Format";
        desc.description = "Audio file format when decode mode is audio";
        desc.type = ParameterType::STRING;
        desc.constraints.default_value = std::string("wav");
        desc.constraints.allowed_strings = {"wav", "raw_pcm"};
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"audio"}};
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "write_audacity_labels";
        desc.display_name = "Write Audacity Labels";
        desc.description = "Write Audacity label metadata for audio decode output";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"audio"}};
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "audio_concealment";
        desc.display_name = "Audio Concealment";
        desc.description = "Enable audio concealment for corrected output";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = true;
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"audio"}};
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "zero_pad_audio";
        desc.display_name = "Zero Pad Audio";
        desc.description = "Pad decoded audio to start from 00:00:00";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"audio"}};
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "write_data_metadata";
        desc.display_name = "Write Data Metadata";
        desc.description = "Write bad sector metadata alongside decoded data output";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        desc.constraints.depends_on = ParameterDependency{"decode_mode", {"data"}};
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "write_report";
        desc.display_name = "Write Decode Report";
        desc.description = "Write textual decoder report to disk";
        desc.type = ParameterType::BOOL;
        desc.constraints.default_value = false;
        descriptors.push_back(desc);
    }

    {
        ParameterDescriptor desc;
        desc.name = "report_path";
        desc.display_name = "Report File";
        desc.description = "Text report destination when report writing is enabled";
        desc.type = ParameterType::FILE_PATH;
        desc.constraints.default_value = std::string("");
        desc.constraints.depends_on = ParameterDependency{"write_report", {"true"}};
        desc.file_extension_hint = ".txt";
        descriptors.push_back(desc);
    }

    return descriptors;
}

std::map<std::string, ParameterValue> default_parameters()
{
    std::map<std::string, ParameterValue> defaults;
    for (const auto& desc : get_parameter_descriptors()) {
        if (desc.constraints.default_value.has_value()) {
            defaults[desc.name] = desc.constraints.default_value.value();
        }
    }
    return defaults;
}

bool validate_and_normalize(
    const std::map<std::string, ParameterValue>& params,
    std::map<std::string, ParameterValue>& normalized,
    std::string& error_message
)
{
    static const std::unordered_set<std::string> known_parameters = {
        "decode_mode",
        "output_path",
        "decoder_log_level",
        "decoder_log_file",
        "timecode_mode",
        "audio_output_format",
        "write_audacity_labels",
        "audio_concealment",
        "zero_pad_audio",
        "write_data_metadata",
        "write_report",
        "report_path"
    };

    static const std::unordered_set<std::string> valid_decode_modes = {"audio", "data"};
    static const std::unordered_set<std::string> valid_timecode_modes = {
        "auto", "force_no_timecodes", "force_timecodes"
    };
    static const std::unordered_set<std::string> valid_audio_output_formats = {"wav", "raw_pcm"};
    static const std::unordered_set<std::string> valid_log_levels = {
        "trace", "debug", "info", "warn", "error", "critical", "off"
    };

    normalized = default_parameters();

    for (const auto& [name, value] : params) {
        if (known_parameters.find(name) == known_parameters.end()) {
            error_message = "Unknown parameter: " + name;
            return false;
        }
        normalized[name] = value;
    }

    if (!std::holds_alternative<std::string>(normalized["decode_mode"])) {
        error_message = "Parameter decode_mode must be a string";
        return false;
    }
    if (!std::holds_alternative<std::string>(normalized["output_path"])) {
        error_message = "Parameter output_path must be a file path string";
        return false;
    }
    if (!std::holds_alternative<std::string>(normalized["decoder_log_level"])) {
        error_message = "Parameter decoder_log_level must be a string";
        return false;
    }
    if (!std::holds_alternative<std::string>(normalized["decoder_log_file"])) {
        error_message = "Parameter decoder_log_file must be a file path string";
        return false;
    }
    if (!std::holds_alternative<std::string>(normalized["timecode_mode"])) {
        error_message = "Parameter timecode_mode must be a string";
        return false;
    }
    if (!std::holds_alternative<std::string>(normalized["audio_output_format"])) {
        error_message = "Parameter audio_output_format must be a string";
        return false;
    }
    if (!std::holds_alternative<bool>(normalized["write_audacity_labels"])) {
        error_message = "Parameter write_audacity_labels must be a boolean";
        return false;
    }
    if (!std::holds_alternative<bool>(normalized["audio_concealment"])) {
        error_message = "Parameter audio_concealment must be a boolean";
        return false;
    }
    if (!std::holds_alternative<bool>(normalized["zero_pad_audio"])) {
        error_message = "Parameter zero_pad_audio must be a boolean";
        return false;
    }
    if (!std::holds_alternative<bool>(normalized["write_data_metadata"])) {
        error_message = "Parameter write_data_metadata must be a boolean";
        return false;
    }
    if (!std::holds_alternative<bool>(normalized["write_report"])) {
        error_message = "Parameter write_report must be a boolean";
        return false;
    }
    if (!std::holds_alternative<std::string>(normalized["report_path"])) {
        error_message = "Parameter report_path must be a file path string";
        return false;
    }

    const std::string decode_mode = get_string_param(normalized, "decode_mode");
    const std::string output_path = get_string_param(normalized, "output_path");
    const std::string log_level = get_string_param(normalized, "decoder_log_level");
    const std::string timecode_mode = get_string_param(normalized, "timecode_mode");
    const std::string audio_output_format = get_string_param(normalized, "audio_output_format");
    const bool write_audacity_labels = get_bool_param(normalized, "write_audacity_labels");
    const bool audio_concealment = get_bool_param(normalized, "audio_concealment");
    const bool zero_pad_audio = get_bool_param(normalized, "zero_pad_audio");
    const bool write_data_metadata = get_bool_param(normalized, "write_data_metadata");
    const bool write_report = get_bool_param(normalized, "write_report");
    const std::string report_path = get_string_param(normalized, "report_path");

    if (!is_string_in_set(decode_mode, valid_decode_modes)) {
        error_message = "Invalid decode_mode: " + decode_mode + " (expected audio or data)";
        return false;
    }
    if (output_path.empty()) {
        error_message = "output_path parameter is required";
        return false;
    }
    if (!is_string_in_set(log_level, valid_log_levels)) {
        error_message = "Invalid decoder_log_level: " + log_level;
        return false;
    }
    if (!is_string_in_set(timecode_mode, valid_timecode_modes)) {
        error_message = "Invalid timecode_mode: " + timecode_mode;
        return false;
    }
    if (!is_string_in_set(audio_output_format, valid_audio_output_formats)) {
        error_message = "Invalid audio_output_format: " + audio_output_format;
        return false;
    }

    if (decode_mode == "audio") {
        if (write_data_metadata) {
            error_message = "write_data_metadata is only valid when decode_mode=data";
            return false;
        }
    } else {
        if (write_audacity_labels) {
            error_message = "write_audacity_labels is only valid when decode_mode=audio";
            return false;
        }
        if (!audio_concealment) {
            error_message = "audio_concealment is only valid when decode_mode=audio";
            return false;
        }
        if (zero_pad_audio) {
            error_message = "zero_pad_audio is only valid when decode_mode=audio";
            return false;
        }
        if (audio_output_format != "wav") {
            error_message = "audio_output_format is only valid when decode_mode=audio";
            return false;
        }
    }

    if (write_report && report_path.empty()) {
        error_message = "report_path is required when write_report=true";
        return false;
    }

    error_message.clear();
    return true;
}

bool parse_parameters(
    const std::map<std::string, ParameterValue>& params,
    ParsedParameters& parsed,
    std::string& error_message
)
{
    std::map<std::string, ParameterValue> normalized;
    if (!validate_and_normalize(params, normalized, error_message)) {
        return false;
    }

    DecoderConfig decoder_config;

    const std::string decode_mode = get_string_param(normalized, "decode_mode");
    const std::string output_path = get_string_param(normalized, "output_path");
    const std::string log_level = get_string_param(normalized, "decoder_log_level");
    const std::string log_file = get_string_param(normalized, "decoder_log_file");
    const std::string timecode_mode = get_string_param(normalized, "timecode_mode");
    const std::string audio_output_format = get_string_param(normalized, "audio_output_format");
    const bool write_audacity_labels = get_bool_param(normalized, "write_audacity_labels");
    const bool audio_concealment = get_bool_param(normalized, "audio_concealment");
    const bool zero_pad_audio = get_bool_param(normalized, "zero_pad_audio");
    const bool write_data_metadata = get_bool_param(normalized, "write_data_metadata");

    decoder_config.global.outputPath = output_path;
    decoder_config.global.logLevel = log_level;
    decoder_config.global.logFile = log_file;

    if (decode_mode == "audio") {
        decoder_config.global.mode = DecoderMode::Audio;
    } else {
        decoder_config.global.mode = DecoderMode::Data;
    }

    if (timecode_mode == "auto") {
        decoder_config.global.noTimecodes = false;
        decoder_config.global.forceTimecodes = false;
    } else if (timecode_mode == "force_no_timecodes") {
        decoder_config.global.noTimecodes = true;
        decoder_config.global.forceTimecodes = false;
    } else {
        decoder_config.global.noTimecodes = false;
        decoder_config.global.forceTimecodes = true;
    }

    decoder_config.audio.audacityLabels = write_audacity_labels;
    decoder_config.audio.noAudioConcealment = !audio_concealment;
    decoder_config.audio.zeroPad = zero_pad_audio;
    decoder_config.audio.noWavHeader = (audio_output_format == "raw_pcm");

    decoder_config.data.outputMetadata = write_data_metadata;

    parsed.normalized_parameters = std::move(normalized);
    parsed.decoder_config = std::move(decoder_config);
    parsed.write_report = get_bool_param(parsed.normalized_parameters, "write_report");
    parsed.report_path = get_string_param(parsed.normalized_parameters, "report_path");

    error_message.clear();
    return true;
}

} // namespace orc::efm_decoder_config
