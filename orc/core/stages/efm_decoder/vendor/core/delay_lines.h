/*
 * File:        delay_lines.h
 * Module:      EFM-library
 * Purpose:     Delay line functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DELAY_LINES_H
#define DELAY_LINES_H

#include <cstdint>
#include <vector>
#include <queue>

class DelayLine
{
public:
    DelayLine(int32_t _delayLength);
    // Constructor taking no argument needed for std::vector in C++
    DelayLine();
    void push(uint8_t& datum, bool& datumError, bool& datumPadded);
    bool isReady();
    void flush();

private:
    struct DelayContents_t {
        uint8_t datum;
        bool error;
        bool padded;
    };

    std::vector<DelayContents_t> m_buffer;

    bool m_ready;
    int32_t m_pushCount;
    int32_t m_delayLength;
};

class DelayLines
{
public:
    DelayLines(std::vector<int32_t> _delayLengths);
    void push(std::vector<uint8_t>& data, std::vector<bool>& errorData, std::vector<bool>& paddedData);
    bool isReady();
    void flush();

private:
    std::vector<DelayLine> m_delayLines;
};

#endif // DELAY_LINES_H
