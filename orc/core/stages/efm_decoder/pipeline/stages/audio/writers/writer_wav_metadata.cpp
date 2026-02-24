/*
 * File:        writer_wav_metadata.cpp
 * Module:      efm-decoder-audio
 * Purpose:     EFM Data24 to Audio decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "writer_wav_metadata.h"
#include <logging.h>
#include <fmt/format.h>
#include <algorithm>
#include <iomanip>
#include <sstream>

// This writer class writes metadata about audio data to a file
// This is used when the output is stereo audio data

WriterWavMetadata::WriterWavMetadata() :
    m_noAudioConcealment(false),
    m_inErrorRange(false),
    m_inConcealedRange(false),
    m_absoluteSectionTime(0, 0, 0),
    m_sectionTime(0, 0, 0),
    m_prevAbsoluteSectionTime(0, 0, 0),
    m_prevSectionTime(0, 0, 0),
    m_haveStartTime(false)
{}

WriterWavMetadata::~WriterWavMetadata()
{
    if (m_file.is_open()) {
        m_file.close();
    }
}

bool WriterWavMetadata::open(const std::string &filename, bool noAudioConcealment)
{
    m_file.open(filename);
    if (!m_file.is_open()) {
        LOG_CRITICAL("WriterWavMetadata::open() - Could not open file {} for writing", filename);
        return false;
    }
    LOG_DEBUG("WriterWavMetadata::open() - Opened file {} for data writing", filename);

    // If we're not concealing audio, we use "error" metadata instead of "silenced"
    m_noAudioConcealment = noAudioConcealment;

    return true;
}

void WriterWavMetadata::write(const AudioSection &audioSection)
{
    if (!m_file.is_open()) {
        LOG_CRITICAL("WriterWavMetadata::write() - File is not open for writing");
        return;
    }

    SectionMetadata metadata = audioSection.metadata;
    m_absoluteSectionTime = metadata.absoluteSectionTime();
    m_sectionTime = metadata.sectionTime();

    // Do we have the start time already?
    if (!m_haveStartTime) {
        m_startTime = m_absoluteSectionTime;
        m_haveStartTime = true;
    }

    // Get the relative time from the start time
    SectionTime relativeSectionTime = m_absoluteSectionTime - m_startTime;

    // Do we have a new track?
    if (std::find(m_trackNumbers.begin(), m_trackNumbers.end(), metadata.trackNumber()) == m_trackNumbers.end()) {
        // Check that the new track number is greater than the previous track numbers
        if (!m_trackNumbers.empty() && metadata.trackNumber() < m_trackNumbers.back()) {
            LOG_WARN("WriterWavMetadata::write() - Track number decreased from {} to {} - ignoring", m_trackNumbers.back(), metadata.trackNumber());
        } else {
            // Append the new track to the statistics
            if (metadata.trackNumber() != 0 && metadata.trackNumber() != 0xAA) {
                m_trackNumbers.push_back(metadata.trackNumber());
                
                m_trackAbsStartTimes.push_back(m_absoluteSectionTime);
                m_trackStartTimes.push_back(m_sectionTime);

                if (m_trackAbsStartTimes.size() == 1) {
                    // This is the first track, so we don't have an end time yet
                } else {
                    // Set the end time of the previous track
                    m_trackAbsEndTimes.push_back(m_prevAbsoluteSectionTime);
                    m_trackEndTimes.push_back(m_prevSectionTime);
                }
            }
    
            LOG_DEBUG("WriterWavMetadata::write() - New track {} detected with disc start time {} and track start time {}",
                metadata.trackNumber(), m_absoluteSectionTime.toString(), m_sectionTime.toString());
        }
    }

    // Output metadata about errors
    for (int subSection = 0; subSection < 98; ++subSection) {
        Audio audio = audioSection.frame(subSection);
        std::vector<int16_t> audioData = audio.data();
        std::vector<bool> errors = audio.errorData();
        std::vector<bool> concealed = audio.concealedData();
        
        for (int sampleOffset = 0; sampleOffset < 12; sampleOffset += 2) {
            // Errors/Silenced
            bool hasError = errors.at(sampleOffset) || errors.at(sampleOffset+1);
            
            if (hasError && !m_inErrorRange) {
                // Start of new error range
                m_errorRangeStart = convertToAudacityTimestamp(relativeSectionTime.minutes(), relativeSectionTime.seconds(),
                    relativeSectionTime.frameNumber(), subSection, sampleOffset);
                m_inErrorRange = true;
            } else if (!hasError && m_inErrorRange) {
                // End of error range
                std::string rangeEnd;
                if (sampleOffset == 0) {
                    // Handle wrap to previous subsection
                    if (subSection > 0) {
                        rangeEnd = convertToAudacityTimestamp(relativeSectionTime.minutes(), relativeSectionTime.seconds(),
                            relativeSectionTime.frameNumber(), subSection - 1, 11);
                    } else {
                        // If we're at the first subsection, just use the current position
                        rangeEnd = convertToAudacityTimestamp(relativeSectionTime.minutes(), relativeSectionTime.seconds(),
                            relativeSectionTime.frameNumber(), subSection, sampleOffset);
                    }
                } else {
                    rangeEnd = convertToAudacityTimestamp(relativeSectionTime.minutes(), relativeSectionTime.seconds(),
                        relativeSectionTime.frameNumber(), subSection, sampleOffset - 1);
                }

                std::string sampleTimeStamp = m_absoluteSectionTime.toString();
                std::string outputString = m_errorRangeStart + "\t" + rangeEnd + "\tError: " + sampleTimeStamp + "\n";
                if (!m_noAudioConcealment) outputString = m_errorRangeStart + "\t" + rangeEnd + "\tSilenced: " + sampleTimeStamp + "\n";

                m_file.write(outputString.c_str(), outputString.size());
                m_inErrorRange = false;
            }

            // Concealed
            bool hasConcealed = concealed.at(sampleOffset) || concealed.at(sampleOffset+1);
            
            if (hasConcealed && !m_inConcealedRange) {
                // Start of new error range
                m_concealedRangeStart = convertToAudacityTimestamp(relativeSectionTime.minutes(), relativeSectionTime.seconds(),
                    relativeSectionTime.frameNumber(), subSection, sampleOffset);
                    m_inConcealedRange = true;
            } else if (!hasConcealed && m_inConcealedRange) {
                // End of error range
                std::string rangeEnd;
                if (sampleOffset == 0) {
                    // Handle wrap to previous subsection
                    if (subSection > 0) {
                        rangeEnd = convertToAudacityTimestamp(relativeSectionTime.minutes(), relativeSectionTime.seconds(),
                            relativeSectionTime.frameNumber(), subSection - 1, 11);
                    } else {
                        // If we're at the first subsection, just use the current position
                        rangeEnd = convertToAudacityTimestamp(relativeSectionTime.minutes(), relativeSectionTime.seconds(),
                            relativeSectionTime.frameNumber(), subSection, sampleOffset);
                    }
                } else {
                    rangeEnd = convertToAudacityTimestamp(relativeSectionTime.minutes(), relativeSectionTime.seconds(),
                        relativeSectionTime.frameNumber(), subSection, sampleOffset - 1);
                }

                std::string sampleTimeStamp = m_absoluteSectionTime.toString();
                std::string outputString = m_concealedRangeStart + "\t" + rangeEnd + "\tConcealed: " + sampleTimeStamp + "\n";
                m_file.write(outputString.c_str(), outputString.size());
                m_inConcealedRange = false;
            }
        }
    }

    if (metadata.trackNumber() != 0 && metadata.trackNumber() != 0xAA) {
        // Update the previous times
        m_prevAbsoluteSectionTime = m_absoluteSectionTime;
        m_prevSectionTime = m_sectionTime;
    }
}

void WriterWavMetadata::flush()
{
    if (!m_file.is_open()) {
        return;
    }

    // Note: For track 1 the track time metadata might be wrong.  On some discs the first track includes unmarked lead-in.
    // Basically, at absolute disc time of 00:00:00 the track time might be positive (e.g 00:01:74 or 2 seconds) and then
    // it will count down to 00:00:00 - at which point the track starts and time starts counting up again.
    //
    // This isn't handled by the metadata writer, so the first track might have an incorrect track start time (but the
    // absolute time will be correct). 

    // Set the end time of the previous track
    m_trackAbsEndTimes.push_back(m_prevAbsoluteSectionTime);
    m_trackEndTimes.push_back(m_prevSectionTime);

    // Only write the metadata if we have more than one track
    if (m_trackNumbers.size() > 1) {
        // Write the track metadata
        for (size_t i = 0; i < m_trackNumbers.size(); ++i) {
            std::string trackAbsStartTime = convertToAudacityTimestamp(m_trackAbsStartTimes[i].minutes(), m_trackAbsStartTimes[i].seconds(),
                m_trackAbsStartTimes[i].frameNumber(), 0, 0);

            std::string trackAbsEndTime = convertToAudacityTimestamp(m_trackAbsEndTimes[i].minutes(), m_trackAbsEndTimes[i].seconds(),
                m_trackAbsEndTimes[i].frameNumber(), 0, 0);

            std::string trackNumber = fmt::format("{:02d}", m_trackNumbers.at(i));
            std::string trackTime = "[" + m_trackStartTimes[i].toString() + "-" + m_trackEndTimes[i].toString() + "]";

            std::string outputString = trackAbsStartTime + "\t" + trackAbsEndTime + "\tTrack: " + trackNumber + " " + trackTime + "\n";
            m_file.write(outputString.c_str(), outputString.size());

            std::string debugString = m_trackAbsStartTimes[i].toString() + " " + m_trackAbsEndTimes[i].toString() + " Track: " + trackNumber + " " + trackTime;
            LOG_DEBUG("WriterWavMetadata::flush(): Wrote track metadata: {}", debugString);
        }
    } else {
        LOG_DEBUG("WriterWavMetadata::flush(): Only 1 track present - not writing track metadata");
    }
}

void WriterWavMetadata::close()
{
    if (!m_file.is_open()) {
        return;
    }

    // Finish writing the metadata
    flush();

    // If we're still in an error range when closing, write the final range
    if (m_inErrorRange) {
        std::string outputString = m_errorRangeStart + "\t" + m_errorRangeStart + "\tError: Incomplete range\n";
        m_file.write(outputString.c_str(), outputString.size());
    }

    m_file.close();
    LOG_DEBUG("WriterWavMetadata::close(): Closed the WAV metadata file");
}

int64_t WriterWavMetadata::size()
{
    if (m_file.is_open()) {
        return m_file.tellp();
    }

    return 0;
}

std::string WriterWavMetadata::convertToAudacityTimestamp(int32_t minutes, int32_t seconds, int32_t frames,
    int32_t subsection, int32_t sample)
{
    // Constants for calculations
    constexpr double FRAME_RATE = 75.0;      // 75 frames per second
    constexpr double SUBSECTIONS_PER_FRAME = 98.0; // 98 subsections per frame
    constexpr double SAMPLES_PER_SUBSECTION = 6.0; // 6 stereo samples per subsection

    // Convert minutes and seconds to total seconds
    double total_seconds = (minutes * 60.0) + seconds;
    
    // Convert frames to seconds
    total_seconds += (frames) / FRAME_RATE;
    
    // Convert subsection to fractional time
    total_seconds += subsection / (FRAME_RATE * SUBSECTIONS_PER_FRAME);
    
    // Convert sample to fractional time
    total_seconds += (sample/2) / (FRAME_RATE * SUBSECTIONS_PER_FRAME * SAMPLES_PER_SUBSECTION);

    // Format the output string with 6 decimal places
    return fmt::format("{:.6f}", total_seconds);
}
