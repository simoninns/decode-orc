/*
 * File:        dec_tvaluestochannel.cpp
 * Module:      efm-decoder-f2
 * Purpose:     EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_tvaluestochannel.h"
#include <logging.h>
#include <queue>
#include <stdexcept>
#include <cmath>
#include <algorithm>
#include <sstream>

TvaluesToChannel::TvaluesToChannel()
{
    // Statistics
    m_consumedTValues = 0;
    m_discardedTValues = 0;
    m_channelFrameCount = 0;

    m_perfectFrames = 0;
    m_longFrames = 0;
    m_shortFrames = 0;

    m_overshootSyncs = 0;
    m_undershootSyncs = 0;
    m_perfectSyncs = 0;

    // Set the initial state
    m_currentState = ExpectingInitialSync;

    m_tvalueDiscardCount = 0;
}

void TvaluesToChannel::pushFrame(const std::vector<uint8_t> &data)
{
    // Add the data to the input buffer
    m_inputBuffer.push(data);

    // Process the state machine
    processStateMachine();
}

std::vector<uint8_t> TvaluesToChannel::popFrame()
{
    // Return the first item in the output buffer
    std::vector<uint8_t> frame = m_outputBuffer.front();
    m_outputBuffer.pop();
    return frame;
}

bool TvaluesToChannel::isReady() const
{
    // Return true if the output buffer is not empty
    return !m_outputBuffer.empty();
}

void TvaluesToChannel::processStateMachine()
{
    // Add the input data to the internal t-value buffer
    if (!m_inputBuffer.empty()) {
        std::vector<uint8_t> frameData = m_inputBuffer.front();
        m_inputBuffer.pop();
        m_internalBuffer.insert(m_internalBuffer.end(), frameData.begin(), frameData.end());
    }

    // We need 588 bits to make a frame.  Every frame starts with T11+T11.
    // So the minimum number of t-values we need is 54 and
    // the maximum number of t-values we can have is 191.  This upper limit
    // is where we need to maintain the buffer size (at 382 for 2 frames).

    while (m_internalBuffer.size() > 382) {
        switch (m_currentState) {
        case ExpectingInitialSync:
            //LOG_DEBUG("TvaluesToChannel::processStateMachine() - State: ExpectingInitialSync");
            m_currentState = expectingInitialSync();
            break;
        case ExpectingSync:
            //LOG_DEBUG("TvaluesToChannel::processStateMachine() - State: ExpectingSync");
            m_currentState = expectingSync();
            break;
        case HandleOvershoot:
            //LOG_DEBUG("TvaluesToChannel::processStateMachine() - State: HandleOvershoot");
            m_currentState = handleOvershoot();
            break;
        case HandleUndershoot:
            //LOG_DEBUG("TvaluesToChannel::processStateMachine() - State: HandleUndershoot");
            m_currentState = handleUndershoot();
            break;
        }
    }
}

TvaluesToChannel::State TvaluesToChannel::expectingInitialSync()
{
    State nextState = ExpectingInitialSync;

    // Expected sync header
    std::vector<uint8_t> t11_t11 = {0x0B, 0x0B};

    // Does the buffer contain a T11+T11 sequence?
    auto it = std::search(m_internalBuffer.begin(), m_internalBuffer.end(), t11_t11.begin(), t11_t11.end());
    int initialSyncIndex = (it != m_internalBuffer.end()) ? std::distance(m_internalBuffer.begin(), it) : -1;

    if (initialSyncIndex != -1) {
        if (m_tvalueDiscardCount > 0)
            LOG_DEBUG("TvaluesToChannel::expectingInitialSync() - Initial sync header found after {} discarded T-values", m_tvalueDiscardCount);
        else
            LOG_DEBUG("TvaluesToChannel::expectingInitialSync() - Initial sync header found");

        m_tvalueDiscardCount = 0;
        nextState = ExpectingSync;
    } else {
        // Drop all but the last T-value in the buffer
        m_tvalueDiscardCount += static_cast<int32_t>(m_internalBuffer.size()) - 1;
        m_discardedTValues += static_cast<int32_t>(m_internalBuffer.size()) - 1;
        m_internalBuffer = std::vector<uint8_t>(m_internalBuffer.end() - 1, m_internalBuffer.end());
    }

    return nextState;
}

TvaluesToChannel::State TvaluesToChannel::expectingSync()
{
    State nextState = ExpectingSync;

    // The internal buffer contains a valid sync at the start
    // Find the next sync header after it
    std::vector<uint8_t> t11_t11 = {0x0B, 0x0B};
    auto it = std::search(m_internalBuffer.begin() + 2, m_internalBuffer.end(), t11_t11.begin(), t11_t11.end());
    int syncIndex = (it != m_internalBuffer.end()) ? std::distance(m_internalBuffer.begin(), it) : -1;

    // Do we have a valid second sync header?
    if (syncIndex != -1) {
        // Extract the frame data from (and including) the first sync header until
        // (but not including) the second sync header
        std::vector<uint8_t> frameData(m_internalBuffer.begin(), m_internalBuffer.begin() + syncIndex);

        // Do we have exactly 588 bits of data?  Count the T-values
        int bitCount = countBits(frameData);

        // If the frame data is 550 to 600 bits, we have a valid frame
        if (bitCount > 550 && bitCount < 600) {
            if (bitCount != 588) {
                if (bitCount > 588) attemptToFixOvershootFrame(frameData);
                if (bitCount < 588) attemptToFixUndershootFrame(0, syncIndex, frameData);
            }

            // We have a valid frame
            // Place the frame data into the output buffer
            m_outputBuffer.push(std::move(frameData));
            
            m_consumedTValues += frameData.size();
            m_channelFrameCount++;
            m_perfectSyncs++;

            if (bitCount == 588)
                m_perfectFrames++;
            if (bitCount > 588)
                m_longFrames++;
            if (bitCount < 588)
                m_shortFrames++;

            // Remove the frame data from the internal buffer
            m_internalBuffer = std::vector<uint8_t>(m_internalBuffer.begin() + syncIndex, m_internalBuffer.end());
            nextState = ExpectingSync;
        } else {
            // This is most likely a missing sync header issue rather than
            // one or more T-values being incorrect. So we'll handle that
            // separately.
            if (bitCount > 588) {
                nextState = HandleOvershoot;
            } else {
                nextState = HandleUndershoot;
            }
        }
    } else {
        // The buffer does not contain a valid second sync header, so throw it away
        
        LOG_DEBUG("TvaluesToChannel::expectingSync() - No second sync header found, sync lost - dropping {} T-values", m_internalBuffer.size());

        m_discardedTValues += m_internalBuffer.size();
        m_internalBuffer.clear();
        nextState = ExpectingInitialSync;
    }

    return nextState;
}

TvaluesToChannel::State TvaluesToChannel::handleUndershoot()
{
    State nextState = ExpectingSync;
    const char* outcome = "unknown";
    int fttBitCount = -1;
    int sttBitCount = -1;
    int droppedTValues = 0;

    // The frame data is too short
    m_undershootSyncs++;

    // Find the second sync header
    std::vector<uint8_t> t11_t11 = {0x0B, 0x0B};
    auto it = std::search(m_internalBuffer.begin() + 2, m_internalBuffer.end(), t11_t11.begin(), t11_t11.end());
    int secondSyncIndex = (it != m_internalBuffer.end()) ? std::distance(m_internalBuffer.begin(), it) : -1;

    // Find the third sync header
    auto it3 = std::search(m_internalBuffer.begin() + secondSyncIndex + 2, m_internalBuffer.end(), t11_t11.begin(), t11_t11.end());
    int thirdSyncIndex = (it3 != m_internalBuffer.end()) ? std::distance(m_internalBuffer.begin(), it3) : -1;

    // So, unless the data is completely corrupt we should have 588 bits between
    // the first and third sync headers (i.e. the second was a corrupt sync header) or
    // 588 bits between the second and third sync headers (i.e. the first was a corrupt sync header)
    //
    // If neither of these conditions are met, we have a corrupt frame data and we have to drop it

    if (thirdSyncIndex != -1) {
        // Value of the Ts between the first and third sync header
        fttBitCount = countBits(m_internalBuffer, 0, thirdSyncIndex);

        // Value of the Ts between the second and third sync header
        sttBitCount = countBits(m_internalBuffer, secondSyncIndex, thirdSyncIndex);

        if (fttBitCount > 550 && fttBitCount < 600) {
            // Valid frame between the first and third sync headers
            std::vector<uint8_t> frameData(m_internalBuffer.begin(), m_internalBuffer.begin() + thirdSyncIndex);
            int32_t bitCount = countBits(frameData);
            if (bitCount != 588) {
                if (bitCount > 588) attemptToFixOvershootFrame(frameData);
                if (bitCount < 588) attemptToFixUndershootFrame(0, thirdSyncIndex, frameData);
            }
            m_outputBuffer.push(std::move(frameData));
            
            m_consumedTValues += frameData.size();
            m_channelFrameCount++;

            if (fttBitCount == 588)
                m_perfectFrames++;
            if (fttBitCount > 588)
                m_longFrames++;
            if (fttBitCount < 588)
                m_shortFrames++;

            // Remove the frame data from the internal buffer
            m_internalBuffer = std::vector<uint8_t>(m_internalBuffer.begin() + thirdSyncIndex, m_internalBuffer.end());
            nextState = ExpectingSync;
            outcome = "recovered-first-to-third";
        } else if (sttBitCount > 550 && sttBitCount < 600) {
            // Valid frame between the second and third sync headers
            std::vector<uint8_t> frameData(m_internalBuffer.begin() + secondSyncIndex, m_internalBuffer.begin() + thirdSyncIndex);
            int32_t bitCount = countBits(frameData);
            if (bitCount != 588) {
                if (bitCount > 588) attemptToFixOvershootFrame(frameData);
                if (bitCount < 588) attemptToFixUndershootFrame(secondSyncIndex, thirdSyncIndex, frameData);
            }
            m_outputBuffer.push(frameData);

            m_consumedTValues += frameData.size();
            m_channelFrameCount++;

            if (sttBitCount == 588)
                m_perfectFrames++;
            if (sttBitCount > 588)
                m_longFrames++;
            if (sttBitCount < 588)
                m_shortFrames++;

            // Remove the frame data from the internal buffer
            m_discardedTValues += secondSyncIndex;
            droppedTValues = secondSyncIndex;
            m_internalBuffer = std::vector<uint8_t>(m_internalBuffer.begin() + thirdSyncIndex, m_internalBuffer.end());
            nextState = ExpectingSync;
            outcome = "recovered-second-to-third";
        } else {
            nextState = ExpectingSync;

            // Remove the frame data from the internal buffer
            m_discardedTValues += secondSyncIndex;
            droppedTValues = secondSyncIndex;
            m_internalBuffer = std::vector<uint8_t>(m_internalBuffer.begin() + thirdSyncIndex, m_internalBuffer.end());
            outcome = "dropped-candidate";
        }
    } else {
        if (m_internalBuffer.size() <= 382) {
            nextState = HandleUndershoot;
            outcome = "waiting-for-more-data";
        } else {
            droppedTValues = static_cast<int>(m_internalBuffer.size() - 1);
            m_discardedTValues += m_internalBuffer.size() - 1;
            m_internalBuffer = std::vector<uint8_t>(m_internalBuffer.end() - 1, m_internalBuffer.end());
            nextState = ExpectingInitialSync;
            outcome = "sync-lost";
        }
    }

    LOG_DEBUG("TvaluesToChannel::handleUndershoot() - {} (first->third bits: {}, second->third bits: {}, dropped T-values: {}, next state: {})",
              outcome,
              fttBitCount,
              sttBitCount,
              droppedTValues,
              static_cast<int>(nextState));

    return nextState;
}

TvaluesToChannel::State TvaluesToChannel::handleOvershoot()
{
    State nextState = ExpectingSync;
    uint32_t splitFrameCount = 0;
    uint32_t splitMinBits = 0;
    uint32_t splitMaxBits = 0;

    // The frame data is too long
    m_overshootSyncs++;

    // Is the overshoot due to a missing/corrupt sync header?
    // Count the bits between the first and second sync headers, if they are 588*2, split
    // the frame data into two frames
    std::vector<uint8_t> t11_t11 = {0x0B, 0x0B};

    // Find the second sync header
    auto it = std::search(m_internalBuffer.begin() + 2, m_internalBuffer.end(), t11_t11.begin(), t11_t11.end());
    int syncIndex = (it != m_internalBuffer.end()) ? std::distance(m_internalBuffer.begin(), it) : -1;

    // Do we have a valid second sync header?
    if (syncIndex != -1) {
        // Extract the frame data from (and including) the first sync header until
        // (but not including) the second sync header
        std::vector<uint8_t> frameData(m_internalBuffer.begin(), m_internalBuffer.begin() + syncIndex);

        // Remove the frame data from the internal buffer
        m_internalBuffer = std::vector<uint8_t>(m_internalBuffer.begin() + syncIndex, m_internalBuffer.end());

        // How many bits of data do we have?  Count the T-values
        int bitCount = countBits(frameData);

        // If the frame data is within the range of n frames, we have n frames
        // separated by corrupt sync headers
        const int frameSize = 588;
        const int tolerance = 11; // How close to 588 bits do we need to be?
        const int maxFrames = 10; // Define the maximum number of frames to check for
        bool validFrames = false;

        for (int n = 2; n <= maxFrames; ++n) {
            if (bitCount > frameSize * n - tolerance && bitCount < frameSize * n + tolerance) {
                validFrames = true;
                size_t frameStartIndex = 0;

                for (int i = 0; i < n; ++i) {
                    int accumulatedBits = 0;
                    size_t frameEndIndex = frameStartIndex;
                    while (accumulatedBits < frameSize && frameEndIndex < frameData.size()) {
                        accumulatedBits += frameData.at(frameEndIndex);
                        ++frameEndIndex;
                    }

                    const size_t frameLength = frameEndIndex - frameStartIndex;

                    std::vector<uint8_t> singleFrameData(frameData.begin() + static_cast<std::ptrdiff_t>(frameStartIndex),
                                                         frameData.begin() + static_cast<std::ptrdiff_t>(frameEndIndex));
                    frameStartIndex = frameEndIndex;

                    uint32_t singleFrameBitCount = countBits(singleFrameData);

                    // Place the frame into the output buffer
                    m_outputBuffer.push(std::move(singleFrameData));

                    if (splitFrameCount == 0) {
                        splitMinBits = singleFrameBitCount;
                        splitMaxBits = singleFrameBitCount;
                    } else {
                        splitMinBits = std::min(splitMinBits, singleFrameBitCount);
                        splitMaxBits = std::max(splitMaxBits, singleFrameBitCount);
                    }
                    splitFrameCount++;

                    m_consumedTValues += static_cast<uint32_t>(frameLength);
                    m_channelFrameCount++;

                    if (singleFrameBitCount == frameSize)
                    m_perfectFrames++;
                    if (singleFrameBitCount < frameSize)
                    m_longFrames++;
                    if (singleFrameBitCount > frameSize)
                    m_shortFrames++;
                }
                break;
            }
        }

        if (!validFrames) {
            const int droppedTValues = static_cast<int>(m_internalBuffer.size()) - 1;
            LOG_DEBUG("TvaluesToChannel::handleOvershoot() - unrecoverable overshoot (bits: {}, dropped T-values: {}, next state: {})",
                      bitCount,
                      droppedTValues,
                      static_cast<int>(ExpectingInitialSync));
            m_discardedTValues += static_cast<int32_t>(m_internalBuffer.size()) - 1;
            m_internalBuffer = std::vector<uint8_t>(m_internalBuffer.end() - 1, m_internalBuffer.end());
            nextState = ExpectingInitialSync;
        } else {
            LOG_DEBUG("TvaluesToChannel::handleOvershoot() - recovered by split (bits: {}, split frames: {}, split bit range: {}-{}, next state: {})",
                      bitCount,
                      splitFrameCount,
                      splitMinBits,
                      splitMaxBits,
                      static_cast<int>(ExpectingSync));
            nextState = ExpectingSync;
        }
    } else {
        throw std::runtime_error("TvaluesToChannel::handleOvershoot() - Overshoot frame detected but no second sync header found, even though it should have been there.");
    }

    return nextState;
}

// This function tries some basic tricks to fix a frame that is more than 588 bits long
void TvaluesToChannel::attemptToFixOvershootFrame(std::vector<uint8_t> &frameData)
{
    int32_t bitCount = countBits(frameData);

    if (bitCount > 588) {
        // We have too many bits, so we'll try to remove some
        // We'll remove the first T-value in the frame
        std::vector<uint8_t> lframeData(frameData.begin(), frameData.end() - 1);
        // ... and the last T-value in the frame
        std::vector<uint8_t> rframeData(frameData.begin() + 1, frameData.end());
        int32_t lbitCount = countBits(lframeData);
        int32_t rbitCount = countBits(rframeData);

        if (lbitCount == 588) {
            frameData = lframeData;
            LOG_DEBUG("TvaluesToChannel::attemptToFixOvershootFrame() - Removed first T-value to fix frame");
        } else if (rbitCount == 588) {
            frameData = rframeData;
            LOG_DEBUG("TvaluesToChannel::attemptToFixOvershootFrame() - Removed last T-value to fix frame");
        }
    }
}

// This function tries some basic tricks to fix a frame that is less than 588 bits long
// Note: the start and end indexes refer to m_internalBuffer
void TvaluesToChannel::attemptToFixUndershootFrame(uint32_t startIndex, uint32_t endIndex, std::vector<uint8_t> &frameData)
{
    int32_t bitCount = countBits(frameData);

    if (bitCount < 588) {
        std::vector<uint8_t> lframeData(m_internalBuffer.begin() + startIndex, m_internalBuffer.begin() + endIndex + 1);     
        int32_t lbitCount = countBits(lframeData);

        if (lbitCount == 588) {
            frameData = lframeData;
            LOG_DEBUG("TvaluesToChannel::attemptToFixUndershootFrame() - Added additional last T-value to fix frame");
            return;
        }

        if (startIndex > 0) {
            std::vector<uint8_t> rframeData(m_internalBuffer.begin() + startIndex - 1, m_internalBuffer.begin() + endIndex);          
            int32_t rbitCount = countBits(rframeData);

            if (rbitCount == 588) {
                frameData = rframeData;
                LOG_DEBUG("TvaluesToChannel::attemptToFixUndershootFrame() - Added additional first T-value to fix frame");
            }
        }
    }
}

// Count the number of bits in the array of T-values
uint32_t TvaluesToChannel::countBits(const std::vector<uint8_t> &data, int32_t startPosition, int32_t endPosition)
{
    if (endPosition == -1)
        endPosition = static_cast<int32_t>(data.size());

    uint32_t bitCount = 0;
    for (int i = startPosition; i < endPosition; i++) {
        bitCount += data.at(i);
    }
    return bitCount;
}

void TvaluesToChannel::showStatistics() const
{
    LOG_INFO("T-values to Channel Frame statistics:");
    LOG_INFO("  T-Values:");
    LOG_INFO("    Consumed: {}", m_consumedTValues);
    LOG_INFO("    Discarded: {}", m_discardedTValues);
    LOG_INFO("  Channel frames:");
    LOG_INFO("    Total: {}", m_channelFrameCount);
    LOG_INFO("    588 bits: {}", m_perfectFrames);
    LOG_INFO("    >588 bits: {}", m_longFrames);
    LOG_INFO("    <588 bits: {}", m_shortFrames);
    LOG_INFO("  Sync headers:");
    LOG_INFO("    Good syncs: {}", m_perfectSyncs);
    LOG_INFO("    Overshoots: {}", m_overshootSyncs);
    LOG_INFO("    Undershoots: {}", m_undershootSyncs);

    // When we overshoot and split the frame, we are guessing the sync header...
    LOG_INFO("    Guessed: {}", m_channelFrameCount - m_perfectSyncs - m_overshootSyncs - m_undershootSyncs);
}

std::string TvaluesToChannel::statisticsText() const
{
    std::ostringstream output;
    output << "T-values to Channel Frame statistics:\n";
    output << "  T-Values:\n";
    output << "    Consumed: " << m_consumedTValues << "\n";
    output << "    Discarded: " << m_discardedTValues << "\n";
    output << "  Channel frames:\n";
    output << "    Total: " << m_channelFrameCount << "\n";
    output << "    588 bits: " << m_perfectFrames << "\n";
    output << "    >588 bits: " << m_longFrames << "\n";
    output << "    <588 bits: " << m_shortFrames << "\n";
    output << "  Sync headers:\n";
    output << "    Good syncs: " << m_perfectSyncs << "\n";
    output << "    Overshoots: " << m_overshootSyncs << "\n";
    output << "    Undershoots: " << m_undershootSyncs << "\n";
    output << "    Guessed: "
           << (m_channelFrameCount - m_perfectSyncs - m_overshootSyncs - m_undershootSyncs);
    return output.str();
}
