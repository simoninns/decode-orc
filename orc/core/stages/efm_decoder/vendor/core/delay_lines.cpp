/*
 * File:        delay_lines.cpp
 * Module:      EFM-library
 * Purpose:     Delay line functions
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "delay_lines.h"
#include <logging.h>
#include <cstdlib>
#include <algorithm>

DelayLines::DelayLines(std::vector<int32_t> delayLengths)
{
    m_delayLines.reserve(delayLengths.size());
    for (int32_t i = 0; i < static_cast<int32_t>(delayLengths.size()); ++i) {
        m_delayLines.push_back(DelayLine(delayLengths[i]));
    }
}

void DelayLines::push(std::vector<uint8_t>& data, std::vector<bool>& errorData, std::vector<bool>& paddedData)
{
    if (data.size() != m_delayLines.size()) {
        LOG_ERROR("Input data size does not match the number of delay lines.");
        std::exit(1);
    }

    // Process each input value through its corresponding delay line
    for (int32_t i = 0; i < static_cast<int32_t>(m_delayLines.size()); ++i) {
        // Use local variables to convert vector<bool> proxy objects to real bool references
        uint8_t datum = data[i];
        bool datum_error = errorData[i];
        bool datum_padded = paddedData[i];
        
        m_delayLines[i].push(datum, datum_error, datum_padded);
        
        // Copy the output values back
        data[i] = datum;
        errorData[i] = datum_error;
        paddedData[i] = datum_padded;
    }

    // Clear the vector if delay lines aren't ready (in order to
    // return empty data vectors)
    if (!isReady()) {
        data.clear();
        errorData.clear();
        paddedData.clear();
    }
}

bool DelayLines::isReady()
{
    for (int32_t i = 0; i < static_cast<int32_t>(m_delayLines.size()); ++i) {
        if (!m_delayLines[i].isReady()) {
            return false;
        }
    }
    return true;
}

void DelayLines::flush()
{
    for (int32_t i = 0; i < static_cast<int32_t>(m_delayLines.size()); ++i) {
        m_delayLines[i].flush();
    }
}

// DelayLine class implementation
DelayLine::DelayLine(int32_t delayLength) :
    m_ready(false),
    m_pushCount(0)
{
    m_buffer.resize(delayLength);
    m_delayLength = delayLength;

    flush();
}

// DelayLine class implementation
DelayLine::DelayLine() : DelayLine(0)
{
}

void DelayLine::push(uint8_t& datum, bool& datumError, bool& datumPadded)
{
    if (m_delayLength == 0) {
        return;
    }

    // Store the input value temporarily
    uint8_t tempInput = datum;
    bool tempInputError = datumError;
    bool tempInputPadded = datumPadded;

    DelayContents_t temp;
    
    // Return output through the reference parameters

    // Get the first value
    temp = m_buffer.front();
    datum = temp.datum;
    datumError = temp.error;
    datumPadded = temp.padded;

    // Remove first element and add new one at the end
    m_buffer.erase(m_buffer.begin());
    temp.datum = tempInput;
    temp.error = tempInputError;
    temp.padded = tempInputPadded;
    m_buffer.push_back(temp);

    // Check if the delay line is ready
    if (m_pushCount >= m_delayLength) {
        m_ready = true;
    } else {
        ++m_pushCount;
    }
}

bool DelayLine::isReady()
{
    return m_ready;
}

void DelayLine::flush()
{
    if (m_delayLength > 0) {
        DelayContents_t temp;
        temp.datum = 0;
        temp.error = false;
        temp.padded = false;
        std::fill(m_buffer.begin(), m_buffer.end(), temp);
        
        m_ready = false;
    } else {
        m_ready = true;
    }
    m_pushCount = 0;
}
