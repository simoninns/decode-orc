/*
 * File:        video_parameter_safety.h
 * Module:      orc-core
 * Purpose:     Shared validation and sanitisation for chroma sink video parameters
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#ifndef ORC_CORE_CHROMA_SINK_VIDEO_PARAMETER_SAFETY_H
#define ORC_CORE_CHROMA_SINK_VIDEO_PARAMETER_SAFETY_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

#include <orc_source_parameters.h>

namespace orc::chroma_sink {

enum class DecoderVideoProfile {
    Mono,
    NtscColour,
    PalColour
};

struct VideoParameterSafetyResult {
    bool ok = true;
    SourceParameters params;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

inline int32_t get_frame_height(const SourceParameters& params)
{
    if (params.field_height <= 0) {
        return -1;
    }

    return (params.field_height * 2) - 1;
}

inline std::string join_issues(const std::vector<std::string>& issues)
{
    std::ostringstream stream;

    for (size_t index = 0; index < issues.size(); ++index) {
        if (index > 0) {
            stream << "; ";
        }
        stream << issues[index];
    }

    return stream.str();
}

inline VideoParameterSafetyResult sanitize_video_parameters(
    const SourceParameters& input,
    DecoderVideoProfile profile)
{
    VideoParameterSafetyResult result;
    result.params = input;

    auto add_warning = [&result](const std::string& message) {
        result.warnings.push_back(message);
    };
    auto add_error = [&result](const std::string& message) {
        result.ok = false;
        result.errors.push_back(message);
    };

    auto& params = result.params;

    if (params.field_width <= 0) {
        add_error("field_width must be greater than 0");
        return result;
    }

    if (params.field_height <= 0) {
        add_error("field_height must be greater than 0");
        return result;
    }

    constexpr int32_t kGenericMaxWidth = 1135;
    constexpr int32_t kGenericMaxFrameHeight = 625;
    constexpr int32_t kNtscDecoderMinStart = 16;
    constexpr int32_t kNtscDecoderRightMargin = 2;
    constexpr int32_t kPalFilterMargin = 7;
    constexpr int32_t kDefaultBurstLength = 4;

    int32_t max_width = kGenericMaxWidth;
    int32_t max_frame_height = kGenericMaxFrameHeight;
    int32_t min_active_start = 0;
    int32_t right_margin = 0;
    const bool requires_colour_reference = (profile != DecoderVideoProfile::Mono);

    if (profile == DecoderVideoProfile::NtscColour) {
        max_width = 910;
        max_frame_height = 625;
        min_active_start = kNtscDecoderMinStart;
        right_margin = kNtscDecoderRightMargin;
    } else if (profile == DecoderVideoProfile::PalColour) {
        max_width = 1135;
        max_frame_height = 625;
        min_active_start = kPalFilterMargin;
        right_margin = kPalFilterMargin;
    }

    if (params.field_width > max_width) {
        add_warning("field_width exceeds decoder limit and will be clamped");
        params.field_width = max_width;
    }

    const int32_t frame_height_before_clamp = get_frame_height(params);
    if (frame_height_before_clamp <= 0) {
        add_error("field_height produces an invalid frame height");
        return result;
    }

    if (frame_height_before_clamp > max_frame_height) {
        add_warning("field_height exceeds decoder limit and will be clamped");
        params.field_height = (max_frame_height + 1) / 2;
    }

    const int32_t frame_height = get_frame_height(params);
    if (frame_height <= 0) {
        add_error("field_height produces an invalid frame height");
        return result;
    }

    const int32_t max_active_end = params.field_width - right_margin;
    if (max_active_end <= min_active_start) {
        add_error("field_width is too small for the selected decoder");
        return result;
    }

    const int32_t original_active_start = params.active_video_start;
    const int32_t original_active_end = params.active_video_end;

    if (params.active_video_start < min_active_start || params.active_video_start >= max_active_end) {
        add_warning("active_video_start is out of range and will be adjusted");
    }
    if (params.active_video_end <= min_active_start || params.active_video_end > max_active_end) {
        add_warning("active_video_end is out of range and will be adjusted");
    }

    params.active_video_start = std::clamp(params.active_video_start, min_active_start, max_active_end - 1);
    params.active_video_end = std::clamp(params.active_video_end, min_active_start + 1, max_active_end);
    if (params.active_video_start >= params.active_video_end) {
        add_warning("active video range is inverted and will be reset to decoder-safe bounds");
        params.active_video_start = min_active_start;
        params.active_video_end = max_active_end;
    }

    if (original_active_start != params.active_video_start || original_active_end != params.active_video_end) {
        params.first_active_field_line = std::clamp(params.first_active_field_line, 0, params.field_height);
        params.last_active_field_line = std::clamp(params.last_active_field_line, 0, params.field_height);
    }

    if (params.first_active_frame_line < 0 || params.first_active_frame_line >= frame_height) {
        add_warning("first_active_frame_line is out of range and will be adjusted");
    }
    if (params.last_active_frame_line <= 0 || params.last_active_frame_line > frame_height) {
        add_warning("last_active_frame_line is out of range and will be adjusted");
    }

    params.first_active_frame_line = std::clamp(params.first_active_frame_line, 0, frame_height - 1);
    params.last_active_frame_line = std::clamp(params.last_active_frame_line, 1, frame_height);
    if (params.first_active_frame_line >= params.last_active_frame_line) {
        add_warning("active frame line range is inverted and will be reset to the full frame");
        params.first_active_frame_line = 0;
        params.last_active_frame_line = frame_height;
    }

    const int32_t derived_first_field_line = params.first_active_frame_line / 2;
    const int32_t derived_last_field_line = (params.last_active_frame_line + 1) / 2;
    if (params.first_active_field_line < 0 || params.first_active_field_line >= params.field_height) {
        add_warning("first_active_field_line is out of range and will be derived from frame bounds");
        params.first_active_field_line = derived_first_field_line;
    } else {
        params.first_active_field_line = std::clamp(params.first_active_field_line, 0, params.field_height - 1);
    }
    if (params.last_active_field_line <= 0 || params.last_active_field_line > params.field_height) {
        add_warning("last_active_field_line is out of range and will be derived from frame bounds");
        params.last_active_field_line = derived_last_field_line;
    } else {
        params.last_active_field_line = std::clamp(params.last_active_field_line, 1, params.field_height);
    }
    if (params.first_active_field_line >= params.last_active_field_line) {
        add_warning("active field line range is inverted and will be derived from frame bounds");
        params.first_active_field_line = derived_first_field_line;
        params.last_active_field_line = std::max(derived_first_field_line + 1, derived_last_field_line);
        params.last_active_field_line = std::clamp(params.last_active_field_line, 1, params.field_height);
    }

    if (params.black_16b_ire >= params.white_16b_ire) {
        add_warning("black_16b_ire must be lower than white_16b_ire; decoder-safe defaults will be used");
        const int32_t black_level = (params.black_16b_ire >= 0) ? params.black_16b_ire : 0;
        params.black_16b_ire = black_level;
        params.white_16b_ire = black_level + 100;
    }

    if (requires_colour_reference) {
        const bool sample_rate_valid = std::isfinite(params.sample_rate) && params.sample_rate > 0.0;
        const bool fsc_valid = std::isfinite(params.fsc) && params.fsc > 0.0;

        if (!sample_rate_valid && !fsc_valid) {
            add_error("sample_rate and fsc must be positive for colour decoding");
            return result;
        }

        if (!sample_rate_valid) {
            add_warning("sample_rate is invalid and will be derived from fsc");
            params.sample_rate = params.fsc * 4.0;
        }
        if (!fsc_valid) {
            add_warning("fsc is invalid and will be derived from sample_rate");
            params.fsc = params.sample_rate / 4.0;
        }

        const int32_t max_burst_end = params.field_width;
        if (max_burst_end <= 0) {
            add_error("active_video_start leaves no room for a colour burst window");
            return result;
        }

        if (params.colour_burst_start < 0 || params.colour_burst_start >= max_burst_end) {
            add_warning("colour_burst_start is out of range and will be adjusted");
        }
        if (params.colour_burst_end <= 0 || params.colour_burst_end > max_burst_end) {
            add_warning("colour_burst_end is out of range and will be adjusted");
        }

        params.colour_burst_start = std::clamp(params.colour_burst_start, 0, std::max(0, max_burst_end - 1));
        params.colour_burst_end = std::clamp(params.colour_burst_end, 1, max_burst_end);

        if (params.colour_burst_start >= params.colour_burst_end) {
            add_warning("colour burst range is inverted and will be reset to a decoder-safe window");
            params.colour_burst_end = max_burst_end;
            params.colour_burst_start = std::max(0, params.colour_burst_end - kDefaultBurstLength);
            if (params.colour_burst_start >= params.colour_burst_end) {
                add_error("unable to derive a valid colour burst window");
                return result;
            }
        }
    } else {
        params.colour_burst_start = std::clamp(params.colour_burst_start, 0, std::max(0, params.field_width - 1));
        params.colour_burst_end = std::clamp(params.colour_burst_end, 0, params.field_width);
        if (params.colour_burst_start >= params.colour_burst_end) {
            params.colour_burst_start = 0;
            params.colour_burst_end = 0;
        }
    }

    return result;
}

} // namespace orc::chroma_sink

#endif // ORC_CORE_CHROMA_SINK_VIDEO_PARAMETER_SAFETY_H