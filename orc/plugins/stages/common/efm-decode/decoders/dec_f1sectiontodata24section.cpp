/*
 * File:        dec_f1sectiontodata24section.cpp
 * Purpose:     ld-efm-decoder - EFM data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_f1sectiontodata24section.h"

#include <cstdlib>
#include <utility>

#include "../efm-lib/efm_constants.h"
#include "../efm-lib/efm_exception.h"

F1SectionToData24Section::F1SectionToData24Section()
    : m_invalidF1FramesCount(0),
      m_validF1FramesCount(0),
      m_corruptBytesCount(0),
      m_paddedBytesCount(0),
      m_unpaddedF1FramesCount(0),
      m_paddedF1FramesCount(0) {}

void F1SectionToData24Section::pushSection(const F1Section& f1Section) {
  // Add the data to the input buffer
  m_inputBuffer.push_back(f1Section);

  // Process the queue
  processQueue();
}

void F1SectionToData24Section::pushSection(F1Section&& f1Section) {
  // Move the data into the input buffer to avoid a deep copy.
  m_inputBuffer.push_back(std::move(f1Section));

  // Process the queue
  processQueue();
}

Data24Section F1SectionToData24Section::popSection() {
  // Move the first item out of the output buffer to avoid a deep copy.
  Data24Section section = std::move(m_outputBuffer.front());
  m_outputBuffer.pop_front();
  return section;
}

bool F1SectionToData24Section::isReady() const {
  // Return true if the output buffer is not empty
  return !m_outputBuffer.empty();
}

void F1SectionToData24Section::processQueue() {
  // Process the input buffer
  while (!m_inputBuffer.empty()) {
    F1Section f1Section = m_inputBuffer.front();
    m_inputBuffer.pop_front();
    Data24Section data24Section;

    // Sanity check the F1 section
    if (!f1Section.isComplete()) {
      ORC_LOG_CRITICAL(
          "F1SectionToData24Section::processQueue - F1 Section is not "
          "complete");
      throw efm::EfmDecodeError(__func__);
    }

    for (int index = 0; index < efm::kFramesPerSection; ++index) {
      const F1Frame& f1Frame = f1Section.frame(index);
      std::vector<uint8_t> data = f1Frame.data();
      std::vector<uint8_t> errorData = f1Frame.errorData();
      std::vector<uint8_t> paddedData = f1Frame.paddedData();

      // ECMA-130 issue 2 page 16 - Clause 16
      // All byte pairs are swapped by the F1 Frame encoder
      if (data.size() == errorData.size()) {
        for (size_t i = 0; i + 1 < data.size(); i += 2) {
          std::swap(data[i], data[i + 1]);
          std::swap(errorData[i], errorData[i + 1]);
          std::swap(paddedData[i], paddedData[i + 1]);
        }
      } else {
        ORC_LOG_CRITICAL("Data and error data size mismatch in F1 frame {}",
                         index);
        throw efm::EfmDecodeError(__func__);
      }

      // Check the error data (and count any flagged errors)
      uint32_t errorCount = f1Frame.countErrors();

      m_corruptBytesCount += errorCount;

      if (errorCount > 0) {
        ++m_invalidF1FramesCount;
      } else {
        ++m_validF1FramesCount;
      }

      // Check the error data (and count any flagged padding)
      uint32_t paddingCount = f1Frame.countPadded();
      m_paddedBytesCount += paddingCount;

      if (paddingCount > 0) {
        ++m_paddedF1FramesCount;
      } else {
        ++m_unpaddedF1FramesCount;
      }

      // Put the resulting data into a Data24 frame and push it to the output
      // buffer
      Data24 data24;
      data24.setData(data);
      data24.setErrorData(errorData);
      data24.setPaddedData(paddedData);

      data24Section.pushFrame(data24);
    }

    // Transfer the metadata
    data24Section.metadata = f1Section.metadata;

    // Add the section to the output buffer
    m_outputBuffer.push_back(data24Section);
  }
}

void F1SectionToData24Section::showStatistics() const {
  ORC_LOG_INFO("F1 Section to Data24 Section statistics:");

  ORC_LOG_INFO("  Frames:");
  ORC_LOG_INFO("    Total F1 frames: {}",
               m_validF1FramesCount + m_invalidF1FramesCount);
  ORC_LOG_INFO("    Error-free F1 frames: {}", m_validF1FramesCount);
  ORC_LOG_INFO("    F1 frames containing errors: {}", m_invalidF1FramesCount);
  ORC_LOG_INFO("    Padded F1 frames: {}", m_paddedF1FramesCount);
  ORC_LOG_INFO("    Unpadded F1 frames: {}", m_unpaddedF1FramesCount);

  ORC_LOG_INFO("  Data:");
  // E-8/P-10: total decoded bytes = every F1 frame's 24 bytes (corrupt bytes
  // are a subset of these, so they must not be added again). Compute in 64-bit
  // to avoid truncating the uint64 frame counters through a 32-bit product.
  const uint64_t totalBytes = (m_validF1FramesCount + m_invalidF1FramesCount) *
                              static_cast<uint64_t>(24);
  const uint64_t goodBytes =
      totalBytes >= m_corruptBytesCount ? totalBytes - m_corruptBytesCount : 0;
  const double totalSize = static_cast<double>(totalBytes);

  if (totalSize < 1024) {
    // Show in bytes if less than 1KB
    ORC_LOG_INFO("    Total bytes: {}", totalBytes);
    ORC_LOG_INFO("    Valid bytes: {}", goodBytes);
    ORC_LOG_INFO("    Corrupt bytes: {}", m_corruptBytesCount);
    ORC_LOG_INFO("    Padded bytes: {}", m_paddedBytesCount);
  } else if (totalSize < 1024 * 1024) {
    // Show in KB if less than 1MB
    double totalKBytes = static_cast<double>(totalBytes) / 1024.0;
    double validKBytes = static_cast<double>(goodBytes) / 1024.0;
    double corruptKBytes = static_cast<double>(m_corruptBytesCount) / 1024.0;
    double paddedKBytes = static_cast<double>(m_paddedBytesCount) / 1024.0;
    ORC_LOG_INFO("    Total KBytes: {:.2f}", totalKBytes);
    ORC_LOG_INFO("    Valid KBytes: {:.2f}", validKBytes);
    ORC_LOG_INFO("    Corrupt KBytes: {:.2f}", corruptKBytes);
    ORC_LOG_INFO("    Padded KBytes: {:.2f}", paddedKBytes);
  } else {
    // Show in MB if 1MB or larger
    double totalMBytes = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    double validMBytes = static_cast<double>(goodBytes) / (1024.0 * 1024.0);
    double corruptMBytes =
        static_cast<double>(m_corruptBytesCount) / (1024.0 * 1024.0);
    double paddedMBytes =
        static_cast<double>(m_paddedBytesCount) / (1024.0 * 1024.0);
    ORC_LOG_INFO("    Total MBytes: {:.2f}", totalMBytes);
    ORC_LOG_INFO("    Valid MBytes: {:.2f}", validMBytes);
    ORC_LOG_INFO("    Corrupt MBytes: {:.2f}", corruptMBytes);
    ORC_LOG_INFO("    Padded MBytes: {:.2f}", paddedMBytes);
  }

  // Guard against divide-by-zero when no frames were decoded.
  const double dataLoss =
      totalBytes > 0 ? (static_cast<double>(m_corruptBytesCount) * 100.0) /
                           static_cast<double>(totalBytes)
                     : 0.0;
  ORC_LOG_INFO("    Data loss: {:.3f}%", dataLoss);
}