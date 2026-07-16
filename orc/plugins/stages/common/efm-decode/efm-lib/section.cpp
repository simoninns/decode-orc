/*
 * File:        section.cpp
 * Purpose:     EFM-library - EFM Section classes
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "section.h"

#include <orc/stage/logging.h>

#include <cstdlib>

#include "efm_exception.h"

F2Section::F2Section() { m_frames.reserve(98); }

void F2Section::pushFrame(const F2Frame& inFrame) {
  if (m_frames.size() >= 98) {
    ORC_LOG_ERROR("F2Section::pushFrame - Section is full");
    throw efm::EfmDecodeError(__func__);
  }
  m_frames.push_back(inFrame);
}

const F2Frame& F2Section::frame(int32_t index) const {
  if (index >= static_cast<int32_t>(m_frames.size()) || index < 0) {
    ORC_LOG_ERROR("F2Section::frame - Index {} out of range", index);
    throw efm::EfmDecodeError(__func__);
  }
  return m_frames[index];
}

void F2Section::setFrame(int32_t index, const F2Frame& inFrame) {
  if (index >= m_frames.size() || index < 0) {
    ORC_LOG_ERROR("F2Section::setFrame - Index {} out of range", index);
    throw efm::EfmDecodeError(__func__);
  }
  m_frames[index] = inFrame;
}

bool F2Section::isComplete() const { return m_frames.size() == 98; }

void F2Section::clear() { m_frames.clear(); }

F1Section::F1Section() { m_frames.reserve(98); }

void F1Section::pushFrame(const F1Frame& inFrame) {
  if (m_frames.size() >= 98) {
    ORC_LOG_ERROR("F1Section::pushFrame - Section is full");
    throw efm::EfmDecodeError(__func__);
  }
  m_frames.push_back(inFrame);
}

const F1Frame& F1Section::frame(int32_t index) const {
  if (index >= static_cast<int32_t>(m_frames.size()) || index < 0) {
    ORC_LOG_ERROR("F1Section::frame - Index {} out of range", index);
    throw efm::EfmDecodeError(__func__);
  }
  return m_frames[index];
}

void F1Section::setFrame(int32_t index, const F1Frame& inFrame) {
  if (index >= 98 || index < 0) {
    ORC_LOG_ERROR("F1Section::setFrame - Index {} out of range", index);
    throw efm::EfmDecodeError(__func__);
  }
  m_frames[index] = inFrame;
}

bool F1Section::isComplete() const { return m_frames.size() == 98; }

void F1Section::clear() { m_frames.clear(); }

void F1Section::showData() {
  for (int32_t i = 0; i < m_frames.size(); ++i) {
    m_frames[i].showData();
  }
}

Data24Section::Data24Section() { m_frames.reserve(98); }

void Data24Section::pushFrame(const Data24& inFrame) {
  if (m_frames.size() >= 98) {
    ORC_LOG_ERROR("Data24Section::pushFrame - Section is full");
    throw efm::EfmDecodeError(__func__);
  }
  m_frames.push_back(inFrame);
}

const Data24& Data24Section::frame(int32_t index) const {
  if (index >= static_cast<int32_t>(m_frames.size()) || index < 0) {
    ORC_LOG_ERROR("Data24Section::frame - Index {} out of range", index);
    throw efm::EfmDecodeError(__func__);
  }
  return m_frames[index];
}

void Data24Section::setFrame(int32_t index, const Data24& inFrame) {
  if (index >= m_frames.size() || index < 0) {
    ORC_LOG_ERROR("Data24Section::setFrame - Index {} out of range", index);
    throw efm::EfmDecodeError(__func__);
  }
  m_frames[index] = inFrame;
}

bool Data24Section::isComplete() const { return m_frames.size() == 98; }

void Data24Section::clear() { m_frames.clear(); }

void Data24Section::showData() {
  for (int32_t i = 0; i < m_frames.size(); ++i) {
    m_frames[i].showData();
  }
}

AudioSection::AudioSection() { m_frames.reserve(98); }

void AudioSection::pushFrame(const Audio& inFrame) {
  if (m_frames.size() >= 98) {
    ORC_LOG_ERROR("AudioSection::pushFrame - Section is full");
    throw efm::EfmDecodeError(__func__);
  }
  m_frames.push_back(inFrame);
}

const Audio& AudioSection::frame(int32_t index) const {
  if (index >= static_cast<int32_t>(m_frames.size()) || index < 0) {
    ORC_LOG_ERROR("AudioSection::frame - Index {} out of range", index);
    throw efm::EfmDecodeError(__func__);
  }
  return m_frames[index];
}

void AudioSection::setFrame(int32_t index, const Audio& inFrame) {
  if (index >= m_frames.size() || index < 0) {
    ORC_LOG_ERROR("AudioSection::setFrame - Index {} out of range", index);
    throw efm::EfmDecodeError(__func__);
  }
  m_frames[index] = inFrame;
}

bool AudioSection::isComplete() const { return m_frames.size() == 98; }

void AudioSection::clear() { m_frames.clear(); }

void AudioSection::showData() {
  for (int32_t i = 0; i < m_frames.size(); ++i) {
    m_frames[i].showData();
  }
}