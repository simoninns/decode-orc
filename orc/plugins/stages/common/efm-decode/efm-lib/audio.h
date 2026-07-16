/*
 * File:        audio.h
 * Purpose:     EFM-library - Audio frame type class
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef AUDIO_H
#define AUDIO_H

#include <cstdint>
#include <vector>

// Audio class
class Audio {
 public:
  // The sample and flag vectors are stored interleaved as L,R pairs: index the
  // returned vector as data[2*i] (left) and data[2*i+1] (right). Empty frames
  // return a shared zero-filled 12-element vector so callers can index it
  // without a per-call allocation (P-3).
  void setData(const std::vector<int16_t>& data);
  const std::vector<int16_t>& data() const;
  void setErrorData(const std::vector<uint8_t>& errorData);
  const std::vector<uint8_t>& errorData() const;
  uint32_t countErrors() const;
  uint32_t countErrorsLeft() const;
  uint32_t countErrorsRight() const;

  void setConcealedData(const std::vector<uint8_t>& paddingData);
  const std::vector<uint8_t>& concealedData() const;

  bool isFull() const;
  bool isEmpty() const;

  void showData();
  int frameSize() const;

 private:
  std::vector<int16_t> m_audioData;
  std::vector<uint8_t> m_audioErrorData;
  std::vector<uint8_t> m_audioConcealedData;
};

#endif  // AUDIO_H