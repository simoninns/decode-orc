/*
 * File:        eia608_decoder.h
 * Module:      orc-core
 * Purpose:     EIA-608 Closed Caption Decoder for timed text conversion
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#ifndef ORC_EIA608_DECODER_H
#define ORC_EIA608_DECODER_H

#include <string>
#include <vector>
#include <array>
#include <memory>
#include <cstdint>

namespace orc {

/**
 * @brief EIA-608 Control Codes
 */
enum class EIA608ControlCode {
    RCL,  // Resume Caption Loading (Pop-On)
    EOC,  // End of Caption (swap buffers + display Pop-On)
    EDM,  // Erase Displayed Memory
    ENM,  // Erase Non-displayed Memory
    CR,   // Carriage Return (Roll-Up)
    RU2,  // Roll-Up 2 rows
    RU3,  // Roll-Up 3 rows
    RU4,  // Roll-Up 4 rows
    RDC,  // Resume Direct Captioning (Paint-On)
    UNKNOWN
};

/**
 * @brief Caption display mode
 */
enum class CaptionMode {
    POP_ON,   // Pop-On captions (prepare in buffer, then display all at once)
    ROLL_UP,  // Roll-Up captions (scroll up with new text)
    PAINT_ON  // Paint-On captions (characters appear as received)
};

/**
 * @brief Timed caption cue
 */
struct CaptionCue {
    double start_time;  // Start time in seconds
    double end_time;    // End time in seconds
    std::string text;   // Caption text (may contain newlines)
    
    CaptionCue(double start, double end, std::string txt)
        : start_time(start), end_time(end), text(std::move(txt)) {}
};

/**
 * @brief Caption buffer (15 rows x 32 columns for EIA-608)
 */
class CaptionBuffer {
public:
    static constexpr size_t MAX_ROWS = 15;
    static constexpr size_t MAX_COLS = 32;
    
    CaptionBuffer();
    
    void clear();
    void write_char(char c);
    void set_cursor(size_t row, size_t col);
    void next_row();  // Move to next row (for CR in Pop-On mode)
    std::string render() const;
    void roll_up();
    
    // Getters for debugging
    size_t get_row() const { return current_row_; }
    size_t get_col() const { return current_col_; }
    
private:
    std::array<std::string, MAX_ROWS> rows_;
    size_t current_row_;
    size_t current_col_;
};

/**
 * @brief EIA-608 Closed Caption Decoder
 * 
 * Converts raw EIA-608 byte pairs to timed text cues suitable for mov_text.
 * Handles Pop-On, Roll-Up, and Paint-On caption modes.
 */
class EIA608Decoder {
public:
    EIA608Decoder();
    ~EIA608Decoder() = default;
    
    /**
     * @brief Process a pair of EIA-608 bytes at a given timestamp
     * @param timestamp Time in seconds
     * @param byte1 First data byte (with parity bit removed)
     * @param byte2 Second data byte (with parity bit removed)
     */
    void process_bytes(double timestamp, uint8_t byte1, uint8_t byte2);
    
    /**
     * @brief Finalize decoding and get all emitted cues
     * @param end_time Final timestamp to close any open cues
     * @return Vector of timed caption cues
     */
    std::vector<CaptionCue> finalize(double end_time);
    
    /**
     * @brief Get currently accumulated cues (without finalizing)
     */
    const std::vector<CaptionCue>& get_cues() const { return emitted_cues_; }
    
private:
    // Internal state
    CaptionMode mode_;
    CaptionBuffer displayed_;
    CaptionBuffer nondisplayed_;
    int rollup_rows_;
    double current_time_;
    double last_eoc_time_;  // Track last EOC to deduplicate
    
    std::vector<std::shared_ptr<CaptionCue>> active_cues_;
    std::vector<CaptionCue> emitted_cues_;
    
    // Processing helpers
    EIA608ControlCode decode_control_code(uint8_t byte1, uint8_t byte2);
    bool decode_pac(uint8_t byte1, uint8_t byte2, size_t& row, size_t& col);
    void handle_control_code(EIA608ControlCode code);
    void handle_printable_char(char c);
    
    // Mode-specific handlers
    void open_popon_cue();
    void close_popon_cue();
    void swap_buffers();
    void ensure_rollup_cue_started();
    void roll_up();
    void ensure_painton_cue_started(char c);
    
    // Cue management
    void close_all_cues();
    void emit_cue(const CaptionCue& cue);
};

} // namespace orc

#endif // ORC_EIA608_DECODER_H
