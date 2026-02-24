/*
 * File:        dec_data24toaudio.cpp
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_data24toaudio.h"

Data24ToAudio::Data24ToAudio() :
    m_invalidData24FramesCount(0),
    m_validData24FramesCount(0),
    m_invalidSamplesCount(0),
    m_validSamplesCount(0),
    m_invalidByteCount(0),
    m_startTime(SectionTime(59, 59, 74)),
    m_endTime(SectionTime(0, 0, 0))
{}

void Data24ToAudio::pushSection(const Data24Section &data24Section)
{
    // Add the data to the input buffer
    m_inputBuffer.push_back(data24Section);

    // Process the queue
    processQueue();
}

AudioSection Data24ToAudio::popSection()
{
    // Return the first item in the output buffer
    AudioSection result = m_outputBuffer.front();
    m_outputBuffer.pop_front();
    return result;
}

bool Data24ToAudio::isReady() const
{
    // Return true if the output buffer is not empty
    return !m_outputBuffer.empty();
}

void Data24ToAudio::processQueue()
{
    // Process the input buffer
    while (!m_inputBuffer.empty()) {
        Data24Section data24Section = m_inputBuffer.front();
        m_inputBuffer.pop_front();
        AudioSection audioSection;

        // Sanity check the Data24 section
        if (!data24Section.isComplete()) {
            LOG_CRITICAL("Data24ToAudio::processQueue - Data24 Section is not complete");
            std::exit(1);
        }

        for (int index = 0; index < 98; ++index) {
            std::vector<uint8_t> data24Data = data24Section.frame(index).data();
            std::vector<bool> data24ErrorData = data24Section.frame(index).errorData();

            if (data24Section.frame(index).countErrors() != 0) {
                ++m_invalidData24FramesCount;
            } else {
                ++m_validData24FramesCount;
            }

            // Convert the 24 bytes of data into 12 16-bit audio samples
            std::vector<int16_t> audioData;
            std::vector<bool> audioErrorData;
            std::vector<bool> audioConcealedData;
            for (int i = 0; i < 24; i += 2) {
                int16_t sample = static_cast<int16_t>(static_cast<uint16_t>(data24Data[i + 1] << 8) | static_cast<uint16_t>(data24Data[i]));

                if (data24ErrorData[i]) m_invalidByteCount++;
                if (data24ErrorData[i + 1]) m_invalidByteCount++;  

                // Set an error flag if either byte of the sample is an error
                if (data24ErrorData[i + 1] || data24ErrorData[i]) {
                    // Error in the sample
                    audioData.push_back(sample);
                    audioErrorData.push_back(true);
                    audioConcealedData.push_back(false);
                    ++m_invalidSamplesCount;
                } else {
                    // No error in the sample
                    audioData.push_back(sample);
                    audioErrorData.push_back(false);
                    audioConcealedData.push_back(false);
                    ++m_validSamplesCount;
                }
            }

            // Put the resulting data into an Audio frame and push it to the output buffer
            Audio audio;
            audio.setData(audioData);
            audio.setErrorData(audioErrorData);
            audio.setConcealedData(audioConcealedData);

            audioSection.pushFrame(audio);
        }

        audioSection.metadata = data24Section.metadata;

        if (audioSection.metadata.absoluteSectionTime() < m_startTime) {
            m_startTime = audioSection.metadata.absoluteSectionTime();
        }

        if (audioSection.metadata.absoluteSectionTime() >= m_endTime) {
            m_endTime = audioSection.metadata.absoluteSectionTime();
        }

        // Add the section to the output buffer
        m_outputBuffer.push_back(audioSection);
    }
}

void Data24ToAudio::showStatistics() const
{
    LOG_INFO("Data24 to Audio statistics:");
    LOG_INFO("  Data24 Frames:");
    LOG_INFO("    Total Frames: {}", m_validData24FramesCount + m_invalidData24FramesCount);
    LOG_INFO("    Valid Frames: {}", m_validData24FramesCount);
    LOG_INFO("    Invalid Frames: {}", m_invalidData24FramesCount);
    LOG_INFO("    Invalid Bytes: {}", m_invalidByteCount);

    LOG_INFO("  Audio Samples:");
    LOG_INFO("    Total samples: {}", m_validSamplesCount + m_invalidSamplesCount);
    LOG_INFO("    Valid samples: {}", m_validSamplesCount);
    LOG_INFO("    Invalid samples: {}", m_invalidSamplesCount);

    LOG_INFO("  Section time information:");
    LOG_INFO("    Start time: {}", m_startTime.toString());
    LOG_INFO("    End time: {}", m_endTime.toString());
    LOG_INFO("    Total time: {}", (m_endTime - m_startTime).toString());
}