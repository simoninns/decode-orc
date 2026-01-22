/*
 * File:        fm_code_observer.cpp
 * Module:      orc-core
 * Purpose:     FM code observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "fm_code_observer.h"
#include "logging.h"
#include "tbc_video_field_representation.h"
#include "vbi_utilities.h"

namespace orc {

void FmCodeObserver::process_field(
	const VideoFieldRepresentation& representation,
	FieldID field_id,
	ObservationContext& context)
{
	auto descriptor = representation.get_descriptor(field_id);
	if (!descriptor.has_value()) {
		ORC_LOG_DEBUG("FmCodeObserver: Field {} - no descriptor", field_id.value());
		return;
	}
    
	// Only applicable to NTSC
	if (descriptor->format != VideoFormat::NTSC) {
		ORC_LOG_DEBUG("FmCodeObserver: Field {} - not NTSC (format={})", field_id.value(), static_cast<int>(descriptor->format));
		return;
	}
    
	// Line 10 (0-based index 9)
	constexpr size_t line_num = 9;
	if (line_num >= descriptor->height) {
		ORC_LOG_DEBUG("FmCodeObserver: Field {} - line 9 out of bounds (height={})", field_id.value(), descriptor->height);
		return;
	}
    
	const uint16_t* line_data = representation.get_line(field_id, line_num);
	if (!line_data) {
		ORC_LOG_DEBUG("FmCodeObserver: Field {} - no line data for line 9", field_id.value());
		return;
	}
    
	// Derive zero-crossing and sample rate from video parameters if available
	uint16_t zero_crossing = 0;
	double sample_rate = 40000000.0; // Default fallback (40MHz)
	size_t active_start = descriptor->width / 8;
	
	if (auto video_params_opt = representation.get_video_parameters()) {
		const auto& vp = *video_params_opt;
		zero_crossing = static_cast<uint16_t>(
			((vp.white_16b_ire - vp.black_16b_ire) / 2) + vp.black_16b_ire);
		sample_rate = vp.sample_rate;
		active_start = vp.active_video_start;
	} else {
		// Fallback to legacy constants
		zero_crossing = static_cast<uint16_t>((50000 + 15000) / 2);
	}
    
	// Calculate bit timing: 0.75 microseconds per bit at actual sample rate
	double jump_samples = (sample_rate / 1000000.0) * 0.75;
	
	ORC_LOG_DEBUG("FmCodeObserver: Field {} - sample_rate={}, jump_samples={:.2f}, active_start={}", 
				  field_id.value(), static_cast<size_t>(sample_rate), jump_samples, active_start);
    
	DecodedFmCode decoded;
	bool success = decode_line(line_data,
							   descriptor->width,
							   zero_crossing,
							   active_start,
							   jump_samples,
							   decoded);
    
	if (!success) {
		ORC_LOG_DEBUG("FmCodeObserver: Field {} - decode_line failed", field_id.value());
		return;
	}
    
	context.set(field_id, "fm_code", "present", true);
	context.set(field_id, "fm_code", "data_value", static_cast<int32_t>(decoded.data_value));
	context.set(field_id, "fm_code", "field_flag", decoded.field_flag);
    
	ORC_LOG_DEBUG("FmCodeObserver: Field {} fm_code={:#06x} field_flag={}",
				  field_id.value(), decoded.data_value, decoded.field_flag);
}

bool FmCodeObserver::decode_line(const uint16_t* line_data,
								 size_t sample_count,
								 uint16_t zero_crossing,
								 size_t active_start,
								 double jump_samples,
								 DecodedFmCode& out) const
{
	auto fm_data = vbi_utils::get_transition_map(line_data, sample_count, zero_crossing);
    
	// Find first transition in the active region
	size_t x = active_start;
	while (x < fm_data.size() && !fm_data[x]) {
		x++;
	}
	if (x >= fm_data.size()) {
		ORC_LOG_DEBUG("FmCodeObserver: No transition found in active region");
		return false;
	}
    
	uint64_t decoded_bits = 0;
	int decode_count = 0;
	size_t last_transition_x = x;
	bool last_state = fm_data[x];
    
	// Decode 40 bits
	while (x < fm_data.size() && decode_count < 40) {
		while (x < fm_data.size() && fm_data[x] == last_state) {
			x++;
		}
		if (x >= fm_data.size()) break;
        
		last_state = fm_data[x];
        
		// Transition in middle of cell = 1, otherwise 0
		if (x - last_transition_x < static_cast<size_t>(jump_samples)) {
			decoded_bits = (decoded_bits << 1) | 1;
			last_transition_x = x;
			decode_count++;
            
			while (x < fm_data.size() && fm_data[x] == last_state) {
				x++;
			}
			if (x >= fm_data.size()) break;
			last_state = fm_data[x];
			last_transition_x = x;
		} else {
			decoded_bits = decoded_bits << 1;
			last_transition_x = x;
			decode_count++;
		}
		x++;
	}
    
	if (decode_count != 40) {
		ORC_LOG_DEBUG("FmCodeObserver: Incomplete decode - only got {} bits", decode_count);
		return false;
	}
    
	// Parse 40-bit structure
	uint64_t clock_sync = (decoded_bits & 0xF000000000ULL) >> 36;
	uint64_t field_indicator = (decoded_bits & 0x0800000000ULL) >> 35;
	uint64_t leading_sync = (decoded_bits & 0x07F0000000ULL) >> 28;
	uint64_t data_value = (decoded_bits & 0x000FFFFF00ULL) >> 8;
	uint64_t parity_bit = (decoded_bits & 0x0000000080ULL) >> 7;
	uint64_t trailing_sync = (decoded_bits & 0x000000007FULL);
    
	ORC_LOG_DEBUG("FmCodeObserver: Decoded bits - clock_sync={} leading_sync={} trailing_sync={}", 
				  clock_sync, leading_sync, trailing_sync);
    
	// Validate sync patterns
	if (clock_sync != 3 || leading_sync != 114 || trailing_sync != 13) {
		ORC_LOG_DEBUG("FmCodeObserver: Sync pattern validation failed - clock_sync={} (expect 3), leading_sync={} (expect 114), trailing_sync={} (expect 13)",
					  clock_sync, leading_sync, trailing_sync);
		return false;
	}
    
	// Check parity (odd overall)
	bool data_even_parity = vbi_utils::is_even_parity(static_cast<uint32_t>(data_value));
	if ((parity_bit == 1 && !data_even_parity) || (parity_bit == 0 && data_even_parity)) {
		ORC_LOG_DEBUG("FmCodeObserver: Parity check failed - parity_bit={}, data_even_parity={}",
					  parity_bit, data_even_parity);
		return false;
	}
    
	out.data_value = static_cast<uint32_t>(data_value);
	out.field_flag = (field_indicator != 0);
	return true;
}

} // namespace orc
