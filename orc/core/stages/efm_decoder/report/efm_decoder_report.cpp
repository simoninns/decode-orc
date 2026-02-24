/*
 * File:        efm_decoder_report.cpp
 * Module:      orc-core
 * Purpose:     Structured report model and rendering for EFM Decoder Sink
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "efm_decoder_report.h"
#include <filesystem>
#include <fstream>
#include <sstream>

namespace orc::efm_decoder_report {

namespace {

std::string bool_text(bool value)
{
    return value ? "yes" : "no";
}

std::string status_text(RunStatus status)
{
    switch (status) {
        case RunStatus::NotRun:
            return "NotRun";
        case RunStatus::Success:
            return "Success";
        case RunStatus::Failed:
            return "Failed";
        case RunStatus::Cancelled:
            return "Cancelled";
    }

    return "Unknown";
}

} // namespace

StageReport to_stage_report(const EFMDecoderRunReport& report)
{
    StageReport stage_report;
    stage_report.summary = "EFM Decoder Sink Report";

    stage_report.items.push_back({"Status", status_text(report.status)});
    stage_report.items.push_back({"Message", report.status_message});

    if (report.status == RunStatus::NotRun) {
        stage_report.items.push_back({"Info", "Trigger the stage to generate decode diagnostics"});
        return stage_report;
    }

    stage_report.items.push_back({"Decode Mode", report.decode_mode});
    stage_report.items.push_back({"Output Path", report.output_path});
    stage_report.items.push_back({"Timecode Mode", report.timecode_mode});
    stage_report.items.push_back({"No-Timecodes Active", bool_text(report.stats.no_timecodes_active)});
    stage_report.items.push_back({"Auto No-Timecodes", bool_text(report.stats.auto_no_timecodes_enabled)});
    stage_report.items.push_back({"Extracted EFM T-Values", std::to_string(report.extracted_tvalues)});
    stage_report.items.push_back({"Data24 Sections", std::to_string(report.stats.produced_data24_sections)});
    stage_report.items.push_back({"Extraction Time", std::to_string(report.extraction_duration_ms) + " ms"});
    stage_report.items.push_back({"Decode Time", std::to_string(report.decode_duration_ms) + " ms"});
    stage_report.items.push_back({"Total Time", std::to_string(report.total_duration_ms) + " ms"});

    stage_report.items.push_back({"Write Report", bool_text(report.write_report)});
    if (report.write_report) {
        stage_report.items.push_back({"Report Path", report.report_path});
    }

    stage_report.metrics["extracted_tvalues"] = static_cast<int64_t>(report.extracted_tvalues);
    stage_report.metrics["data24_sections"] = report.stats.produced_data24_sections;
    stage_report.metrics["extraction_duration_ms"] = report.extraction_duration_ms;
    stage_report.metrics["decode_duration_ms"] = report.decode_duration_ms;
    stage_report.metrics["total_duration_ms"] = report.total_duration_ms;
    stage_report.metrics["decode_exit_code"] = static_cast<int64_t>(report.decode_exit_code);
    stage_report.metrics["shared_channel_to_f3_ms"] = report.stats.shared_channel_to_f3_ms;
    stage_report.metrics["shared_f3_to_f2_ms"] = report.stats.shared_f3_to_f2_ms;
    stage_report.metrics["shared_f2_correction_ms"] = report.stats.shared_f2_correction_ms;
    stage_report.metrics["shared_f2_to_f1_ms"] = report.stats.shared_f2_to_f1_ms;
    stage_report.metrics["shared_f1_to_data24_ms"] = report.stats.shared_f1_to_data24_ms;
    stage_report.metrics["audio_data24_to_audio_ms"] = report.stats.audio_data24_to_audio_ms;
    stage_report.metrics["audio_correction_ms"] = report.stats.audio_correction_ms;
    stage_report.metrics["data_data24_to_raw_sector_ms"] = report.stats.data_data24_to_raw_sector_ms;
    stage_report.metrics["data_raw_sector_to_sector_ms"] = report.stats.data_raw_sector_to_sector_ms;

    return stage_report;
}

std::string render_text_report(const EFMDecoderRunReport& report)
{
    std::ostringstream output;

    output << "EFM Decoder Sink Report\n";
    output << "=======================\n\n";

    output << "Status\n";
    output << "------\n";
    output << "State: " << status_text(report.status) << "\n";
    output << "Message: " << report.status_message << "\n";
    output << "Decoder Exit Code: " << report.decode_exit_code << "\n\n";

    output << "Configuration\n";
    output << "-------------\n";
    output << "Decode Mode: " << report.decode_mode << "\n";
    output << "Output Path: " << report.output_path << "\n";
    output << "Timecode Mode: " << report.timecode_mode << "\n";
    output << "No-Timecodes Active: " << bool_text(report.stats.no_timecodes_active) << "\n";
    output << "Auto No-Timecodes Enabled: " << bool_text(report.stats.auto_no_timecodes_enabled) << "\n";
    output << "Audio Output Format: " << report.audio_output_format << "\n";
    output << "Write Audacity Labels: " << bool_text(report.write_audacity_labels) << "\n";
    output << "Audio Concealment: " << bool_text(report.audio_concealment) << "\n";
    output << "Zero Pad Audio: " << bool_text(report.zero_pad_audio) << "\n";
    output << "Write Data Metadata: " << bool_text(report.write_data_metadata) << "\n";
    output << "Write Report: " << bool_text(report.write_report) << "\n";
    if (report.write_report) {
        output << "Report Path: " << report.report_path << "\n";
    }
    output << "\n";

    output << "Run Timing\n";
    output << "----------\n";
    output << "Extracted EFM T-Values: " << report.extracted_tvalues << "\n";
    output << "Produced Data24 Sections: " << report.stats.produced_data24_sections << "\n";
    output << "Extraction Duration: " << report.extraction_duration_ms << " ms\n";
    output << "Decode Duration: " << report.decode_duration_ms << " ms\n";
    output << "Total Duration: " << report.total_duration_ms << " ms\n\n";

    output << "Pipeline Statistics\n";
    output << "-------------------\n";
    output << "Shared - Channel to F3: " << report.stats.shared_channel_to_f3_ms << " ms\n";
    output << "Shared - F3 to F2: " << report.stats.shared_f3_to_f2_ms << " ms\n";
    output << "Shared - F2 Correction: " << report.stats.shared_f2_correction_ms << " ms\n";
    output << "Shared - F2 to F1: " << report.stats.shared_f2_to_f1_ms << " ms\n";
    output << "Shared - F1 to Data24: " << report.stats.shared_f1_to_data24_ms << " ms\n";
    output << "Audio - Data24 to Audio: " << report.stats.audio_data24_to_audio_ms << " ms\n";
    output << "Audio - Audio Correction: " << report.stats.audio_correction_ms << " ms\n";
    output << "Data - Data24 to Raw Sector: " << report.stats.data_data24_to_raw_sector_ms << " ms\n";
    output << "Data - Raw Sector to Sector: " << report.stats.data_raw_sector_to_sector_ms << " ms\n";

    output << "\nDetailed Shared Decode Statistics\n";
    output << "---------------------------------\n";
    if (!report.stats.shared_decode_statistics_text.empty()) {
        output << report.stats.shared_decode_statistics_text << "\n";
    } else {
        output << "No detailed shared decode statistics captured.\n";
    }

    output << "\nDetailed Mode Decode Statistics\n";
    output << "-------------------------------\n";
    if (!report.stats.mode_decode_statistics_text.empty()) {
        output << report.stats.mode_decode_statistics_text << "\n";
    } else {
        output << "No detailed mode decode statistics captured.\n";
    }

    return output.str();
}

bool write_text_report(const EFMDecoderRunReport& report, std::string& error_message)
{
    if (!report.write_report) {
        error_message.clear();
        return true;
    }

    if (report.report_path.empty()) {
        error_message = "Report path is empty";
        return false;
    }

    try {
        const std::filesystem::path output_path(report.report_path);
        if (output_path.has_parent_path()) {
            std::filesystem::create_directories(output_path.parent_path());
        }

        std::ofstream file(report.report_path, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            error_message = "Failed to open report path for writing: " + report.report_path;
            return false;
        }

        file << render_text_report(report);
        if (!file.good()) {
            error_message = "Failed while writing report file: " + report.report_path;
            return false;
        }

        file.close();
    } catch (const std::exception& e) {
        error_message = e.what();
        return false;
    }

    error_message.clear();
    return true;
}

} // namespace orc::efm_decoder_report
