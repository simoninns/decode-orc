/*
 * File:        eia608_decoder.cpp
 * Module:      orc-core
 * Purpose:     EIA-608 Closed Caption Decoder implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "eia608_decoder.h"
#include "logging.h"
#include <algorithm>
#include <cctype>

namespace orc {

// CaptionBuffer implementation

CaptionBuffer::CaptionBuffer()
    : current_row_(MAX_ROWS - 1), current_col_(0)
{
    clear();
}

void CaptionBuffer::clear()
{
    for (auto& row : rows_) {
        row.clear();
    }
    current_row_ = MAX_ROWS - 1;
    current_col_ = 0;
}

void CaptionBuffer::write_char(char c)
{
    if (current_row_ >= MAX_ROWS) {
        current_row_ = MAX_ROWS - 1;
    }
    
    // Ensure row has enough space
    if (rows_[current_row_].length() < current_col_) {
        rows_[current_row_].resize(current_col_, ' ');
    }
    
    // Allow text beyond MAX_COLS - we're converting to mov_text which doesn't have column limits
    // Original EIA-608 has 32 columns, but captions can be longer for proper display
    if (current_col_ >= rows_[current_row_].length()) {
        rows_[current_row_] += c;
    } else {
        rows_[current_row_][current_col_] = c;
    }
    current_col_++;
}

void CaptionBuffer::set_cursor(size_t row, size_t col)
{
    current_row_ = (row < MAX_ROWS) ? row : MAX_ROWS - 1;
    current_col_ = (col < MAX_COLS) ? col : 0;
    
    // Ensure the row has space up to this column
    if (rows_[current_row_].length() < current_col_) {
        rows_[current_row_].resize(current_col_, ' ');
    }
}

void CaptionBuffer::next_row()
{
    if (current_row_ < MAX_ROWS - 1) {
        current_row_++;
    }
    current_col_ = 0;
}

std::string CaptionBuffer::render() const
{
    std::vector<std::string> lines;
    
    for (size_t row_idx = 0; row_idx < rows_.size(); ++row_idx) {
        const auto& row = rows_[row_idx];
        
        // Trim leading and trailing spaces
        std::string line = row;
        while (!line.empty() && line.front() == ' ') {
            line.erase(line.begin());
        }
        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }
        
        if (!line.empty()) {
            lines.push_back(line);
        }
    }
    
    // Join non-empty lines with space
    // EIA-608 captions often span multiple rows and should be joined with spaces
    std::string result;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i > 0) result += ' ';
        result += lines[i];
    }
    
    return result;
}

void CaptionBuffer::roll_up()
{
    // Shift all rows up by one
    for (size_t i = 0; i < MAX_ROWS - 1; ++i) {
        rows_[i] = std::move(rows_[i + 1]);
    }
    
    // Clear bottom row
    rows_[MAX_ROWS - 1].clear();
    current_row_ = MAX_ROWS - 1;
    current_col_ = 0;
}

// EIA608Decoder implementation

EIA608Decoder::EIA608Decoder()
    : mode_(CaptionMode::POP_ON)
    , rollup_rows_(2)
    , current_time_(0.0)
    , last_eoc_time_(-1.0)
{
}

EIA608ControlCode EIA608Decoder::decode_control_code(uint8_t byte1, uint8_t byte2)
{
    // EIA-608 control codes are TWO-BYTE sequences
    // Miscellaneous Control Codes (Table 52):
    // Data Channel 1: 0x14 (field 1) or 0x1C (field 2), byte2 = 0x20-0x2F
    // Data Channel 2: 0x1C (field 1) or 0x1C (field 2), byte2 = 0x20-0x2F
    
    if (byte1 == 0x14 || byte1 == 0x1C) {
        // Miscellaneous control codes
        if (byte2 >= 0x20 && byte2 <= 0x2F) {
            switch (byte2) {
                case 0x20: return EIA608ControlCode::RCL;  // Resume Caption Loading
                case 0x25: return EIA608ControlCode::RU2;  // Roll-Up 2 rows
                case 0x26: return EIA608ControlCode::RU3;  // Roll-Up 3 rows
                case 0x27: return EIA608ControlCode::RU4;  // Roll-Up 4 rows
                case 0x29: return EIA608ControlCode::RDC;  // Resume Direct Captioning
                case 0x2C: return EIA608ControlCode::EDM;  // Erase Displayed Memory
                case 0x2D: return EIA608ControlCode::CR;   // Carriage Return
                case 0x2E: return EIA608ControlCode::ENM;  // Erase Non-displayed Memory
                case 0x2F: return EIA608ControlCode::EOC;  // End of Caption
                default: break;  // Other codes like BS, FON, etc. - ignore for now
            }
        }
    }
    
    // PACs (Preamble Address Codes) are handled separately in decode_pac()
    
    return EIA608ControlCode::UNKNOWN;
}

bool EIA608Decoder::decode_pac(uint8_t byte1, uint8_t byte2, size_t& row, size_t& col)
{
    // PAC format per Table 53:
    // byte1 determines which row pair (see table below)
    // byte2 bit 5 (0x20) determines even/odd within the pair
    // byte2 bits 0-3 encode indent/color
    
    if (byte2 < 0x40 || byte2 > 0x7F) {
        return false;  // Not a valid PAC
    }
    
    // Map byte1 to base row using EIA-608 row numbers
    // Table 53 mapping for Data Channel 1
    // These values represent row numbers that need to be converted to 0-based indices
    int base_row = -1;
    switch (byte1) {
        case 0x11: base_row = (byte2 & 0x20) ? 2 : 1; break;   // rows 1-2
        case 0x12: base_row = (byte2 & 0x20) ? 4 : 3; break;   // rows 3-4
        case 0x15: base_row = (byte2 & 0x20) ? 6 : 5; break;   // rows 5-6
        case 0x16: base_row = (byte2 & 0x20) ? 8 : 7; break;   // rows 7-8
        case 0x17: base_row = (byte2 & 0x20) ? 10 : 9; break;  // rows 9-10
        case 0x10: base_row = (byte2 & 0x20) ? 12 : 11; break; // rows 11-12
        case 0x13: base_row = (byte2 & 0x20) ? 14 : 13; break; // rows 13-14
        case 0x14: base_row = (byte2 & 0x20) ? 15 : 14; break; // rows 14-15 (note: row 14 appears twice)
        default: return false;
    }
    
    if (base_row < 1) {
        return false;
    }
    
    // Convert 1-based row number to 0-based array index
    // EIA-608 rows are numbered 1-15, our array uses indices 0-14
    row = static_cast<size_t>(base_row - 1);
    
    // Decode indent from bits 0-3 of byte2
    // 0x40-0x4F or 0x60-0x6F: colored text with no indent
    // 0x50-0x5E or 0x70-0x7E: white text with indent
    if ((byte2 & 0x10)) {  // Indent PACs
        int indent_code = (byte2 & 0x0E) >> 1;
        col = indent_code * 4;  // 0, 4, 8, 12, 16, 20, 24, 28
    } else {
        col = 0;  // Color/attribute PACs start at column 0
    }
    
    return true;
}

void EIA608Decoder::process_bytes(double timestamp, uint8_t byte1, uint8_t byte2)
{
    current_time_ = timestamp;
    
    // Check if this is a control code or PAC (byte1 in 0x10-0x1F range)
    if (byte1 >= 0x10 && byte1 <= 0x1F) {
        // First try to decode as a control code
        EIA608ControlCode code = decode_control_code(byte1, byte2);
        if (code != EIA608ControlCode::UNKNOWN) {
            handle_control_code(code);
            return;
        }
        
        // Not a control code, try PAC (Preamble Address Code)
        size_t row, col;
        if (decode_pac(byte1, byte2, row, col)) {
            // Set cursor position in the appropriate buffer
            if (mode_ == CaptionMode::POP_ON) {
                nondisplayed_.set_cursor(row, col);
            } else {
                displayed_.set_cursor(row, col);
            }
        }
        return;
    }
    
    // Handle printable characters (0x20-0x7F)
    if (byte1 >= 0x20 && byte1 <= 0x7F) {
        handle_printable_char(static_cast<char>(byte1));
    }
    
    if (byte2 >= 0x20 && byte2 <= 0x7F) {
        handle_printable_char(static_cast<char>(byte2));
    }
}

void EIA608Decoder::handle_printable_char(char c)
{
    switch (mode_) {
        case CaptionMode::POP_ON:
            nondisplayed_.write_char(c);
            break;
            
        case CaptionMode::ROLL_UP:
            displayed_.write_char(c);
            ensure_rollup_cue_started();
            break;
            
        case CaptionMode::PAINT_ON:
            displayed_.write_char(c);
            ensure_painton_cue_started(c);
            break;
    }
}

void EIA608Decoder::handle_control_code(EIA608ControlCode code)
{
    switch (code) {
        case EIA608ControlCode::RCL:
            // Only clear if we're entering Pop-On mode from a different mode
            if (mode_ != CaptionMode::POP_ON) {
                nondisplayed_.clear();
            }
            mode_ = CaptionMode::POP_ON;
            break;
            
        case EIA608ControlCode::EOC:
            if (mode_ == CaptionMode::POP_ON) {
                // Deduplicate: EOC is often sent on both fields (Field 1 and Field 2)
                // Ignore if we just processed an EOC within 0.1 seconds
                if (current_time_ - last_eoc_time_ < 0.1) {
                    break;
                }
                last_eoc_time_ = current_time_;
                
                // EOC swaps buffers and displays the new content
                // First, close any existing displayed caption
                for (auto& cue : active_cues_) {
                    cue->end_time = current_time_;
                    emit_cue(*cue);
                }
                active_cues_.clear();
                
                // Swap buffers (non-displayed becomes displayed)
                swap_buffers();
                
                // Open new cue for the newly displayed content
                open_popon_cue();
                
                // Clear the now non-displayed buffer so we can load new content
                nondisplayed_.clear();
            }
            break;
            
        case EIA608ControlCode::EDM:
            close_all_cues();
            displayed_.clear();
            break;
            
        case EIA608ControlCode::ENM:
            nondisplayed_.clear();
            break;
            
        case EIA608ControlCode::CR:
            if (mode_ == CaptionMode::ROLL_UP) {
                roll_up();
            } else if (mode_ == CaptionMode::POP_ON) {
                // In Pop-On mode, CR moves to the next row for multi-line captions
                nondisplayed_.next_row();
            } else if (mode_ == CaptionMode::PAINT_ON) {
                displayed_.next_row();
            }
            break;
            
        case EIA608ControlCode::RU2:
        case EIA608ControlCode::RU3:
        case EIA608ControlCode::RU4:
            close_all_cues();
            mode_ = CaptionMode::ROLL_UP;
            rollup_rows_ = (code == EIA608ControlCode::RU2) ? 2 :
                          (code == EIA608ControlCode::RU3) ? 3 : 4;
            break;
            
        case EIA608ControlCode::RDC:
            close_all_cues();
            mode_ = CaptionMode::PAINT_ON;
            break;
            
        default:
            break;
    }
}

void EIA608Decoder::open_popon_cue()
{
    std::string text = displayed_.render();
    if (text.empty()) {
        return;
    }
    
    ORC_LOG_DEBUG("EIA608Decoder: Opening cue with text: '{}'", text);
    
    auto cue = std::make_shared<CaptionCue>(current_time_, -1.0, text);
    active_cues_.push_back(cue);
}

// No longer needed - we use close_all_cues() instead
// void EIA608Decoder::close_popon_cue()

void EIA608Decoder::swap_buffers()
{
    std::swap(displayed_, nondisplayed_);
}

void EIA608Decoder::ensure_rollup_cue_started()
{
    if (active_cues_.empty()) {
        std::string text = displayed_.render();
        auto cue = std::make_shared<CaptionCue>(current_time_, -1.0, text);
        active_cues_.push_back(cue);
    } else {
        // Update existing cue text
        active_cues_[0]->text = displayed_.render();
    }
}

void EIA608Decoder::roll_up()
{
    // Close current cue and emit it
    if (!active_cues_.empty()) {
        auto cue = active_cues_[0];
        cue->end_time = current_time_;
        emit_cue(*cue);
        active_cues_.clear();
    }
    
    // Scroll the buffer
    displayed_.roll_up();
}

void EIA608Decoder::ensure_painton_cue_started(char c)
{
    if (active_cues_.empty()) {
        auto cue = std::make_shared<CaptionCue>(current_time_, -1.0, "");
        active_cues_.push_back(cue);
    }
    
    // Append character to active cue
    active_cues_[0]->text += c;
}

void EIA608Decoder::close_all_cues()
{
    for (auto& cue : active_cues_) {
        cue->end_time = current_time_;
        emit_cue(*cue);
    }
    active_cues_.clear();
}

void EIA608Decoder::emit_cue(const CaptionCue& cue)
{
    // Validate cue before emitting
    if (cue.end_time <= cue.start_time) {
        return;
    }
    
    if (cue.text.empty()) {
        return;
    }
    
    // Trim whitespace from text
    std::string text = cue.text;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    
    if (text.empty()) {
        return;
    }
    
    emitted_cues_.emplace_back(cue.start_time, cue.end_time, text);
}

std::vector<CaptionCue> EIA608Decoder::finalize(double end_time)
{
    current_time_ = end_time;
    close_all_cues();
    return emitted_cues_;
}

} // namespace orc
