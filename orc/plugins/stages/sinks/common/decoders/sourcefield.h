/*
 * File:        sourcefield.h
 * Module:      orc-core
 * Purpose:     Source field container
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */



#ifndef SOURCEFIELD_H
#define SOURCEFIELD_H

#include <orc_source_parameters.h>
#include <vector>
#include <cstdint>
#include <optional>

// A field with metadata and data
// Data comes from VFR (VideoFieldRepresentation) in orc-core
// All metadata is obtained through the VFR interface, not TBC metadata directly
struct SourceField {
    // Metadata from VFR interface
    int32_t seq_no = 0;  // Sequence number (field ID + 1 for 1-based indexing)
    bool is_first_field = true;  // True if this is the first field (top field)
    std::optional<int32_t> field_phase_id;  // PAL phase ID from VFR hint
    
    // For composite sources (Y+C modulated together)
    std::vector<uint16_t> data;
    
    // For YC sources (separate Y and C files)
    std::vector<uint16_t> luma_data;   // Y only (clean, no modulated chroma)
    std::vector<uint16_t> chroma_data; // C only (modulated chroma)
    bool is_yc = false;  // True if this is a YC source, false for composite

    // Return the vertical offset of this field within the interlaced frame
    // (i.e. 0 for the top field, 1 for the bottom field).
    int32_t getOffset() const {
        return is_first_field ? 0 : 1;
    }

    // Return the first/last active line numbers within this field's data,
    // given the video parameters.
    int32_t getFirstActiveLine(const ::orc::SourceParameters &videoParameters) const {
        return (videoParameters.first_active_frame_line + 1 - getOffset()) / 2;
    }
    int32_t getLastActiveLine(const ::orc::SourceParameters &videoParameters) const {
        return (videoParameters.last_active_frame_line + 1 - getOffset()) / 2;
    }
};

#endif
