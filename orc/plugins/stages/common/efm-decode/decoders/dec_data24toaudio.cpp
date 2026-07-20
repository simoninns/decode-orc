/*
 * File:        dec_data24toaudio.cpp
 * Purpose:     efm-decoder-audio - EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_data24toaudio.h"

#include "../efm-lib/efm_constants.h"
#include "../efm-lib/efm_exception.h"

Data24ToAudio::Data24ToAudio()
    : m_invalidData24FramesCount(0),
      m_validData24FramesCount(0),
      m_invalidSamplesCount(0),
      m_validSamplesCount(0),
      m_invalidByteCount(0),
      // C-1: this is a min-finder seed, so it must be the maximum representable
      // time (IEC 60908 §17.5.1 ceiling 99:59:74). A 59:59:74 seed never
      // updates for a disc whose earliest section lies past 60 min, corrupting
      // the reported start/total time.
      m_startTime(SectionTime(99, 59, 74)),
      m_endTime(SectionTime(0, 0, 0)) {}

void Data24ToAudio::pushSection(const Data24Section& data24Section) {
  // Add the data to the input buffer
  m_inputBuffer.push_back(data24Section);

  // Process the queue
  processQueue();
}

void Data24ToAudio::pushSection(Data24Section&& data24Section) {
  // Move the data into the input buffer to avoid a deep copy.
  m_inputBuffer.push_back(std::move(data24Section));

  // Process the queue
  processQueue();
}

AudioSection Data24ToAudio::popSection() {
  // Move the first item out of the output buffer to avoid a deep copy.
  AudioSection result = std::move(m_outputBuffer.front());
  m_outputBuffer.pop_front();
  return result;
}

bool Data24ToAudio::isReady() const {
  // Return true if the output buffer is not empty
  return !m_outputBuffer.empty();
}

void Data24ToAudio::processQueue() {
  // Process the input buffer
  while (!m_inputBuffer.empty()) {
    Data24Section data24Section = m_inputBuffer.front();
    m_inputBuffer.pop_front();
    AudioSection audioSection;

    // Sanity check the Data24 section
    if (!data24Section.isComplete()) {
      ORC_LOG_CRITICAL(
          "Data24ToAudio::processQueue - Data24 Section is not complete");
      throw efm::EfmDecodeError(__func__);
    }

    for (int index = 0; index < efm::kFramesPerSection; ++index) {
      const Data24& d24Frame = data24Section.frame(index);
      std::vector<uint8_t> data24Data = d24Frame.data();
      std::vector<uint8_t> data24ErrorData = d24Frame.errorData();
      std::vector<uint8_t> data24PaddedData = d24Frame.paddedData();

      if (d24Frame.countErrors() != 0) {
        ++m_invalidData24FramesCount;
      } else {
        ++m_validData24FramesCount;
      }

      // Convert the 24 bytes of data into 12 16-bit audio samples
      std::vector<int16_t> audioData;
      std::vector<uint8_t> audioErrorData;
      std::vector<uint8_t> audioConcealedData;
      std::vector<uint8_t> audioPaddedData;
      for (int i = 0; i < 24; i += 2) {
        int16_t sample =
            static_cast<int16_t>(static_cast<uint16_t>(data24Data[i + 1] << 8) |
                                 static_cast<uint16_t>(data24Data[i]));

        if (data24ErrorData[i]) m_invalidByteCount++;
        if (data24ErrorData[i + 1]) m_invalidByteCount++;

        // A sample is structural filler if either of its bytes came from the
        // CIRC warm-up fill or the end-of-stream drain.
        audioPaddedData.push_back(
            (data24PaddedData[i] || data24PaddedData[i + 1]) ? 1 : 0);

        // Set an error flag if either byte of the sample is an error
        if (data24ErrorData[i + 1] || data24ErrorData[i]) {
          // Error in the sample
          audioData.push_back(sample);
          audioErrorData.push_back(1);
          audioConcealedData.push_back(0);
          ++m_invalidSamplesCount;
        } else {
          // No error in the sample
          audioData.push_back(sample);
          audioErrorData.push_back(0);
          audioConcealedData.push_back(0);
          ++m_validSamplesCount;
        }
      }

      // Put the resulting data into an Audio frame and push it to the output
      // buffer
      Audio audio;
      audio.setData(audioData);
      audio.setErrorData(audioErrorData);
      audio.setConcealedData(audioConcealedData);
      audio.setPaddedData(audioPaddedData);

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

void Data24ToAudio::showStatistics() const {
  ORC_LOG_INFO("Data24 to Audio statistics:");
  ORC_LOG_INFO("  Data24 Frames:");
  ORC_LOG_INFO("    Total Frames: {}",
               m_validData24FramesCount + m_invalidData24FramesCount);
  ORC_LOG_INFO("    Valid Frames: {}", m_validData24FramesCount);
  ORC_LOG_INFO("    Invalid Frames: {}", m_invalidData24FramesCount);
  ORC_LOG_INFO("    Invalid Bytes: {}", m_invalidByteCount);

  ORC_LOG_INFO("  Audio Samples:");
  ORC_LOG_INFO("    Total samples: {}",
               m_validSamplesCount + m_invalidSamplesCount);
  ORC_LOG_INFO("    Valid samples: {}", m_validSamplesCount);
  ORC_LOG_INFO("    Invalid samples: {}", m_invalidSamplesCount);

  ORC_LOG_INFO("  Section time information:");
  ORC_LOG_INFO("    Start time: {}", m_startTime.toString());
  ORC_LOG_INFO("    End time: {}", m_endTime.toString());
  ORC_LOG_INFO("    Total time: {}", (m_endTime - m_startTime).toString());
}