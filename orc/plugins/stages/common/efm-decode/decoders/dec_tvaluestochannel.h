/*
 * File:        dec_tvaluestochannel.h
 * Purpose:     efm-decoder-f2 - EFM T-values to F2 Section decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef DEC_TVALUESTOCHANNEL_H
#define DEC_TVALUESTOCHANNEL_H

#include <cstdint>
#include <queue>
#include <vector>

#include "decoders.h"
#include "tvalues.h"

class TvaluesToChannel : public Decoder {
 public:
  TvaluesToChannel();
  void pushFrame(const std::vector<uint8_t>& data);
  std::vector<uint8_t> popFrame();
  bool isReady() const;

  void showStatistics() const;

 private:
  void processStateMachine();
  void attemptToFixOvershootFrame(std::vector<uint8_t>& frameData);
  void attemptToFixUndershootFrame(uint32_t startIndex, uint32_t endIndex,
                                   std::vector<uint8_t>& frameData);
  uint32_t countBits(const std::vector<uint8_t>& data,
                     int32_t startPosition = 0, int32_t endPosition = -1);

  // State machine states
  enum State {
    ExpectingInitialSync,
    ExpectingSync,
    HandleOvershoot,
    HandleUndershoot
  };

  // Statistics (P-10: 64-bit so cumulative T-value counters do not wrap on a
  // long capture - a 32-bit T-value count wraps after ~95 minutes of audio).
  uint64_t m_consumedTValues;
  uint64_t m_discardedTValues;
  uint64_t m_channelFrameCount;

  uint64_t m_perfectFrames;
  uint64_t m_longFrames;
  uint64_t m_shortFrames;

  uint64_t m_overshootSyncs;
  uint64_t m_undershootSyncs;
  uint64_t m_perfectSyncs;

  State m_currentState;
  std::vector<uint8_t> m_internalBuffer;
  std::vector<uint8_t> m_frameData;

  std::queue<std::vector<uint8_t>> m_inputBuffer;
  std::queue<std::vector<uint8_t>> m_outputBuffer;

  Tvalues m_tvalues;
  uint64_t m_tvalueDiscardCount;

  State expectingInitialSync();
  State expectingSync();
  State handleUndershoot();
  State handleOvershoot();
};

#endif  // DEC_TVALUESTOCHANNEL_H