/*
 * File:        dec_audiocorrection.cpp
 * Purpose:     efm-decoder-audio - EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_audiocorrection.h"

#include <fmt/format.h>

#include <utility>

AudioCorrection::AudioCorrection()
    : m_concealedSamplesCount(0),
      m_silencedSamplesCount(0),
      m_validSamplesCount(0) {}

void AudioCorrection::pushSection(const AudioSection& audioSection) {
  // Add the data to the input buffer
  m_inputBuffer.push_back(audioSection);

  // Process the queue
  processQueue();
}

AudioSection AudioCorrection::popSection() {
  // Move the first item out of the output buffer to avoid a deep copy.
  AudioSection result = std::move(m_outputBuffer.front());
  m_outputBuffer.pop_front();
  return result;
}

bool AudioCorrection::isReady() const {
  // Return true if the output buffer is not empty
  return !m_outputBuffer.empty();
}

void AudioCorrection::processQueue() {
  // TODO(sdi): this will never correct the very first and last sections

  // R-5: guard against an empty input buffer (defensive - the only caller
  // pushes before calling, but an empty front() would be undefined behaviour).
  if (m_inputBuffer.empty()) return;

  // Pop a section from the input buffer (move to avoid a deep copy)
  m_correctionBuffer.push_back(std::move(m_inputBuffer.front()));
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
        // If this is the first frame, use the first frame in the section as the
        // preceding frame
        precedingFrame = m_correctionBuffer.at(0).frame(97);
      } else {
        precedingFrame = m_correctionBuffer.at(1).frame(subSection - 1);
      }

      correctingFrame = m_correctionBuffer.at(1).frame(subSection);

      if (correctingFrame.countErrors() == 0) {
        // No errors in this frame - just copy it
        correctedSection.pushFrame(correctingFrame);
        m_validSamplesCount +=
            correctingFrame.frameSize();  // 6 left + 6 right mono samples
        continue;
      }

      if (subSection == 97) {
        // If this is the last frame, use the last frame in the section as the
        // following frame
        followingFrame = m_correctionBuffer.at(2).frame(0);
      } else {
        followingFrame = m_correctionBuffer.at(1).frame(subSection + 1);
      }

      // Sample correction
      std::vector<int16_t> correctedLeftSamples;
      std::vector<uint8_t> correctedLeftErrorSamples;
      std::vector<uint8_t> correctedLeftPaddedSamples;
      std::vector<int16_t> correctedRightSamples;
      std::vector<uint8_t> correctedRightErrorSamples;
      std::vector<uint8_t> correctedRightPaddedSamples;

      // P-3: each of these accessors builds a fresh 6-element vector, so hoist
      // them out of the per-sample loop (previously ~70 allocations per frame).
      const std::vector<int16_t> precLeft = precedingFrame.dataLeft();
      const std::vector<uint8_t> precLeftErr = precedingFrame.errorDataLeft();
      const std::vector<int16_t> precRight = precedingFrame.dataRight();
      const std::vector<uint8_t> precRightErr = precedingFrame.errorDataRight();
      const std::vector<int16_t> corrLeft = correctingFrame.dataLeft();
      const std::vector<uint8_t> corrLeftErr = correctingFrame.errorDataLeft();
      const std::vector<int16_t> corrRight = correctingFrame.dataRight();
      const std::vector<uint8_t> corrRightErr =
          correctingFrame.errorDataRight();
      const std::vector<int16_t> follLeft = followingFrame.dataLeft();
      const std::vector<uint8_t> follLeftErr = followingFrame.errorDataLeft();
      const std::vector<int16_t> follRight = followingFrame.dataRight();
      const std::vector<uint8_t> follRightErr = followingFrame.errorDataRight();

      for (int sampleOffset = 0; sampleOffset < 6; ++sampleOffset) {
        // Left channel
        // Get the preceding, correcting and following left samples
        int16_t precedingLeftSample, correctingLeftSample, followingLeftSample;
        int16_t precedingLeftSampleError, correctingLeftSampleError,
            followingLeftSampleError;

        if (sampleOffset == 0) {
          precedingLeftSample = precLeft.at(5);
          precedingLeftSampleError = precLeftErr.at(5);
        } else {
          precedingLeftSample = corrLeft.at(sampleOffset - 1);
          precedingLeftSampleError = corrLeftErr.at(sampleOffset - 1);
        }

        correctingLeftSample = corrLeft.at(sampleOffset);
        correctingLeftSampleError = corrLeftErr.at(sampleOffset);

        if (sampleOffset == 5) {
          followingLeftSample = follLeft.at(0);
          followingLeftSampleError = follLeftErr.at(0);
        } else {
          followingLeftSample = corrLeft.at(sampleOffset + 1);
          followingLeftSampleError = corrLeftErr.at(sampleOffset + 1);
        }

        if (correctingLeftSampleError != 0) {
          // Do we have a valid preceding and following sample?
          if (precedingLeftSampleError || followingLeftSampleError) {
            // Silence the sample
            ORC_LOG_DEBUG(
                "AudioCorrection::processQueue() -  Left  Silencing: Section "
                "address {} - Frame {}, sample {}",
                m_correctionBuffer.at(1)
                    .metadata.absoluteSectionTime()
                    .toString(),
                subSection, sampleOffset);
            correctedLeftSamples.push_back(0);
            correctedLeftErrorSamples.push_back(1);
            correctedLeftPaddedSamples.push_back(0);
            ++m_silencedSamplesCount;
          } else {
            // Conceal the sample
            ORC_LOG_DEBUG(
                "AudioCorrection::processQueue() -  Left Concealing: Section "
                "address {} - Frame {}, sample {} - Preceding = {}, Following "
                "= {}, Average = {}",
                m_correctionBuffer.at(1)
                    .metadata.absoluteSectionTime()
                    .toString(),
                subSection, sampleOffset, precedingLeftSample,
                followingLeftSample,
                (precedingLeftSample + followingLeftSample) / 2);
            correctedLeftSamples.push_back(static_cast<int16_t>(
                (precedingLeftSample + followingLeftSample) / 2));
            correctedLeftErrorSamples.push_back(0);
            correctedLeftPaddedSamples.push_back(1);
            ++m_concealedSamplesCount;
          }
        } else {
          // The sample is valid - just copy it
          correctedLeftSamples.push_back(correctingLeftSample);
          correctedLeftErrorSamples.push_back(0);
          correctedLeftPaddedSamples.push_back(0);
          ++m_validSamplesCount;
        }

        // Right channel
        // Get the preceding, correcting and following right samples
        int16_t precedingRightSample, correctingRightSample,
            followingRightSample;
        int16_t precedingRightSampleError, correctingRightSampleError,
            followingRightSampleError;

        if (sampleOffset == 0) {
          precedingRightSample = precRight.at(5);
          precedingRightSampleError = precRightErr.at(5);
        } else {
          precedingRightSample = corrRight.at(sampleOffset - 1);
          precedingRightSampleError = corrRightErr.at(sampleOffset - 1);
        }

        correctingRightSample = corrRight.at(sampleOffset);
        correctingRightSampleError = corrRightErr.at(sampleOffset);

        if (sampleOffset == 5) {
          followingRightSample = follRight.at(0);
          followingRightSampleError = follRightErr.at(0);
        } else {
          followingRightSample = corrRight.at(sampleOffset + 1);
          followingRightSampleError = corrRightErr.at(sampleOffset + 1);
        }

        if (correctingRightSampleError != 0) {
          // Do we have a valid preceding and following sample?
          if (precedingRightSampleError || followingRightSampleError) {
            // Silence the sample
            ORC_LOG_DEBUG(
                "AudioCorrection::processQueue() - Right  Silencing: Section "
                "address {} - Frame {}, sample {}",
                m_correctionBuffer.at(1)
                    .metadata.absoluteSectionTime()
                    .toString(),
                subSection, sampleOffset);
            correctedRightSamples.push_back(0);
            correctedRightErrorSamples.push_back(1);
            correctedRightPaddedSamples.push_back(0);
            ++m_silencedSamplesCount;
          } else {
            // Conceal the sample
            ORC_LOG_DEBUG(
                "AudioCorrection::processQueue() - Right Concealing: Section "
                "address {} - Frame {}, sample {} - Preceding = {}, Following "
                "= {}, Average = {}",
                m_correctionBuffer.at(1)
                    .metadata.absoluteSectionTime()
                    .toString(),
                subSection, sampleOffset, precedingRightSample,
                followingRightSample,
                (precedingRightSample + followingRightSample) / 2);
            correctedRightSamples.push_back(static_cast<int16_t>(
                (precedingRightSample + followingRightSample) / 2));
            correctedRightErrorSamples.push_back(0);
            correctedRightPaddedSamples.push_back(1);
            ++m_concealedSamplesCount;
          }
        } else {
          // The sample is valid - just copy it
          correctedRightSamples.push_back(correctingRightSample);
          correctedRightErrorSamples.push_back(0);
          correctedRightPaddedSamples.push_back(0);
          ++m_validSamplesCount;
        }
      }

      // Combine the left and right channel data (and error data)
      std::vector<int16_t> correctedSamples;
      std::vector<uint8_t> correctedErrorSamples;
      std::vector<uint8_t> correctedPaddedSamples;

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

void AudioCorrection::showStatistics() const {
  ORC_LOG_INFO("Audio correction statistics:");
  ORC_LOG_INFO(
      "  Total mono samples: {}",
      m_validSamplesCount + m_concealedSamplesCount + m_silencedSamplesCount);
  ORC_LOG_INFO("  Valid mono samples: {}", m_validSamplesCount);
  ORC_LOG_INFO("  Concealed mono samples: {}", m_concealedSamplesCount);
  ORC_LOG_INFO("  Silenced mono samples: {}", m_silencedSamplesCount);
}

void AudioCorrection::flush() {
  // Output any remaining sections in the correction buffer
  // Since we can't perform correction on the last sections (no following data),
  // we output them as-is
  while (!m_correctionBuffer.empty()) {
    m_outputBuffer.push_back(m_correctionBuffer.front());
    m_correctionBuffer.erase(m_correctionBuffer.begin());
  }
}
