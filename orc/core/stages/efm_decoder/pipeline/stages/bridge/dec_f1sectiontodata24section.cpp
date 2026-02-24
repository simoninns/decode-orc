/*
 * File:        dec_f1sectiontodata24section.cpp
 * Module:      ld-efm-decoder
 * Purpose:     EFM data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_f1sectiontodata24section.h"
#include <cstdlib>
#include <utility>
#include <sstream>
#include <iomanip>

F1SectionToData24Section::F1SectionToData24Section() :
    m_invalidF1FramesCount(0),
    m_validF1FramesCount(0),
    m_corruptBytesCount(0),
    m_paddedBytesCount(0),
    m_unpaddedF1FramesCount(0),
    m_paddedF1FramesCount(0)
{}

void F1SectionToData24Section::pushSection(const F1Section &f1Section)
{
    // Add the data to the input buffer
    m_inputBuffer.push_back(f1Section);

    // Process the queue
    processQueue();
}

Data24Section F1SectionToData24Section::popSection()
{
    // Return the first item in the output buffer
    Data24Section section = m_outputBuffer.front();
    m_outputBuffer.pop_front();
    return section;
}

bool F1SectionToData24Section::isReady() const
{
    // Return true if the output buffer is not empty
    return !m_outputBuffer.empty();
}

void F1SectionToData24Section::processQueue()
{
    // Process the input buffer
    while (!m_inputBuffer.empty()) {
        F1Section f1Section = m_inputBuffer.front();
        m_inputBuffer.pop_front();
        Data24Section data24Section;

        // Sanity check the F1 section
        if (!f1Section.isComplete()) {
            LOG_CRITICAL("F1SectionToData24Section::processQueue - F1 Section is not complete");
            std::exit(1);
        }

        for (int index = 0; index < 98; ++index) {
            std::vector<uint8_t> data = f1Section.frame(index).data();
            std::vector<bool> errorData = f1Section.frame(index).errorData();
            std::vector<bool> paddedData = f1Section.frame(index).paddedData();

            // ECMA-130 issue 2 page 16 - Clause 16
            // All byte pairs are swapped by the F1 Frame encoder
            if (data.size() == errorData.size()) {
                for (size_t i = 0; i + 1 < data.size(); i += 2) {
                    std::swap(data[i], data[i + 1]);
                    bool errorBit = errorData[i];
                    errorData[i] = errorData[i + 1];
                    errorData[i + 1] = errorBit;

                    bool paddedBit = paddedData[i];
                    paddedData[i] = paddedData[i + 1];
                    paddedData[i + 1] = paddedBit;
                }
            } else {
                LOG_CRITICAL("Data and error data size mismatch in F1 frame {}", index);
                std::exit(1);
            }

            // Check the error data (and count any flagged errors)
            uint32_t errorCount = f1Section.frame(index).countErrors();

            m_corruptBytesCount += errorCount;

            if (errorCount > 0)
                ++m_invalidF1FramesCount;
            else
                ++m_validF1FramesCount;

            // Check the error data (and count any flagged padding)
            uint32_t paddingCount = f1Section.frame(index).countPadded();
            m_paddedBytesCount += paddingCount;

            if (paddingCount > 0)
                ++m_paddedF1FramesCount;
            else
                ++m_unpaddedF1FramesCount;

            // Put the resulting data into a Data24 frame and push it to the output buffer
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

void F1SectionToData24Section::showStatistics() const
{
    LOG_INFO("F1 Section to Data24 Section statistics:");

    LOG_INFO("  Frames:");
    LOG_INFO("    Total F1 frames: {}", m_validF1FramesCount + m_invalidF1FramesCount);
    LOG_INFO("    Error-free F1 frames: {}", m_validF1FramesCount);
    LOG_INFO("    F1 frames containing errors: {}", m_invalidF1FramesCount);
    LOG_INFO("    Padded F1 frames: {}", m_paddedF1FramesCount);
    LOG_INFO("    Unpadded F1 frames: {}", m_unpaddedF1FramesCount);

    LOG_INFO("  Data:");
    uint32_t validBytes = (m_validF1FramesCount + m_invalidF1FramesCount) * 24;
    double totalSize = validBytes + m_corruptBytesCount;

    if (totalSize < 1024) {
        // Show in bytes if less than 1KB
        LOG_INFO("    Total bytes: {}", validBytes + m_corruptBytesCount);
        LOG_INFO("    Valid bytes: {}", validBytes);
        LOG_INFO("    Corrupt bytes: {}", m_corruptBytesCount);
        LOG_INFO("    Padded bytes: {}", m_paddedBytesCount);
    } else if (totalSize < 1024 * 1024) {
        // Show in KB if less than 1MB
        double validKBytes = static_cast<double>(validBytes + m_corruptBytesCount) / 1024.0;
        double validOnlyKBytes = static_cast<double>(validBytes) / 1024.0;
        double corruptKBytes = static_cast<double>(m_corruptBytesCount) / 1024.0;
        double paddedKBytes = static_cast<double>(m_paddedBytesCount) / 1024.0;
        LOG_INFO("    Total KBytes: {:.2f}", validKBytes);
        LOG_INFO("    Valid KBytes: {:.2f}", validOnlyKBytes);
        LOG_INFO("    Corrupt KBytes: {:.2f}", corruptKBytes);
        LOG_INFO("    Padded KBytes: {:.2f}", paddedKBytes);
    } else {
        // Show in MB if 1MB or larger
        double validMBytes = static_cast<double>(validBytes + m_corruptBytesCount) / (1024.0 * 1024.0);
        double validOnlyMBytes = static_cast<double>(validBytes) / (1024.0 * 1024.0);
        double corruptMBytes = static_cast<double>(m_corruptBytesCount) / (1024.0 * 1024.0);
        double paddedMBytes = static_cast<double>(m_paddedBytesCount) / (1024.0 * 1024.0);
        LOG_INFO("    Total MBytes: {:.2f}", validMBytes);
        LOG_INFO("    Valid MBytes: {:.2f}", validOnlyMBytes);
        LOG_INFO("    Corrupt MBytes: {:.2f}", corruptMBytes);
        LOG_INFO("    Padded MBytes: {:.2f}", paddedMBytes);
    }

    LOG_INFO("    Data loss: {:.3f}%", (m_corruptBytesCount * 100.0) / validBytes);
}

std::string F1SectionToData24Section::statisticsText() const
{
    std::ostringstream output;
    output << "F1 Section to Data24 Section statistics:\n";
    output << "  Frames:\n";
    output << "    Total F1 frames: " << (m_validF1FramesCount + m_invalidF1FramesCount) << "\n";
    output << "    Error-free F1 frames: " << m_validF1FramesCount << "\n";
    output << "    F1 frames containing errors: " << m_invalidF1FramesCount << "\n";
    output << "    Padded F1 frames: " << m_paddedF1FramesCount << "\n";
    output << "    Unpadded F1 frames: " << m_unpaddedF1FramesCount << "\n";

    output << "  Data:\n";
    uint32_t validBytes = (m_validF1FramesCount + m_invalidF1FramesCount) * 24;
    double totalSize = validBytes + m_corruptBytesCount;

    output << std::fixed;
    if (totalSize < 1024) {
        output << "    Total bytes: " << (validBytes + m_corruptBytesCount) << "\n";
        output << "    Valid bytes: " << validBytes << "\n";
        output << "    Corrupt bytes: " << m_corruptBytesCount << "\n";
        output << "    Padded bytes: " << m_paddedBytesCount << "\n";
    } else if (totalSize < 1024 * 1024) {
        double validKBytes = static_cast<double>(validBytes + m_corruptBytesCount) / 1024.0;
        double validOnlyKBytes = static_cast<double>(validBytes) / 1024.0;
        double corruptKBytes = static_cast<double>(m_corruptBytesCount) / 1024.0;
        double paddedKBytes = static_cast<double>(m_paddedBytesCount) / 1024.0;
        output << std::setprecision(2);
        output << "    Total KBytes: " << validKBytes << "\n";
        output << "    Valid KBytes: " << validOnlyKBytes << "\n";
        output << "    Corrupt KBytes: " << corruptKBytes << "\n";
        output << "    Padded KBytes: " << paddedKBytes << "\n";
    } else {
        double validMBytes = static_cast<double>(validBytes + m_corruptBytesCount) / (1024.0 * 1024.0);
        double validOnlyMBytes = static_cast<double>(validBytes) / (1024.0 * 1024.0);
        double corruptMBytes = static_cast<double>(m_corruptBytesCount) / (1024.0 * 1024.0);
        double paddedMBytes = static_cast<double>(m_paddedBytesCount) / (1024.0 * 1024.0);
        output << std::setprecision(2);
        output << "    Total MBytes: " << validMBytes << "\n";
        output << "    Valid MBytes: " << validOnlyMBytes << "\n";
        output << "    Corrupt MBytes: " << corruptMBytes << "\n";
        output << "    Padded MBytes: " << paddedMBytes << "\n";
    }

    output << std::setprecision(3);
    output << "    Data loss: " << ((m_corruptBytesCount * 100.0) / validBytes) << "%";
    return output.str();
}