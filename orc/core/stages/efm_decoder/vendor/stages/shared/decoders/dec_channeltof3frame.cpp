/*
 * File:        dec_channeltof3frame.cpp
 * Module:      efm-decoder-f2
 * Purpose:     EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_channeltof3frame.h"
#include <logging.h>
#include <queue>
#include <stdexcept>
#include <cmath>
#include <sstream>

ChannelToF3Frame::ChannelToF3Frame()
{
    // Statistics
    m_goodFrames = 0;
    m_undershootFrames = 0;
    m_overshootFrames = 0;

    m_validEfmSymbols = 0;
    m_invalidEfmSymbols = 0;

    m_validSubcodeSymbols = 0;
    m_invalidSubcodeSymbols = 0;
}

void ChannelToF3Frame::pushFrame(const std::vector<uint8_t> &data)
{
    // Add the data to the input buffer
    m_inputBuffer.push(data);

    // Process queue
    processQueue();
}

F3Frame ChannelToF3Frame::popFrame()
{
    // Return the first item in the output buffer
    F3Frame frame = m_outputBuffer.front();
    m_outputBuffer.pop();
    return frame;
}

bool ChannelToF3Frame::isReady() const
{
    // Return true if the output buffer is not empty
    return !m_outputBuffer.empty();
}

void ChannelToF3Frame::processQueue()
{
    while (!m_inputBuffer.empty()) {
        // Extract the first item in the input buffer
        std::vector<uint8_t> frameData = m_inputBuffer.front();
        m_inputBuffer.pop();

        // Count the number of bits in the frame
        int bitCount = 0;
        for (int i = 0; i < frameData.size(); ++i) {
            bitCount += frameData.at(i);
        }

        // Generate statistics
        if (bitCount != 588)
            LOG_DEBUG("ChannelToF3Frame::processQueue() - Frame data is {} bits (should be 588)", bitCount);
        if (bitCount == 588)
            m_goodFrames++;
        if (bitCount < 588)
            m_undershootFrames++;
        if (bitCount > 588)
            m_overshootFrames++;

        // Create an F3 frame
        F3Frame f3Frame = createF3Frame(frameData);

        // Place the frame into the output buffer
        m_outputBuffer.push(f3Frame);
    }
}

F3Frame ChannelToF3Frame::createF3Frame(const std::vector<uint8_t> &tValues)
{
    F3Frame f3Frame;

    // The channel frame data is:
    //   Sync Header: 24 bits (bits 0-23)
    //   Merging bits: 3 bits (bits 24-26)
    //   Subcode: 14 bits (bits 27-40)
    //   Merging bits: 3 bits (bits 41-43)
    //   Then 32x 17-bit data values (bits 44-587)
    //     Data: 14 bits
    //     Merging bits: 3 bits
    //
    // Giving a total of 588 bits

    // Convert the T-values to data
    std::vector<uint8_t> frameData = tvaluesToData(tValues);

    // Extract the subcode in bits 27-40
    uint16_t subcode = m_efm.fourteenToEight(getBits(frameData, 27, 40));
    if (subcode == 300) {
        subcode = 0;
        m_invalidSubcodeSymbols++;
    } else {
        m_validSubcodeSymbols++;
    }

    // Extract the data values in bits 44-587 ignoring the merging bits
    std::vector<uint8_t> dataValues;
    std::vector<bool> errorValues;
    for (int i = 44; i < (frameData.size() * 8) - 13; i += 17) {
        uint16_t dataValue = m_efm.fourteenToEight(getBits(frameData, i, i + 13));

        if (dataValue < 256) {
            dataValues.push_back(dataValue);
            errorValues.push_back(false);
            m_validEfmSymbols++;
        } else {
            dataValues.push_back(0);
            errorValues.push_back(true);
            m_invalidEfmSymbols++;
        }
    }

    // If the data values are not a multiple of 32 (due to undershoot), pad with zeros
    while (dataValues.size() < 32) {
        dataValues.push_back(0);
        errorValues.push_back(true);
    }

    // Create an F3 frame...

    // Determine the frame type
    if (subcode == 256)
        f3Frame.setFrameTypeAsSync0();
    else if (subcode == 257)
        f3Frame.setFrameTypeAsSync1();
    else
        f3Frame.setFrameTypeAsSubcode(subcode);

    // Set the frame data
    f3Frame.setData(dataValues);
    f3Frame.setErrorData(errorValues);

    return f3Frame;
}

std::vector<uint8_t> ChannelToF3Frame::tvaluesToData(const std::vector<uint8_t> &tValues)
{
    // Pre-allocate output buffer (each T-value generates at least 1 bit)
    std::vector<uint8_t> outputData;
    outputData.reserve((tValues.size() + 7) / 8);
    
    uint32_t bitBuffer = 0;  // Use 32-bit buffer to avoid frequent byte writes
    int bitsInBuffer = 0;

    for (uint8_t tValue : tValues) {
        // Validate T-value
        if (tValue < 3 || tValue > 11) {
            throw std::runtime_error("ChannelToF3Frame::tvaluesToData(): T-value must be in the range 3 to 11.");
        }

        // Shift in 1 followed by (tValue-1) zeros
        bitBuffer = (bitBuffer << tValue) | (1U << (tValue - 1));
        bitsInBuffer += tValue;

        // Write complete bytes when we have 8 or more bits
        while (bitsInBuffer >= 8) {
            outputData.push_back(static_cast<char>(bitBuffer >> (bitsInBuffer - 8)));
            bitsInBuffer -= 8;
        }
    }

    // Handle remaining bits
    if (bitsInBuffer > 0) {
        bitBuffer <<= (8 - bitsInBuffer);
        outputData.push_back(static_cast<char>(bitBuffer));
    }

    return outputData;
}

uint16_t ChannelToF3Frame::getBits(const std::vector<uint8_t> &data, int startBit, int endBit)
{
    // Validate input
    if (startBit < 0 || startBit > 587 || endBit < 0 || endBit > 587 || startBit > endBit) {
        throw std::runtime_error("ChannelToF3Frame::getBits(): Invalid bit range");
    }

    int startByte = startBit / 8;
    int endByte = endBit / 8;
    
    if (endByte >= data.size()) {
        throw std::runtime_error("ChannelToF3Frame::getBits(): Byte index exceeds data size");
    }

    // Fast path for bits within a single byte
    if (startByte == endByte) {
        uint8_t mask = (0xFF >> (startBit % 8)) & (0xFF << (7 - (endBit % 8)));
        return (data[startByte] & mask) >> (7 - (endBit % 8));
    }

    // Handle multi-byte case
    uint16_t result = 0;
    int bitsRemaining = endBit - startBit + 1;
    
    // Handle first byte
    int firstByteBits = 8 - (startBit % 8);
    uint8_t mask = 0xFF >> (startBit % 8);
    result = (data[startByte] & mask) << (bitsRemaining - firstByteBits);
    
    // Handle middle bytes
    for (int i = startByte + 1; i < endByte; i++) {
        result |= (data[i] & 0xFF) << (bitsRemaining - firstByteBits - 8 * (i - startByte));
    }
    
    // Handle last byte
    int lastByteBits = (endBit % 8) + 1;
    mask = 0xFF << (8 - lastByteBits);
    result |= (data[endByte] & mask) >> (8 - lastByteBits);

    return result;
}

void ChannelToF3Frame::showStatistics() const
{
    LOG_INFO("Channel to F3 Frame statistics:");
    LOG_INFO("  Channel Frames:");
    LOG_INFO("    Total: {}", m_goodFrames + m_undershootFrames + m_overshootFrames);
    LOG_INFO("    Good: {}", m_goodFrames);
    LOG_INFO("    Undershoot: {}", m_undershootFrames);
    LOG_INFO("    Overshoot: {}", m_overshootFrames);
    LOG_INFO("  EFM symbols:");
    LOG_INFO("    Valid: {}", m_validEfmSymbols);
    LOG_INFO("    Invalid: {}", m_invalidEfmSymbols);
    LOG_INFO("  Subcode symbols:");
    LOG_INFO("    Valid: {}", m_validSubcodeSymbols);
    LOG_INFO("    Invalid: {}", m_invalidSubcodeSymbols);
}

std::string ChannelToF3Frame::statisticsText() const
{
    std::ostringstream output;
    output << "Channel to F3 Frame statistics:\n";
    output << "  Channel Frames:\n";
    output << "    Total: " << (m_goodFrames + m_undershootFrames + m_overshootFrames) << "\n";
    output << "    Good: " << m_goodFrames << "\n";
    output << "    Undershoot: " << m_undershootFrames << "\n";
    output << "    Overshoot: " << m_overshootFrames << "\n";
    output << "  EFM symbols:\n";
    output << "    Valid: " << m_validEfmSymbols << "\n";
    output << "    Invalid: " << m_invalidEfmSymbols << "\n";
    output << "  Subcode symbols:\n";
    output << "    Valid: " << m_validSubcodeSymbols << "\n";
    output << "    Invalid: " << m_invalidSubcodeSymbols;
    return output.str();
}
