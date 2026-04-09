/*
 * File:        nn_output_backend.cpp
 * Module:      orc-core
 * Purpose:     NN NTSC output backend implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "nn_output_backend.h"
#include "logging.h"

namespace orc {

// ---------------------------------------------------------------------------
// NnRawOutputBackend
// ---------------------------------------------------------------------------

NnRawOutputBackend::~NnRawOutputBackend()
{
    if (out_file_.is_open()) {
        out_file_.close();
    }
}

bool NnRawOutputBackend::initialize(Configuration& config)
{
    nn_ntsc::NnOutputWriter::Configuration wcfg;
    wcfg.paddingAmount = config.padding_amount;

    if (config.format == "rgb") {
        wcfg.pixelFormat = nn_ntsc::NnOutputWriter::RGB48;
        wcfg.outputY4m   = false;
        format_info_     = "RGB48";
    } else if (config.format == "yuv") {
        wcfg.pixelFormat = nn_ntsc::NnOutputWriter::YUV444P16;
        wcfg.outputY4m   = false;
        format_info_     = "YUV444P16";
    } else {  // "y4m"
        wcfg.pixelFormat = nn_ntsc::NnOutputWriter::YUV444P16;
        wcfg.outputY4m   = true;
        format_info_     = "Y4M (YUV444P16)";
    }

    // updateConfiguration() pads video_params in place — side-effect visible to caller
    writer_.updateConfiguration(*config.video_params, wcfg);
    writer_.printOutputInfo();

    // Cache the per-frame header (constant for the session; non-empty only for Y4M)
    frame_header_ = writer_.getFrameHeader();

    // Open output file
    out_file_.open(config.output_path, std::ios::binary | std::ios::trunc);
    if (!out_file_) {
        ORC_LOG_ERROR("NnRawOutputBackend: Cannot open output file '{}'", config.output_path);
        return false;
    }

    // Write stream header (non-empty only for Y4M)
    auto stream_hdr = writer_.getStreamHeader();
    if (!stream_hdr.empty()) {
        out_file_.write(stream_hdr.data(), static_cast<std::streamsize>(stream_hdr.size()));
        if (!out_file_.good()) {
            ORC_LOG_ERROR("NnRawOutputBackend: Failed to write stream header");
            return false;
        }
    }

    return true;
}

bool NnRawOutputBackend::writeFrame(const nn_ntsc::ComponentFrame& frame)
{
    if (!out_file_.is_open()) {
        ORC_LOG_ERROR("NnRawOutputBackend: Not initialized");
        return false;
    }

    if (!frame_header_.empty()) {
        out_file_.write(frame_header_.data(),
                       static_cast<std::streamsize>(frame_header_.size()));
        if (!out_file_.good()) {
            ORC_LOG_ERROR("NnRawOutputBackend: Failed to write frame header");
            return false;
        }
    }

    nn_ntsc::NnOutputFrame out_frame;
    writer_.convert(frame, out_frame);

    out_file_.write(reinterpret_cast<const char*>(out_frame.data()),
                   static_cast<std::streamsize>(out_frame.size() * sizeof(uint16_t)));

    if (!out_file_.good()) {
        ORC_LOG_ERROR("NnRawOutputBackend: Failed to write frame data");
        return false;
    }

    return true;
}

bool NnRawOutputBackend::finalize()
{
    if (out_file_.is_open()) {
        out_file_.flush();
        out_file_.close();
    }
    return true;
}

std::string NnRawOutputBackend::getFormatInfo() const
{
    return format_info_;
}

} // namespace orc
