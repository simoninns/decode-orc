/*
 * File:        closed_caption_observer.cpp
 * Module:      orc-core
 * Purpose:     Closed caption observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "closed_caption_observer.h"
#include "logging.h"
#include "vbi_utilities.h"
#include "video_field_representation.h"
#include "observation_context.h"

namespace orc {

void ClosedCaptionObserver::process_field(
	const VideoFieldRepresentation& representation,
	FieldID field_id,
	ObservationContext& context)
{
	auto descriptor = representation.get_descriptor(field_id);
	if (!descriptor.has_value()) {
		context.set(field_id, "closed_caption", "present", false);
		return;
	}
    
	// Closed captions are only present on NTSC field 2 line 21 or PAL line 22
	if (descriptor->format == VideoFormat::NTSC && (field_id.value() % 2 == 0)) {
		context.set(field_id, "closed_caption", "present", false);
		return;
	}
    
	size_t line_num = (descriptor->format == VideoFormat::NTSC) ? 20 : 21; // 0-based
	if (line_num >= descriptor->height) {
		context.set(field_id, "closed_caption", "present", false);
		return;
	}
    
	const uint16_t* line_data = representation.get_line(field_id, line_num);
	if (!line_data) {
		context.set(field_id, "closed_caption", "present", false);
		return;
	}
    
	auto video_params_opt = representation.get_video_parameters();
	if (!video_params_opt.has_value()) {
		context.set(field_id, "closed_caption", "present", false);
		return;
	}
	const auto& video_params = video_params_opt.value();
    
	if (video_params.white_16b_ire < 0 || video_params.black_16b_ire < 0 ||
		video_params.colour_burst_end < 0) {
		context.set(field_id, "closed_caption", "present", false);
		return;
	}
    
	uint16_t zero_crossing = static_cast<uint16_t>(
		((video_params.white_16b_ire - video_params.black_16b_ire) / 4) +
		video_params.black_16b_ire);
    
	double samples_per_bit = static_cast<double>(descriptor->width) / 32.0; // 32x fH
	size_t colorburst_end = static_cast<size_t>(video_params.colour_burst_end);
    
	DecodedCaption decoded;
	bool success = decode_line(
		line_data,
		descriptor->width,
		zero_crossing,
		colorburst_end,
		samples_per_bit,
		decoded);
    
	context.set(field_id, "closed_caption", "present", success);
	if (!success) {
		return;
	}
    
	context.set(field_id, "closed_caption", "data0", static_cast<int32_t>(decoded.data0));
	context.set(field_id, "closed_caption", "data1", static_cast<int32_t>(decoded.data1));
	context.set(field_id, "closed_caption", "parity0_valid", decoded.parity_valid0);
	context.set(field_id, "closed_caption", "parity1_valid", decoded.parity_valid1);
    
	ORC_LOG_DEBUG("ClosedCaptionObserver: Field {} CC=[{:#04x}, {:#04x}] parity=({}, {})",
				  field_id.value(), decoded.data0, decoded.data1,
				  decoded.parity_valid0, decoded.parity_valid1);
}

std::vector<ObservationKey> ClosedCaptionObserver::get_provided_observations() const
{
	return {
		{"closed_caption", "present", ObservationType::BOOL, "Closed caption data decoded", true},
		{"closed_caption", "data0", ObservationType::INT32, "First EIA-608 byte (7-bit + parity)", true},
		{"closed_caption", "data1", ObservationType::INT32, "Second EIA-608 byte (7-bit + parity)", true},
		{"closed_caption", "parity0_valid", ObservationType::BOOL, "Parity validity for first byte", true},
		{"closed_caption", "parity1_valid", ObservationType::BOOL, "Parity validity for second byte", true},
	};
}

bool ClosedCaptionObserver::decode_line(const uint16_t* line_data,
										size_t sample_count,
										uint16_t zero_crossing,
										size_t colorburst_end,
										double samples_per_bit,
										DecodedCaption& decoded) const
{
	if (sample_count == 0 || samples_per_bit <= 0.0) {
		return false;
	}
    
	auto transition_map = vbi_utils::get_transition_map(
		line_data, sample_count, zero_crossing);
    
	// Find 00 start bits (1.5-bit low period)
	double x = static_cast<double>(colorburst_end) + (2.0 * samples_per_bit);
	double x_limit = static_cast<double>(sample_count) - (17.0 * samples_per_bit);
	double last_one = x;
    
	while ((x - last_one) < (1.5 * samples_per_bit)) {
		if (x >= x_limit || x < 0.0 || static_cast<size_t>(x) >= transition_map.size()) {
			return false;
		}
		if (transition_map[static_cast<size_t>(x)]) last_one = x;
		x += 1.0;
	}
    
	// Find 1 start bit
	if (!vbi_utils::find_transition(transition_map, true, x, x_limit)) {
		return false;
	}
    
	// Skip start bit, move to first data bit
	x += 1.5 * samples_per_bit;
    
	// Decode first byte (7 bits + parity)
	uint8_t byte0 = 0;
	for (int i = 0; i < 7; ++i) {
		if (static_cast<size_t>(x) >= transition_map.size()) return false;
		byte0 >>= 1;
		if (transition_map[static_cast<size_t>(x)]) byte0 += 64;
		x += samples_per_bit;
	}
	if (static_cast<size_t>(x) >= transition_map.size()) return false;
	uint8_t parity0 = transition_map[static_cast<size_t>(x)] ? 1 : 0;
	x += samples_per_bit;
    
	// Decode second byte
	uint8_t byte1 = 0;
	for (int i = 0; i < 7; ++i) {
		if (static_cast<size_t>(x) >= transition_map.size()) return false;
		byte1 >>= 1;
		if (transition_map[static_cast<size_t>(x)]) byte1 += 64;
		x += samples_per_bit;
	}
	if (static_cast<size_t>(x) >= transition_map.size()) return false;
	uint8_t parity1 = transition_map[static_cast<size_t>(x)] ? 1 : 0;
    
	decoded.data0 = byte0;
	decoded.data1 = byte1;
    
	// Odd parity: parity bit should make total number of 1 bits odd
	decoded.parity_valid0 = !(vbi_utils::is_even_parity(byte0) && parity0 != 1);
	decoded.parity_valid1 = !(vbi_utils::is_even_parity(byte1) && parity1 != 1);
    
	return true;
}

} // namespace orc
