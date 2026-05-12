/*
 * File:        audio_sink_stage_deps.cpp
 * Module:      orc-core
 * Purpose:     AudioSinkStage dependency implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "audio_sink_stage_deps.h"

#include "buffered_file_io.h"
#include "logging.h"

#include <ios>
#include <limits>
#include <utility>

namespace orc
{
    void AudioSinkStageDeps::init(TriggerProgressCallback progress_callback,
                                  std::atomic<bool>* is_processing,
                                  std::atomic<bool>* cancel_requested)
    {
        progress_callback_ = std::move(progress_callback);
        is_processing_ = is_processing;
        cancel_requested_ = cancel_requested;
    }

    bool AudioSinkStageDeps::write_wav_header(std::ofstream& out,
                                              uint32_t num_samples,
                                              uint32_t sample_rate,
                                              uint16_t num_channels,
                                              uint16_t bits_per_sample) const
    {
        uint32_t byte_rate = sample_rate * num_channels * (bits_per_sample / 8);
        uint16_t block_align = num_channels * (bits_per_sample / 8);
        uint32_t data_size = num_samples * num_channels * (bits_per_sample / 8);
        uint32_t file_size = 36 + data_size;

        out.write("RIFF", 4);
        out.write(reinterpret_cast<const char*>(&file_size), 4);
        out.write("WAVE", 4);

        out.write("fmt ", 4);
        uint32_t fmt_size = 16;
        out.write(reinterpret_cast<const char*>(&fmt_size), 4);

        uint16_t audio_format = 1;
        out.write(reinterpret_cast<const char*>(&audio_format), 2);
        out.write(reinterpret_cast<const char*>(&num_channels), 2);
        out.write(reinterpret_cast<const char*>(&sample_rate), 4);
        out.write(reinterpret_cast<const char*>(&byte_rate), 4);
        out.write(reinterpret_cast<const char*>(&block_align), 2);
        out.write(reinterpret_cast<const char*>(&bits_per_sample), 2);

        out.write("data", 4);
        out.write(reinterpret_cast<const char*>(&data_size), 4);

        return out.good();
    }

    AudioSinkWriteResult AudioSinkStageDeps::write_audio_wav(
        const VideoFieldRepresentation* representation,
        const std::string& output_path)
    {
        auto field_range = representation->field_range();
        FieldID start_field = field_range.start;
        FieldID end_field = field_range.end;

        uint64_t total_fields = end_field.value() - start_field.value();

        uint64_t total_samples = 0;
        for (FieldID fid = start_field; fid < end_field; ++fid) {
            total_samples += representation->get_audio_sample_count(fid);
        }

        if (total_samples == 0) {
            return {false, 0, "No audio samples found in field range"};
        }

        BufferedFileWriter<int16_t> writer(4 * 1024 * 1024);
        if (!writer.open(output_path)) {
            return {false, 0, "Failed to open output file: " + output_path};
        }

        {
            std::ofstream header_out(output_path, std::ios::binary);
            const uint32_t sample_rate = 44100;
            const uint16_t num_channels = 2;
            const uint16_t bits_per_sample = 16;
            const uint32_t wav_samples = total_samples > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
                ? std::numeric_limits<uint32_t>::max()
                : static_cast<uint32_t>(total_samples);

            if (!write_wav_header(header_out, wav_samples, sample_rate, num_channels, bits_per_sample)) {
                return {false, 0, "Failed to write WAV header"};
            }
            header_out.close();
        }

        if (!writer.open(output_path, std::ios::binary | std::ios::app)) {
            return {false, 0, "Failed to reopen output file for audio data: " + output_path};
        }

        uint64_t frames_written = 0;
        for (FieldID fid = start_field; fid < end_field; ++fid) {
            if (cancel_requested_ && cancel_requested_->load()) {
                writer.close();
                if (is_processing_) {
                    is_processing_->store(false);
                }
                return {false, 0, "Cancelled by user"};
            }

            auto samples = representation->get_audio_samples(fid);
            if (!samples.empty()) {
                writer.write(samples);
                frames_written += samples.size() / 2;
            }

            uint64_t current_field = fid.value() - start_field.value();
            if (progress_callback_ && current_field % 10 == 0) {
                progress_callback_(
                    current_field,
                    total_fields,
                    "Writing audio field " + std::to_string(current_field) + "/" + std::to_string(total_fields));
            }
        }

        writer.close();
        ORC_LOG_DEBUG("AudioSinkDeps: Wrote {} audio frames", frames_written);
        return {true, frames_written, ""};
    }
} // namespace orc
