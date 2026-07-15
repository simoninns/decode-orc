/*
 * File:        audio.cpp
 * Purpose:     EFM-library - Audio frame type class
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "audio.h"

#include <orc/stage/logging.h>

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

// Get the data for the audio, returning a zero-filled vector if empty
std::vector<int16_t> Audio::data() const {
  if (m_audioData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::data(): Frame is empty, returning zero-filled vector");
    return std::vector<int16_t>(frameSize(), 0);
  }
  return m_audioData;
}

// Get the left channel data for the audio, returning a zero-filled vector if
// empty
std::vector<int16_t> Audio::dataLeft() const {
  if (m_audioData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::dataLeft(): Frame is empty, returning zero-filled vector");
    return std::vector<int16_t>(frameSize(), 0);
  }

  std::vector<int16_t> dataLeft;
  for (int i = 0; i < frameSize(); i += 2) {
    dataLeft.push_back(m_audioData[i]);
  }
  return dataLeft;
}

// Get the right channel data for the audio, returning a zero-filled vector if
// empty
std::vector<int16_t> Audio::dataRight() const {
  if (m_audioData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::dataRight(): Frame is empty, returning zero-filled vector");
    return std::vector<int16_t>(frameSize(), 0);
  }

  std::vector<int16_t> dataRight;
  for (int i = 1; i < frameSize(); i += 2) {
    dataRight.push_back(m_audioData[i]);
  }
  return dataRight;
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

// Get the error_data for the audio, returning a zero-filled vector if empty
std::vector<uint8_t> Audio::errorData() const {
  if (m_audioErrorData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::errorData(): Error frame is empty, returning zero-filled "
        "vector");
    return std::vector<uint8_t>(frameSize(), 0);
  }
  return m_audioErrorData;
}

// Get the left channel error data for the audio, returning a zero-filled vector
// if empty
std::vector<uint8_t> Audio::errorDataLeft() const {
  if (m_audioErrorData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::errorDataLeft(): Error frame is empty, returning zero-filled "
        "vector");
    return std::vector<uint8_t>(frameSize(), 0);
  }

  std::vector<uint8_t> errorDataLeft;
  for (int i = 0; i < frameSize(); i += 2) {
    errorDataLeft.push_back(m_audioErrorData[i]);
  }
  return errorDataLeft;
}

// Get the right channel error data for the audio, returning a zero-filled
// vector if empty
std::vector<uint8_t> Audio::errorDataRight() const {
  if (m_audioErrorData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::errorDataRight(): Error frame is empty, returning zero-filled "
        "vector");
    return std::vector<uint8_t>(frameSize(), 0);
  }

  std::vector<uint8_t> errorDataRight;
  for (int i = 1; i < frameSize(); i += 2) {
    errorDataRight.push_back(m_audioErrorData[i]);
  }
  return errorDataRight;
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

std::vector<uint8_t> Audio::concealedData() const {
  if (m_audioConcealedData.empty()) {
    ORC_LOG_DEBUG(
        "Audio::concealedData(): Concealed data is empty, returning "
        "zero-filled vector");
    return std::vector<uint8_t>(frameSize(), 0);
  }
  return m_audioConcealedData;
}
