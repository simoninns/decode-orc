/*
 * File:        output_backend.cpp
 * Module:      orc-core
 * Purpose:     Output backend factory implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "output_backend.h"
#include "raw_output_backend.h"

#ifdef HAVE_FFMPEG
#include "ffmpeg_output_backend.h"
#endif

namespace orc {

std::unique_ptr<OutputBackend> OutputBackendFactory::create(const std::string& format)
{
    // Raw formats
    if (format == "rgb" || format == "yuv" || format == "y4m") {
        return std::make_unique<RawOutputBackend>();
    }
    
#ifdef HAVE_FFMPEG
    // Encoded formats (require FFmpeg)
    if (format.find("mp4-") == 0 || 
        format.find("mkv-") == 0 || 
        format.find("mov-") == 0) {
        return std::make_unique<FFmpegOutputBackend>();
    }
#endif
    
    // Unknown format
    return nullptr;
}

std::vector<std::string> OutputBackendFactory::getSupportedFormats()
{
    std::vector<std::string> formats = {"rgb", "yuv", "y4m"};
    
#ifdef HAVE_FFMPEG
    formats.push_back("mp4-h264");
    formats.push_back("mp4-h265");
    formats.push_back("mkv-ffv1");
#endif
    
    return formats;
}

} // namespace orc
