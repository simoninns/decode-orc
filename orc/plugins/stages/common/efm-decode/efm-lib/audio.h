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

  // Per-sample flag marking a sample that was assembled from decoder-supplied
  // filler (CIRC warm-up or end-of-stream drain) rather than from the disc.
  // Carried through from the Data24 frame so that concealment can tell a
  // structural boundary artefact apart from a genuine unrecoverable sample.
  void setPaddedData(const std::vector<uint8_t>& paddedData);
  const std::vector<uint8_t>& paddedData() const;

  bool isFull() const;
  bool isEmpty() const;

  void showData();
  int frameSize() const;

 private:
  std::vector<int16_t> m_audioData;
  std::vector<uint8_t> m_audioErrorData;
  std::vector<uint8_t> m_audioConcealedData;
  std::vector<uint8_t> m_audioPaddedData;
};

#endif  // AUDIO_H