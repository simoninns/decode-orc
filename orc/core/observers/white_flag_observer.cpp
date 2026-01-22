/*
 * File:        white_flag_observer.cpp
 * Module:      orc-core
 * Purpose:     White flag observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "white_flag_observer.h"
#include "logging.h"

namespace orc {

void WhiteFlagObserver::process_field(
	const VideoFieldRepresentation& representation,
	FieldID field_id,
	ObservationContext& context)
{
	auto descriptor = representation.get_descriptor(field_id);
	if (!descriptor.has_value()) {
		return;
	}
    
	// Only applicable to NTSC
	if (descriptor->format != VideoFormat::NTSC) {
		return;
	}
    
	// Line 11 (0-based index 10)
	constexpr size_t line_num = 10;
	if (line_num >= descriptor->height) {
		return;
	}
    
	const uint16_t* line_data = representation.get_line(field_id, line_num);
	if (!line_data) {
		return;
	}
    
	uint16_t zero_crossing = 0;
	if (auto video_params_opt = representation.get_video_parameters()) {
		const auto& vp = *video_params_opt;
		zero_crossing = static_cast<uint16_t>(
			((vp.white_16b_ire - vp.black_16b_ire) / 2) + vp.black_16b_ire);
	} else {
		zero_crossing = static_cast<uint16_t>((50000 + 15000) / 2);
	}
    
	size_t active_start = descriptor->width / 8;
	size_t active_end = descriptor->width * 7 / 8;
	if (active_end <= active_start) {
		return;
	}
    
	size_t white_count = 0;
	size_t total_count = active_end - active_start;
	for (size_t i = active_start; i < active_end; ++i) {
		if (line_data[i] > zero_crossing) {
			white_count++;
		}
	}
    
	bool present = (white_count > total_count / 2);
	context.set(field_id, "white_flag", "present", present);
    
	ORC_LOG_DEBUG("WhiteFlagObserver: Field {} white_flag={} (white {}/{} samples)",
				  field_id.value(), present, white_count, total_count);
}

} // namespace orc
