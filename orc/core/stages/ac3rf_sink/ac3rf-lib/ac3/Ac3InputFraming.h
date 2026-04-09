// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#ifndef AC3RF_DECODE_AC3INPUTFRAMING_H
#define AC3RF_DECODE_AC3INPUTFRAMING_H
#include <cstdint>
#include <vector>

class Logger;

class Ac3InputFraming {
public:
    explicit Ac3InputFraming(Logger &log);
    ~Ac3InputFraming() = default;
    Ac3InputFraming(const Ac3InputFraming &) = delete;
    Ac3InputFraming(Ac3InputFraming &&) = delete;
    Ac3InputFraming &operator=(const Ac3InputFraming &) = delete;
    Ac3InputFraming &operator=(Ac3InputFraming &&) = delete;

    struct NumberedFrame {
        size_t global_symbol_index;
        int frame_number;
        std::array<uint8_t, 37> frame_data;
    };
    std::vector<NumberedFrame> arrangeInFrames(const std::vector<uint8_t> &symbols);

private:
    Logger &m_log;

    int64_t m_total_symbols_seen = 0;
    int m_frame_number = -1;
    int m_previous_frame_number = 0;

    int m_sync_frame_symbols_seen = 0;
    uint8_t m_sync_frame_no[4];
    int m_symbol_in_frame_counter = 0;
    uint8_t m_symbols_in_frame[37 * 4];
    int m_consecutive_synched = 0;
    int64_t m_auto_sync_at = -1;
};


#endif //AC3RF_DECODE_AC3INPUTFRAMING_H