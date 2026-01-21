/*
 * File:        vbi_decoder.h
 * Module:      orc-core
 * Purpose:     VBI decoding API for GUI
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_VBI_DECODER_H
#define ORC_CORE_VBI_DECODER_H

#include "vbi_types.h"
#include "field_id.h"
#include "node_id.h"
#include "lru_cache.h"
#include <memory>
#include <optional>
#include <string>

namespace orc {

// Forward declarations
class DAG;
class ObservationContext;

/**
 * @brief Complete VBI information for a field
 * 
 * This structure contains all decoded VBI data for display in the GUI,
 * matching the information shown in ld-analyse's VBI dialog.
 */
struct VBIFieldInfo {
    FieldID field_id;
    
    // Raw VBI data (3 lines: 16, 17, 18)
    std::array<int32_t, 3> vbi_data;
    
    // Frame/timecode information
    std::optional<int32_t> picture_number;       // CAV frame number
    std::optional<CLVTimecode> clv_timecode;     // CLV timecode
    std::optional<int32_t> chapter_number;       // Chapter marker
    
    // Control codes
    bool stop_code_present = false;              // Picture stop code
    bool lead_in = false;                        // Lead-in code
    bool lead_out = false;                       // Lead-out code
    std::optional<std::string> user_code;        // User code string
    
    // Programme status (original specification)
    std::optional<ProgrammeStatus> programme_status;
    
    // Amendment 2 status
    std::optional<Amendment2Status> amendment2_status;
    
    // Validity flags
    bool has_vbi_data = false;                   // True if VBI was successfully decoded
    std::string error_message;                    // Error description if decoding failed
};

/**
 * @brief VBI decoder for extracting VBI information from observation context
 * 
 * This class provides utilities for VBI decoding, allowing the GUI
 * to translate observations from ObservationContext into decoded VBI data
 * for display.
 */
class VBIDecoder {
public:
    VBIDecoder() = default;
    ~VBIDecoder() = default;
    
    /**
     * @brief Decode VBI information from observation context
     * 
     * @param observation_context The observation context to extract from
     * @param field_id The field to get VBI data for
     * @return VBI information, or empty optional if not available
     * 
     * This method extracts biphase-decoded VBI observations from the
     * observation context and formats them into VBIFieldInfo for display.
     */
    static std::optional<VBIFieldInfo> decode_vbi(
        const ObservationContext& observation_context,
        FieldID field_id);

private:
    /**
     * @brief Parse VBI data from raw observation values
     * @param field_id The field ID
     * @param vbi_line_16 VBI data from line 16
     * @param vbi_line_17 VBI data from line 17
     * @param vbi_line_18 VBI data from line 18
     * @param observation_context The observation context to extract interpreted fields from
     * @return Parsed VBI information
     */
    static VBIFieldInfo parse_vbi_data(
        FieldID field_id,
        int32_t vbi_line_16,
        int32_t vbi_line_17,
        int32_t vbi_line_18,
        const ObservationContext& observation_context);
};

} // namespace orc

#endif // ORC_CORE_VBI_DECODER_H
