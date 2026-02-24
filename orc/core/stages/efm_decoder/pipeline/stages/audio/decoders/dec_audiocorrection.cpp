/*
 * File:        dec_audiocorrection.cpp
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_audiocorrection.h"
#include <fmt/format.h>
#include <sstream>

AudioCorrection::AudioCorrection() :
    m_firstSectionFlag(true),
    m_concealedSamplesCount(0),
    m_silencedSamplesCount(0),
    m_validSamplesCount(0)
{}

void AudioCorrection::pushSection(const AudioSection &audioSection)
{
    // Add the data to the input buffer
    m_inputBuffer.push_back(audioSection);

    // Process the queue
    processQueue();
}

AudioSection AudioCorrection::popSection()
{
    // Return the first item in the output buffer
    AudioSection result = m_outputBuffer.front();
    m_outputBuffer.pop_front();
    return result;
}

bool AudioCorrection::isReady() const
{
    // Return true if the output buffer is not empty
    return !m_outputBuffer.empty();
}

void AudioCorrection::processQueue()
{
    // TODO: this will never correct the very first and last sections

    // Pop a section from the input buffer
    m_correctionBuffer.push_back(m_inputBuffer.front());
    m_inputBuffer.pop_front();

    // Perform correction on the section in the middle of the correction buffer
    if (m_correctionBuffer.size() == 3) {
        AudioSection correctedSection;

        // Process all 98 frames in the section
        for (int subSection = 0; subSection < 98; ++subSection) {
            Audio correctedFrame;

            // Get the preceding, correcting and following frames
            Audio precedingFrame, correctingFrame, followingFrame;
            if (subSection == 0) {
                // If this is the first frame, use the first frame in the section as the preceding frame
                precedingFrame = m_correctionBuffer.at(0).frame(97);
            } else {
                precedingFrame = m_correctionBuffer.at(1).frame(subSection - 1);
            }

            correctingFrame = m_correctionBuffer.at(1).frame(subSection);

            if (correctingFrame.countErrors() == 0) {
                // No errors in this frame - just copy it
                correctedSection.pushFrame(correctingFrame);
                continue;
            }

            if (subSection == 97) {
                // If this is the last frame, use the last frame in the section as the following frame
                followingFrame = m_correctionBuffer.at(2).frame(0);
            } else {
                followingFrame = m_correctionBuffer.at(1).frame(subSection + 1);
            }

            // Sample correction
            std::vector<int16_t> correctedLeftSamples;
            std::vector<bool> correctedLeftErrorSamples;
            std::vector<bool> correctedLeftPaddedSamples;
            std::vector<int16_t> correctedRightSamples;
            std::vector<bool> correctedRightErrorSamples;
            std::vector<bool> correctedRightPaddedSamples;

            for (int sampleOffset = 0; sampleOffset < 6; ++sampleOffset) {
                // Left channel
                // Get the preceding, correcting and following left samples
                int16_t precedingLeftSample, correctingLeftSample, followingLeftSample;
                int16_t precedingLeftSampleError, correctingLeftSampleError, followingLeftSampleError;

                if (sampleOffset == 0) {
                    precedingLeftSample = precedingFrame.dataLeft().at(5);
                    precedingLeftSampleError = precedingFrame.errorDataLeft().at(5);
                } else {
                    precedingLeftSample = correctingFrame.dataLeft().at(sampleOffset - 1);
                    precedingLeftSampleError = correctingFrame.errorDataLeft().at(sampleOffset - 1);
                }

                correctingLeftSample = correctingFrame.dataLeft().at(sampleOffset);
                correctingLeftSampleError = correctingFrame.errorDataLeft().at(sampleOffset);

                if (sampleOffset == 5) {
                    followingLeftSample = followingFrame.dataLeft().at(0);
                    followingLeftSampleError = followingFrame.errorDataLeft().at(0);
                } else {
                    followingLeftSample = correctingFrame.dataLeft().at(sampleOffset + 1);
                    followingLeftSampleError = correctingFrame.errorDataLeft().at(sampleOffset + 1);
                }

                if (correctingLeftSampleError != 0) {
                    // Do we have a valid preceding and following sample?
                    if (precedingLeftSampleError || followingLeftSampleError) {
                        // Silence the sample
                        LOG_DEBUG("AudioCorrection::processQueue() -  Left  Silencing: Section address {} - Frame {}, sample {}",
                            m_correctionBuffer.at(1).metadata.absoluteSectionTime().toString(), subSection, sampleOffset);
                        correctedLeftSamples.push_back(0);
                        correctedLeftErrorSamples.push_back(true);
                        correctedLeftPaddedSamples.push_back(false);
                        ++m_silencedSamplesCount;
                    } else {
                        // Conceal the sample
                        LOG_DEBUG("AudioCorrection::processQueue() -  Left Concealing: Section address {} - Frame {}, sample {} - Preceding = {}, Following = {}, Average = {}",
                            m_correctionBuffer.at(1).metadata.absoluteSectionTime().toString(), subSection, sampleOffset,
                            precedingLeftSample, followingLeftSample, (precedingLeftSample + followingLeftSample) / 2);
                        correctedLeftSamples.push_back((precedingLeftSample + followingLeftSample) / 2);
                        correctedLeftErrorSamples.push_back(false);
                        correctedLeftPaddedSamples.push_back(true);
                        ++m_concealedSamplesCount;
                    }
                } else {
                    // The sample is valid - just copy it
                    correctedLeftSamples.push_back(correctingLeftSample);
                    correctedLeftErrorSamples.push_back(false);
                    correctedLeftPaddedSamples.push_back(false);
                    ++m_validSamplesCount;
                }

                // Right channel
                // Get the preceding, correcting and following right samples
                int16_t precedingRightSample, correctingRightSample, followingRightSample;
                int16_t precedingRightSampleError, correctingRightSampleError, followingRightSampleError;

                if (sampleOffset == 0) {
                    precedingRightSample = precedingFrame.dataRight().at(5);
                    precedingRightSampleError = precedingFrame.errorDataRight().at(5);
                } else {
                    precedingRightSample = correctingFrame.dataRight().at(sampleOffset - 1);
                    precedingRightSampleError = correctingFrame.errorDataRight().at(sampleOffset - 1);
                }

                correctingRightSample = correctingFrame.dataRight().at(sampleOffset);
                correctingRightSampleError = correctingFrame.errorDataRight().at(sampleOffset);

                if (sampleOffset == 5) {
                    followingRightSample = followingFrame.dataRight().at(0);
                    followingRightSampleError = followingFrame.errorDataRight().at(0);
                } else {
                    followingRightSample = correctingFrame.dataRight().at(sampleOffset + 1);
                    followingRightSampleError = correctingFrame.errorDataRight().at(sampleOffset + 1);
                }

                if (correctingRightSampleError != 0) {
                    // Do we have a valid preceding and following sample?
                    if (precedingRightSampleError || followingRightSampleError) {
                        // Silence the sample
                        LOG_DEBUG("AudioCorrection::processQueue() - Right  Silencing: Section address {} - Frame {}, sample {}",
                            m_correctionBuffer.at(1).metadata.absoluteSectionTime().toString(), subSection, sampleOffset);
                        correctedRightSamples.push_back(0);
                        correctedRightErrorSamples.push_back(true);
                        correctedRightPaddedSamples.push_back(false);
                        ++m_silencedSamplesCount;
                    } else {
                        // Conceal the sample
                        LOG_DEBUG("AudioCorrection::processQueue() - Right Concealing: Section address {} - Frame {}, sample {} - Preceding = {}, Following = {}, Average = {}",
                            m_correctionBuffer.at(1).metadata.absoluteSectionTime().toString(), subSection, sampleOffset,
                            precedingRightSample, followingRightSample, (precedingRightSample + followingRightSample) / 2);
                        correctedRightSamples.push_back((precedingRightSample + followingRightSample) / 2);
                        correctedRightErrorSamples.push_back(false);
                        correctedRightPaddedSamples.push_back(true);
                        ++m_concealedSamplesCount;
                    }
                } else {
                    // The sample is valid - just copy it
                    correctedRightSamples.push_back(correctingRightSample);
                    correctedRightErrorSamples.push_back(false);
                    correctedRightPaddedSamples.push_back(false);
                    ++m_validSamplesCount;
                }
            }

            // Combine the left and right channel data (and error data)
            std::vector<int16_t> correctedSamples;
            std::vector<bool> correctedErrorSamples;
            std::vector<bool> correctedPaddedSamples;

            for (int i = 0; i < 6; ++i) {
                correctedSamples.push_back(correctedLeftSamples.at(i));
                correctedSamples.push_back(correctedRightSamples.at(i));
                correctedErrorSamples.push_back(correctedLeftErrorSamples.at(i));
                correctedErrorSamples.push_back(correctedRightErrorSamples.at(i));
                correctedPaddedSamples.push_back(correctedLeftPaddedSamples.at(i));
                correctedPaddedSamples.push_back(correctedRightPaddedSamples.at(i));
            }

            // Write the channel data back to the correction buffer's frame
            correctedFrame.setData(correctedSamples);
            correctedFrame.setErrorData(correctedErrorSamples);
            correctedFrame.setConcealedData(correctedPaddedSamples);

            correctedSection.pushFrame(correctedFrame);
        }

        correctedSection.metadata = m_correctionBuffer.at(1).metadata;
        m_correctionBuffer[1] = correctedSection;

        // Write the first section in the correction buffer to the output buffer
        m_outputBuffer.push_back(m_correctionBuffer.at(0));
        m_correctionBuffer.erase(m_correctionBuffer.begin());
    }
}

void AudioCorrection::showStatistics() const
{
    LOG_INFO("Audio correction statistics:");
    LOG_INFO("  Total mono samples: {}", m_validSamplesCount + m_concealedSamplesCount + m_silencedSamplesCount);
    LOG_INFO("  Valid mono samples: {}", m_validSamplesCount);
    LOG_INFO("  Concealed mono samples: {}", m_concealedSamplesCount);
    LOG_INFO("  Silenced mono samples: {}", m_silencedSamplesCount);
}

std::string AudioCorrection::statisticsText() const
{
    std::ostringstream output;
    output << "Audio correction statistics:\n";
    output << "  Total mono samples: " << (m_validSamplesCount + m_concealedSamplesCount + m_silencedSamplesCount) << "\n";
    output << "  Valid mono samples: " << m_validSamplesCount << "\n";
    output << "  Concealed mono samples: " << m_concealedSamplesCount << "\n";
    output << "  Silenced mono samples: " << m_silencedSamplesCount;
    return output.str();
}

void AudioCorrection::flush()
{
    // Output any remaining sections in the correction buffer
    // Since we can't perform correction on the last sections (no following data),
    // we output them as-is
    while (!m_correctionBuffer.empty()) {
        m_outputBuffer.push_back(m_correctionBuffer.front());
        m_correctionBuffer.erase(m_correctionBuffer.begin());
    }
}
