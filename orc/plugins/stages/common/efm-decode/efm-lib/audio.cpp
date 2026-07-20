/*
 * File:        audio.cpp
 * Purpose:     EFM-library - Audio frame type class
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "audio.h"

#include <orc/support/logging.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "efm_exception.h"

// Audio class
// Set the data for the audio, ensuring it matches the frame size
void Audio::setData(const std::vector<int16_t>& data) {
  if (data.size() != static_cast<size_t>(frameSize())) {
    ORC_LOG_ERROR(
        "Audio::setData(): Data size of {} does not match frame size of {}",
        data.size(), frameSize());
    throw efm::EfmDecodeError(__func__);
  }
  m_audioData = data;
}

// Get the interleaved L,R data for the audio, returning a shared zero-filled
// vector if empty.
const std::vector<int16_t>& Audio::data() const {
  static const std::vector<int16_t> emptyData(12, 0);
  if (m_audioData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::data(): Frame is empty, returning zero-filled vector");
    return emptyData;
  }
  return m_audioData;
}

// Set the error data for the audio, ensuring it matches the frame size
void Audio::setErrorData(const std::vector<uint8_t>& errorData) {
  if (errorData.size() != static_cast<size_t>(frameSize())) {
    ORC_LOG_ERROR(
        "Audio::setErrorData(): Error data size of {} does not match frame "
        "size of {}",
        errorData.size(), frameSize());
    throw efm::EfmDecodeError(__func__);
  }
  m_audioErrorData = errorData;
}

// Get the interleaved L,R error data for the audio, returning a shared
// zero-filled vector if empty.
const std::vector<uint8_t>& Audio::errorData() const {
  static const std::vector<uint8_t> emptyError(12, 0);
  if (m_audioErrorData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::errorData(): Error frame is empty, returning zero-filled "
        "vector");
    return emptyError;
  }
  return m_audioErrorData;
}

// Count the number of errors in the audio. R-5: a default-constructed Audio has
// an empty error vector; guard against indexing it out of bounds.
uint32_t Audio::countErrors() const {
  if (m_audioErrorData.empty()) return 0;
  uint32_t errorCount = 0;
  for (int i = 0; i < frameSize(); ++i) {
    if (m_audioErrorData[i]) errorCount++;
  }
  return errorCount;
}

// Count the number of errors in the left channel of the audio
uint32_t Audio::countErrorsLeft() const {
  if (m_audioErrorData.empty()) return 0;
  uint32_t errorCount = 0;
  for (int i = 0; i < frameSize(); i += 2) {
    if (m_audioErrorData[i]) errorCount++;
  }
  return errorCount;
}

// Count the number of errors in the right channel of the audio
uint32_t Audio::countErrorsRight() const {
  if (m_audioErrorData.empty()) return 0;
  uint32_t errorCount = 0;
  for (int i = 1; i < frameSize(); i += 2) {
    if (m_audioErrorData[i]) errorCount++;
  }
  return errorCount;
}

// Check if the audio is full (i.e., has data)
bool Audio::isFull() const { return !isEmpty(); }

// Check if the audio is empty (i.e., has no data)
bool Audio::isEmpty() const { return m_audioData.empty(); }

// Show the audio data and errors in debug
void Audio::showData() {
  if (!orc::get_logger()->should_log(spdlog::level::debug)) return;
  // R-5: guard against an error vector shorter than the data vector (e.g. a
  // frame with data set but no error data).
  const bool haveErrors = m_audioErrorData.size() == m_audioData.size();
  std::string dataString;
  for (int i = 0; i < static_cast<int>(m_audioData.size()); ++i) {
    if (!haveErrors || m_audioErrorData[i] == false) {
      char buf[10];
      snprintf(buf, sizeof(buf), "%c%04x ", m_audioData[i] < 0 ? '-' : '+',
               static_cast<unsigned>(std::abs(m_audioData[i])));
      dataString += buf;
    } else {
      dataString += "XXXXX ";
    }
  }

  // Convert to uppercase for display
  for (char& c : dataString) {
    if (c >= 'a' && c <= 'f') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }

  ORC_LOG_DEBUG("{}", dataString);
}

int Audio::frameSize() const { return 12; }

void Audio::setConcealedData(const std::vector<uint8_t>& concealedData) {
  if (static_cast<int>(concealedData.size()) != frameSize()) {
    ORC_LOG_ERROR(
        "Audio::setConcealedData(): Concealed data size of {} does not match "
        "frame size of {}",
        concealedData.size(), frameSize());
    throw efm::EfmDecodeError(__func__);
  }
  m_audioConcealedData = concealedData;
}

const std::vector<uint8_t>& Audio::concealedData() const {
  static const std::vector<uint8_t> emptyConcealed(12, 0);
  if (m_audioConcealedData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::concealedData(): Concealed data is empty, returning "
        "zero-filled vector");
    return emptyConcealed;
  }
  return m_audioConcealedData;
}

void Audio::setPaddedData(const std::vector<uint8_t>& paddedData) {
  if (static_cast<int>(paddedData.size()) != frameSize()) {
    ORC_LOG_ERROR(
        "Audio::setPaddedData(): Padded data size of {} does not match frame "
        "size of {}",
        paddedData.size(), frameSize());
    throw efm::EfmDecodeError(__func__);
  }
  m_audioPaddedData = paddedData;
}

const std::vector<uint8_t>& Audio::paddedData() const {
  static const std::vector<uint8_t> emptyPadded(12, 0);
  if (m_audioPaddedData.empty()) {
    return emptyPadded;
  }
  return m_audioPaddedData;
}
