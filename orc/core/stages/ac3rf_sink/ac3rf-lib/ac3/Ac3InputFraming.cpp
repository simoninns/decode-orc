// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#include <spdlog/fmt/fmt.h>  // orc vendor adaptation: std::format → fmt::format (C++17 compat)
#include "../Logger.h"
#include "Ac3InputFraming.h"

Ac3InputFraming::Ac3InputFraming(Logger &log)
: m_log(log)
{
}

// Finds the sync pattens and breaks the sequence up into frames
std::vector<Ac3InputFraming::NumberedFrame>
Ac3InputFraming::arrangeInFrames(const std::vector<uint8_t> &symbols) {

    std::vector<NumberedFrame> frames;

    for (int i = 0; i < symbols.size(); i++, m_total_symbols_seen++) {
        const uint8_t s = symbols[i];

        if (m_sync_frame_symbols_seen < 12 && m_auto_sync_at < m_total_symbols_seen) {
            bool is_next_sync_symbol;
            switch (m_sync_frame_symbols_seen) {
                case 0:
                    is_next_sync_symbol = s == 0;
                    break;
                case 1:
                    is_next_sync_symbol = s == 1;
                    break;
                case 2:
                    is_next_sync_symbol = s == 1;
                    break;
                case 3:
                    is_next_sync_symbol = s == 3;
                    break;
                case 4:
                case 5:
                case 6:
                case 7:
                    m_sync_frame_no[m_sync_frame_symbols_seen - 4] = s;
                    is_next_sync_symbol = true;
                    break;
                case 8:
                case 9:
                case 10:
                case 11:
                    is_next_sync_symbol = s == 0;
                    break;
                default: throw std::runtime_error("cannot happen");
            }
            if (is_next_sync_symbol) {
                m_sync_frame_symbols_seen += 1;
                if (m_sync_frame_symbols_seen == 12)
                    m_frame_number = (m_sync_frame_no[0] << 6) | (m_sync_frame_no[1] << 4) | (m_sync_frame_no[2] << 2) | m_sync_frame_no[3];
            } else {
                if (m_consecutive_synched > 0) {
                    m_log.debug(eAudio, fmt::format("Missing sync symbol index {} (consecutiveSynched={})!", m_total_symbols_seen, m_consecutive_synched));
                    m_consecutive_synched -= 1;
                    m_frame_number = (m_previous_frame_number + 1) % 72;
                    m_auto_sync_at = m_total_symbols_seen + 12 - m_sync_frame_symbols_seen;
                } else {
                    m_sync_frame_symbols_seen = 0;
                }
            }
        } else if (m_total_symbols_seen >= m_auto_sync_at) {
            if (m_sync_frame_symbols_seen == 12 && m_symbol_in_frame_counter == 0)
                m_consecutive_synched = std::min(m_consecutive_synched + 1, 7);
            else if (m_total_symbols_seen == m_auto_sync_at) {
                m_sync_frame_symbols_seen = 12;
                m_auto_sync_at = -1; // FIXME: better logic this is broken!
            }

            m_symbols_in_frame[m_symbol_in_frame_counter] = s;
            m_symbol_in_frame_counter += 1;
            if (m_symbol_in_frame_counter == 37 * 4) {
                std::array<uint8_t, 37> frame{};
                for (int j = 0; j < 37; j++)
                    frame[j] = m_symbols_in_frame[j * 4] << 6 | m_symbols_in_frame[j * 4 + 1] << 4 | m_symbols_in_frame[j * 4 + 2] << 2 | m_symbols_in_frame[j * 4 + 3];

                frames.push_back(NumberedFrame {(size_t)m_total_symbols_seen - 34 * 4 + 1, m_frame_number, frame });

                m_previous_frame_number = m_frame_number;
                m_symbol_in_frame_counter = 0;
                m_sync_frame_symbols_seen = 0;
            }
        }
    }
    return frames;
}
