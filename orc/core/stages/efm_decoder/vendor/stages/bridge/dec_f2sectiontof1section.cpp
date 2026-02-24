/*
 * File:        dec_f2sectiontof1section.cpp
 * Module:      ld-efm-decoder
 * Purpose:     EFM data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dec_f2sectiontof1section.h"
#include <logging.h>
#include <fmt/core.h>
#include <algorithm>
#include <cstdlib>
#include <sstream>

F2SectionToF1Section::F2SectionToF1Section() :
    m_delayLine1({ 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
        0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1 }),
    m_delayLine2({ 0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2, 2, 2, 0, 0, 0, 0, 2, 2, 2, 2 }),
    m_delayLineM({ 108, 104, 100, 96, 92, 88, 84, 80, 76, 72, 68, 64, 60, 56,
        52,  48,  44,  40, 36, 32, 28, 24, 20, 16, 12, 8,  4,  0 }),
    m_invalidInputF2FramesCount(0),
    m_validInputF2FramesCount(0),
    m_invalidOutputF1FramesCount(0),
    m_validOutputF1FramesCount(0),
    m_dlLostFramesCount(0),
    m_continuityErrorCount(0),
    m_inputByteErrors(0),
    m_outputByteErrors(0),
    m_invalidPaddedF1FramesCount(0),
    m_invalidNonPaddedF1FramesCount(0),
    m_lastFrameNumber(-1)
{}

void F2SectionToF1Section::pushSection(const F2Section &f2Section)
{
    // Add the data to the input buffer
    m_inputBuffer.push_back(f2Section);

    // Process the queue
    processQueue();
}

F1Section F2SectionToF1Section::popSection()
{
    // Return the first item in the output buffer
    F1Section section = m_outputBuffer.front();
    m_outputBuffer.pop_front();
    return section;
}

bool F2SectionToF1Section::isReady() const
{
    // Return true if the output buffer is not empty
    return !m_outputBuffer.empty();
}

// Note: The F2 frames will not be correct until the delay lines are full
// So lead-in is required to prevent loss of the input date.  For now we will
// just discard the data until the delay lines are full.
void F2SectionToF1Section::processQueue()
{
    // Process the input buffer
    while (!m_inputBuffer.empty()) {
        F2Section f2Section = m_inputBuffer.front();
        m_inputBuffer.pop_front();
        F1Section f1Section;

        // Sanity check the F2 section
        if (!f2Section.isComplete()) {
            LOG_CRITICAL("F2SectionToF1Section::processQueue - F2 Section is not complete");
            LOG_CRITICAL("This usually indicates a stream error or EOF while reading from stdin in a pipeline");
            LOG_CRITICAL("Check that the input EFM stream is properly formatted and complete");
            std::exit(1);
        }

        // Check section continuity
        if (m_lastFrameNumber != -1) {
            if (f2Section.metadata.absoluteSectionTime().frames()
                != m_lastFrameNumber + 1) {
                LOG_WARN("F2 Section continuity error last frame: {} current frame: {}",
                             m_lastFrameNumber, f2Section.metadata.absoluteSectionTime().frames());
                LOG_WARN("Last section time: {}", f2Section.metadata.absoluteSectionTime().toString());
                LOG_WARN("This is a bug in the F2 Metadata correction and should be reported");
                m_continuityErrorCount++;
            }
            m_lastFrameNumber = f2Section.metadata.absoluteSectionTime().frames();
        } else {
            m_lastFrameNumber = f2Section.metadata.absoluteSectionTime().frames();
        }

        for (int index = 0; index < 98; index++) {
            std::vector<uint8_t> data = f2Section.frame(index).data();
            std::vector<bool> errorData = f2Section.frame(index).errorData();
            std::vector<bool> paddedData = f2Section.frame(index).paddedData();

            // Check F2 frame for errors (counts only when errorData = 1)
            uint32_t inFrameErrors = f2Section.frame(index).countErrors();
            if (inFrameErrors == 0)
                m_validInputF2FramesCount++;
            else {
                m_invalidInputF2FramesCount++;
                m_inputByteErrors += inFrameErrors;
            }

            m_delayLine1.push(data, errorData, paddedData);
            if (data.empty()) {
                // Output an empty F1 frame (ensures the section is complete)
                // Note: This isn't an error frame, it's just an empty frame
                F1Frame f1Frame;
                f1Frame.setData(std::vector<uint8_t>(24, 0));
                f1Frame.setErrorData(std::vector<bool>(24, false));
                f1Frame.setPaddedData(std::vector<bool>(24, false));
                f1Section.pushFrame(f1Frame);
                m_dlLostFramesCount++;
                continue;
            }

            // Process the data
            // Note: We will only get valid data if the delay lines are all full
            m_inverter.invertParity(data);

            m_circ.c1Decode(data, errorData, paddedData);

            m_delayLineM.push(data, errorData, paddedData);
            if (data.empty()) {
                // Output an empty F1 frame (ensures the section is complete)
                // Note: This isn't an error frame, it's just an empty frame
                F1Frame f1Frame;
                f1Frame.setData(std::vector<uint8_t>(24, 0));
                f1Frame.setErrorData(std::vector<bool>(24, false));
                f1Frame.setPaddedData(std::vector<bool>(24, false));
                f1Section.pushFrame(f1Frame);
                m_dlLostFramesCount++;
                continue;
            }

            // Only perform C2 decode if delay line 1 is full and delay line M is full
            m_circ.c2Decode(data, errorData, paddedData);

            if (std::any_of(errorData.begin(), errorData.end(), [](bool value) { return value; })) {
                LOG_DEBUG("F2SectionToF1Section - F2 Frame [{}]: C2 Failed in section {}",
                              index, f2Section.metadata.absoluteSectionTime().toString());
            }

            m_interleave.deinterleave(data, errorData, paddedData);

            m_delayLine2.push(data, errorData, paddedData);
            if (data.empty()) {
                // Output an empty F1 frame (ensures the section is complete)
                // Note: This isn't an error frame, it's just an empty frame
                F1Frame f1Frame;
                f1Frame.setData(std::vector<uint8_t>(24, 0));
                f1Frame.setErrorData(std::vector<bool>(24, false));
                f1Frame.setPaddedData(std::vector<bool>(24, false));
                f1Section.pushFrame(f1Frame);
                m_dlLostFramesCount++;
                continue;
            }

            // Put the resulting data (and error data) into an F1 frame and
            // push it to the output buffer
            F1Frame f1Frame;
            f1Frame.setData(data);
            f1Frame.setErrorData(errorData);
            f1Frame.setPaddedData(paddedData);

            // Check F1 frame for errors
            // Note: The error C2 count will differ from the overall error F1 count
            // due to the the interleaving which will distribute the errors over more
            // than on frame (potentially)
            uint32_t outFrameErrors = f1Frame.countErrors();
            uint32_t outFramePadding = f1Frame.countPadded();

            if (outFrameErrors == 0 && outFramePadding == 0)
                m_validOutputF1FramesCount++;
            else {
                m_invalidOutputF1FramesCount++;
                m_outputByteErrors += outFrameErrors;

                // Invalid with or without padding?
                if (outFramePadding > 0) m_invalidPaddedF1FramesCount++;
                else m_invalidNonPaddedF1FramesCount++;
            }

            f1Section.pushFrame(f1Frame);
        }

        // All frames in the section are processed
        f1Section.metadata = f2Section.metadata;

        // Add the section to the output buffer
        m_outputBuffer.push_back(f1Section);
    }
}

void F2SectionToF1Section::showData(const std::string &description, int32_t index, const std::string &timeString,
                                    std::vector<uint8_t> &data, std::vector<bool> &dataError)
{
    // Early return if no errors to avoid string processing
    bool hasError = false;
    for (const bool error : dataError) {
        if (error) {
            hasError = true;
            break;
        }
    }
    if (!hasError) return;

    // Pre-allocate string with approximate size needed
    std::string dataString;
    dataString.reserve(data.size() * 3);

    // Process data only if we know we need to display it
    for (size_t i = 0; i < data.size(); ++i) {
        if (!dataError[i]) {
            dataString.append(fmt::format("{:02x} ", data[i]));
        } else {
            dataString.append("XX ");
        }
    }

    LOG_DEBUG("F2SectionToF1Section - {}[{:02d}]: ({}) {}XX=ERROR",
                  description, index, timeString, dataString);
}

void F2SectionToF1Section::showStatistics() const
{
    LOG_INFO("F2 Section to F1 Section statistics:");
    LOG_INFO("  Input F2 Frames:");
    LOG_INFO("    Valid frames: {}", m_validInputF2FramesCount);
    LOG_INFO("    Corrupt frames: {} frames containing {} byte errors",
                 m_invalidInputF2FramesCount, m_inputByteErrors);
    LOG_INFO("    Delay line lost frames: {}", m_dlLostFramesCount);
    LOG_INFO("    Continuity errors: {}", m_continuityErrorCount);

    LOG_INFO("  Output F1 Frames (after CIRC):");
    LOG_INFO("    Valid frames: {}", m_validOutputF1FramesCount);
    LOG_INFO("    Invalid frames due to padding: {}", m_invalidPaddedF1FramesCount);
    LOG_INFO("    Invalid frames without padding: {}", m_invalidNonPaddedF1FramesCount);
    LOG_INFO("    Invalid frames (total): {}", m_invalidOutputF1FramesCount);
    LOG_INFO("    Output byte errors: {}", m_outputByteErrors);

    LOG_INFO("  C1 decoder:");
    LOG_INFO("    Valid C1s: {}", m_circ.validC1s());
    LOG_INFO("    Fixed C1s: {}", m_circ.fixedC1s());
    LOG_INFO("    Error C1s: {}", m_circ.errorC1s());

    LOG_INFO("  C2 decoder:");
    LOG_INFO("    Valid C2s: {}", m_circ.validC2s());
    LOG_INFO("    Fixed C2s: {}", m_circ.fixedC2s());
    LOG_INFO("    Error C2s: {}", m_circ.errorC2s());
}

std::string F2SectionToF1Section::statisticsText() const
{
    std::ostringstream output;
    output << "F2 Section to F1 Section statistics:\n";
    output << "  Input F2 Frames:\n";
    output << "    Valid frames: " << m_validInputF2FramesCount << "\n";
    output << "    Corrupt frames: " << m_invalidInputF2FramesCount
           << " frames containing " << m_inputByteErrors << " byte errors\n";
    output << "    Delay line lost frames: " << m_dlLostFramesCount << "\n";
    output << "    Continuity errors: " << m_continuityErrorCount << "\n";

    output << "  Output F1 Frames (after CIRC):\n";
    output << "    Valid frames: " << m_validOutputF1FramesCount << "\n";
    output << "    Invalid frames due to padding: " << m_invalidPaddedF1FramesCount << "\n";
    output << "    Invalid frames without padding: " << m_invalidNonPaddedF1FramesCount << "\n";
    output << "    Invalid frames (total): " << m_invalidOutputF1FramesCount << "\n";
    output << "    Output byte errors: " << m_outputByteErrors << "\n";

    output << "  C1 decoder:\n";
    output << "    Valid C1s: " << m_circ.validC1s() << "\n";
    output << "    Fixed C1s: " << m_circ.fixedC1s() << "\n";
    output << "    Error C1s: " << m_circ.errorC1s() << "\n";

    output << "  C2 decoder:\n";
    output << "    Valid C2s: " << m_circ.validC2s() << "\n";
    output << "    Fixed C2s: " << m_circ.fixedC2s() << "\n";
    output << "    Error C2s: " << m_circ.errorC2s();
    return output.str();
}
