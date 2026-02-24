/*
 * File:        efm_decoder_report.h
 * Module:      orc-core
 * Purpose:     Structured report model and rendering for EFM Decoder Sink
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_CORE_EFM_DECODER_REPORT_H
#define ORC_CORE_EFM_DECODER_REPORT_H

#include "../../stage.h"
#include <cstdint>
#include <string>

namespace orc::efm_decoder_report {

enum class RunStatus {
    NotRun,
    Success,
    Failed,
    Cancelled
};

struct DecodeStatistics {
    int64_t shared_channel_to_f3_ms{0};
    int64_t shared_f3_to_f2_ms{0};
    int64_t shared_f2_correction_ms{0};
    int64_t shared_f2_to_f1_ms{0};
    int64_t shared_f1_to_data24_ms{0};
    int64_t audio_data24_to_audio_ms{0};
    int64_t audio_correction_ms{0};
    int64_t data_data24_to_raw_sector_ms{0};
    int64_t data_raw_sector_to_sector_ms{0};
    int64_t produced_data24_sections{0};
    bool auto_no_timecodes_enabled{false};
    bool no_timecodes_active{false};
    std::string shared_decode_statistics_text;
    std::string mode_decode_statistics_text;
};

struct EFMDecoderRunReport {
    RunStatus status{RunStatus::NotRun};
    std::string status_message{"Not yet executed"};

    std::string decode_mode{"audio"};
    std::string output_path;
    std::string timecode_mode{"auto"};
    std::string audio_output_format{"wav"};
    bool write_audacity_labels{false};
    bool audio_concealment{true};
    bool zero_pad_audio{false};
    bool write_data_metadata{false};

    bool write_report{false};
    std::string report_path;

    uint64_t extracted_tvalues{0};
    int decode_exit_code{-1};
    int64_t extraction_duration_ms{0};
    int64_t decode_duration_ms{0};
    int64_t total_duration_ms{0};

    DecodeStatistics stats;
};

StageReport to_stage_report(const EFMDecoderRunReport& report);
std::string render_text_report(const EFMDecoderRunReport& report);
bool write_text_report(const EFMDecoderRunReport& report, std::string& error_message);

} // namespace orc::efm_decoder_report

#endif // ORC_CORE_EFM_DECODER_REPORT_H
