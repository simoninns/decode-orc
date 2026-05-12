/*
 * File:        efm_processor.cpp
 * Purpose:     efm-decoder - Unified EFM decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "efm_processor.h"
#include "logging.h"
#include <spdlog/sinks/basic_file_sink.h>

EfmProcessor::EfmProcessor()
    : m_audioMode(true)
    , m_noTimecodes(false)
    , m_audacityLabels(false)
    , m_noAudioConcealment(false)
    , m_zeroPad(false)
    , m_noWavHeader(false)
    , m_outputMetadata(false)
    , m_reportOutput(false)
{
}

// ---------------------------------------------------------------------------
// Configuration setters
// ---------------------------------------------------------------------------

void EfmProcessor::setAudioMode(bool audioMode)
{
    m_audioMode = audioMode;
}

void EfmProcessor::setNoTimecodes(bool noTimecodes)
{
    m_noTimecodes = noTimecodes;
}

void EfmProcessor::setAudacityLabels(bool audacityLabels)
{
    m_audacityLabels = audacityLabels;
}

void EfmProcessor::setNoAudioConcealment(bool noAudioConcealment)
{
    m_noAudioConcealment = noAudioConcealment;
}

void EfmProcessor::setZeroPad(bool zeroPad)
{
    m_zeroPad = zeroPad;
}

void EfmProcessor::setNoWavHeader(bool noWavHeader)
{
    m_noWavHeader = noWavHeader;
}

void EfmProcessor::setOutputMetadata(bool outputMetadata)
{
    m_outputMetadata = outputMetadata;
}

void EfmProcessor::setReportOutput(bool reportOutput)
{
    m_reportOutput = reportOutput;
}

// ---------------------------------------------------------------------------
// Streaming API
// ---------------------------------------------------------------------------

bool EfmProcessor::beginStream(const std::string &outputFilename, int64_t totalTValues)
{
    m_outputFilename   = outputFilename;
    m_zeroPadApplied   = false;
    m_totalTValues     = totalTValues;
    m_processedTValues = 0;
    m_lastProgress     = 0;

    // Apply decoder configuration
    m_f2SectionCorrection.setNoTimecodes(m_noTimecodes);

    // Open output writers based on mode
    if (m_audioMode) {
        if (m_noWavHeader) {
            m_writerRaw.open(outputFilename);
        } else {
            m_writerWav.open(outputFilename);
        }
        if (m_audacityLabels) {
            std::string labelsFilename = outputFilename;
            size_t dotPos = labelsFilename.rfind(".wav");
            if (dotPos != std::string::npos && dotPos == labelsFilename.length() - 4) {
                labelsFilename = labelsFilename.substr(0, dotPos) + ".txt";
            } else {
                labelsFilename += ".txt";
            }
            m_writerWavMetadata.open(labelsFilename, m_noAudioConcealment);
        }
    } else {
        m_writerSector.open(outputFilename);
        if (m_outputMetadata) {
            std::string metadataFilename = outputFilename + ".bsm";
            m_writerSectorMetadata.open(metadataFilename);
        }
    }

    // Record overall wall-clock start time
    m_startTime = std::chrono::high_resolution_clock::now();

    return true;
}

void EfmProcessor::pushChunk(const std::vector<uint8_t> &chunk)
{
    if (chunk.empty()) {
        return;
    }

    m_processedTValues += static_cast<int64_t>(chunk.size());

    // Log progress at 5 % intervals (based on caller-supplied total)
    if (m_totalTValues > 0) {
        int progress = static_cast<int>((m_processedTValues * 100) / m_totalTValues);
        if (progress >= m_lastProgress + 5) {
            ORC_LOG_INFO("Progress: {} %", progress);
            m_lastProgress = progress;
        }
    }

    m_tValuesToChannel.pushFrame(chunk);
    drainPipeline(m_zeroPadApplied);
}

bool EfmProcessor::finishStream()
{
    // Final drain with no new input (mirrors the empty-chunk end-of-data pass
    // that the old buffer loop performed before breaking).
    drainPipeline(m_zeroPadApplied);

    // Flush decoders that require an explicit flush, then do a final drain.
    // F2SectionCorrection must be flushed first; its output (through the
    // full pipeline) must all reach AudioCorrection before we flush
    // AudioCorrection, otherwise the final corrected frames are lost.
    ORC_LOG_INFO("Flushing decoding pipelines");
    m_f2SectionCorrection.flush();

    // Drain everything that the F2SectionCorrection flush produces,
    // pushing the final Data24 sections into the audio/data pipeline.
    ORC_LOG_INFO("Processing final pipeline data");
    drainPipeline(m_zeroPadApplied);

    // Now all Data24 sections have been pushed into AudioCorrection;
    // flush it to release its internal lookahead buffer, then drain once more.
    if (m_audioMode && !m_noAudioConcealment) {
        m_audioCorrection.flush();
        drainAudioPipeline();
    }

    // Validate and show statistics
    bool success = true;
    if (!m_f2SectionCorrection.isValid()) {
        success = false;
        ORC_LOG_WARN("Decoding FAILED");
        ORC_LOG_WARN("F2 Section Correction stage did not complete lead-in detection successfully.");
        ORC_LOG_WARN("This could be due to invalid input data or due to missing timecode information in the input EFM.");
        ORC_LOG_WARN("If you think the input EFM is valid - try running again with the --no-timecodes option "
                 "(in case the input EFM is not ECMA-130 compliant and does not contain timecodes).");
    } else {
        ORC_LOG_INFO("Decoding complete");

        if (m_reportOutput) {
            std::string reportFilename = m_outputFilename + ".txt";
            ORC_LOG_INFO("Writing decoding report to: {}", reportFilename);
            auto reportSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(reportFilename, true);
            reportSink->set_level(spdlog::level::info);
            reportSink->set_pattern("%v");
            orc::get_logger()->sinks().push_back(reportSink);
            showAllStatistics();
            orc::get_logger()->flush();
            orc::get_logger()->sinks().pop_back();
        } else {
            showAllStatistics();
        }
    }

    // Close all open files
    if (m_writerWav.isOpen())            m_writerWav.close();
    if (m_writerRaw.isOpen())            m_writerRaw.close();
    if (m_writerWavMetadata.isOpen())    m_writerWavMetadata.close();
    if (m_writerSector.isOpen())         m_writerSector.close();
    if (m_writerSectorMetadata.isOpen()) m_writerSectorMetadata.close();

    return success;
}

// ---------------------------------------------------------------------------
// Convenience wrapper: buffers all t-values then uses the streaming API
// ---------------------------------------------------------------------------

bool EfmProcessor::processFromBuffer(const std::vector<uint8_t> &tValues,
                                     const std::string &outputFilename)
{
    beginStream(outputFilename, static_cast<int64_t>(tValues.size()));

    // Feed through the pipeline in 1024-byte strides (same stride as before)
    size_t offset = 0;
    while (offset < tValues.size()) {
        size_t count = std::min<size_t>(1024, tValues.size() - offset);
        std::vector<uint8_t> chunk(tValues.begin() + static_cast<std::ptrdiff_t>(offset),
                                   tValues.begin() + static_cast<std::ptrdiff_t>(offset + count));
        offset += count;
        pushChunk(chunk);
    }

    return finishStream();
}

// ---------------------------------------------------------------------------
// Internal pipeline helpers
// ---------------------------------------------------------------------------

void EfmProcessor::drainPipeline(bool &zeroPadApplied)
{
    // -----------------------------------------------------------------------
    // General pipeline: T-values → Channel → F3 → F2Section → F2SectionCorrection
    // -----------------------------------------------------------------------

    auto t0 = std::chrono::high_resolution_clock::now();
    while (m_tValuesToChannel.isReady()) {
        m_channelToF3.pushFrame(m_tValuesToChannel.popFrame());
    }
    m_pipelineStats.channelToF3Time += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - t0).count();

    t0 = std::chrono::high_resolution_clock::now();
    while (m_channelToF3.isReady()) {
        F3Frame f3Frame = m_channelToF3.popFrame();
        m_f3FrameToF2Section.pushFrame(f3Frame);
    }
    m_pipelineStats.f3ToF2Time += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - t0).count();

    t0 = std::chrono::high_resolution_clock::now();
    while (m_f3FrameToF2Section.isReady()) {
        F2Section f2Section = m_f3FrameToF2Section.popSection();
        m_f2SectionCorrection.pushSection(f2Section);
    }
    m_pipelineStats.f2CorrectionTime += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - t0).count();

    // -----------------------------------------------------------------------
    // D24 pipeline: F2SectionCorrection → F2SectionToF1Section → F1SectionToData24Section
    // -----------------------------------------------------------------------

    t0 = std::chrono::high_resolution_clock::now();
    while (m_f2SectionCorrection.isReady()) {
        F2Section f2Section = m_f2SectionCorrection.popSection();
        m_f2SectionToF1Section.pushSection(f2Section);
    }
    m_pipelineStats.f2ToF1Time += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - t0).count();

    t0 = std::chrono::high_resolution_clock::now();
    while (m_f2SectionToF1Section.isReady()) {
        F1Section f1Section = m_f2SectionToF1Section.popSection();
        f1Section.showData();
        m_f1SectionToData24Section.pushSection(f1Section);
    }
    m_pipelineStats.f1ToData24Time += std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - t0).count();

    // -----------------------------------------------------------------------
    // Route Data24 sections to audio or data pipeline
    // -----------------------------------------------------------------------

    while (m_f1SectionToData24Section.isReady()) {
        Data24Section data24Section = m_f1SectionToData24Section.popSection();
        data24Section.showData();

        if (m_audioMode) {
            // Apply zero-padding on first section when --zero-pad is active
            if (m_zeroPad && !zeroPadApplied) {
                zeroPadApplied = true;
                int32_t requiredPadding = data24Section.metadata.absoluteSectionTime().frames();
                if (requiredPadding > 0) {
                    ORC_LOG_INFO("Zero padding enabled, start time is {} and requires {} frames of padding",
                        data24Section.metadata.absoluteSectionTime().toString(), requiredPadding);

                    // Build a single all-zero Data24Section template
                    SectionTime zeroTime(0, 0, 0);
                    Data24Section zeroSection;
                    zeroSection.metadata = data24Section.metadata;
                    zeroSection.metadata.setAbsoluteSectionTime(zeroTime);
                    zeroSection.metadata.setSectionTime(zeroTime);
                    for (int j = 0; j < 98; ++j) {
                        Data24 data24Zero;
                        data24Zero.setData(std::vector<uint8_t>(24, 0));
                        data24Zero.setErrorData(std::vector<uint8_t>(24, 0));
                        data24Zero.setPaddedData(std::vector<uint8_t>(24, 1));
                        zeroSection.pushFrame(data24Zero);
                    }

                    for (int32_t i = 0; i < requiredPadding; ++i) {
                        zeroSection.metadata.setAbsoluteSectionTime(zeroTime);
                        zeroSection.metadata.setSectionTime(zeroTime);
                        auto pad_t0 = std::chrono::high_resolution_clock::now();
                        m_data24ToAudio.pushSection(zeroSection);
                        m_pipelineStats.data24ToAudioTime +=
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::high_resolution_clock::now() - pad_t0).count();
                        drainAudioPipeline();
                        ++zeroTime;
                    }
                }
            }

            auto audio_t0 = std::chrono::high_resolution_clock::now();
            m_data24ToAudio.pushSection(data24Section);
            m_pipelineStats.data24ToAudioTime +=
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now() - audio_t0).count();
            drainAudioPipeline();

        } else {
            auto data_t0 = std::chrono::high_resolution_clock::now();
            m_data24ToRawSector.pushSection(data24Section);
            m_pipelineStats.data24ToRawSectorTime +=
                std::chrono::duration_cast<std::chrono::microseconds>(
                    std::chrono::high_resolution_clock::now() - data_t0).count();
            drainDataPipeline();
        }
    }
}

void EfmProcessor::drainAudioPipeline()
{
    if (m_noAudioConcealment) {
        // Bypass correction — write decoded audio directly
        while (m_data24ToAudio.isReady()) {
            AudioSection audioSection = m_data24ToAudio.popSection();
            if (m_noWavHeader) {
                m_writerRaw.write(audioSection);
            } else {
                m_writerWav.write(audioSection);
            }
            if (m_audacityLabels) {
                m_writerWavMetadata.write(audioSection);
            }
        }
    } else {
        // Feed through AudioCorrection
        auto t0 = std::chrono::high_resolution_clock::now();
        while (m_data24ToAudio.isReady()) {
            AudioSection audioSection = m_data24ToAudio.popSection();
            m_audioCorrection.pushSection(audioSection);
        }
        m_pipelineStats.audioCorrectionTime +=
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - t0).count();

        while (m_audioCorrection.isReady()) {
            AudioSection audioSection = m_audioCorrection.popSection();
            if (m_noWavHeader) {
                m_writerRaw.write(audioSection);
            } else {
                m_writerWav.write(audioSection);
            }
            if (m_audacityLabels) {
                m_writerWavMetadata.write(audioSection);
            }
        }
    }
}

void EfmProcessor::drainDataPipeline()
{
    auto t0 = std::chrono::high_resolution_clock::now();
    while (m_data24ToRawSector.isReady()) {
        RawSector rawSector = m_data24ToRawSector.popSector();
        m_rawSectorToSector.pushSector(rawSector);
        rawSector.showData();
    }
    m_pipelineStats.rawSectorToSectorTime +=
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - t0).count();

    while (m_rawSectorToSector.isReady()) {
        Sector sector = m_rawSectorToSector.popSector();
        m_sectorCorrection.pushSector(sector);
    }

    while (m_sectorCorrection.isReady()) {
        Sector sector = m_sectorCorrection.popSector();
        m_writerSector.write(sector);
        if (m_outputMetadata) {
            m_writerSectorMetadata.write(sector);
        }
    }
}
// Statistics helpers
// ---------------------------------------------------------------------------

void EfmProcessor::showGeneralPipelineStatistics() const
{
    [[maybe_unused]] int64_t totalMs = (m_pipelineStats.channelToF3Time
                    + m_pipelineStats.f3ToF2Time
                    + m_pipelineStats.f2CorrectionTime) / 1000;
    ORC_LOG_INFO("Decoder processing summary (general):");
    ORC_LOG_INFO("  Channel to F3 processing time: {} ms", m_pipelineStats.channelToF3Time / 1000);
    ORC_LOG_INFO("  F3 to F2 section processing time: {} ms", m_pipelineStats.f3ToF2Time / 1000);
    ORC_LOG_INFO("  F2 correction processing time: {} ms", m_pipelineStats.f2CorrectionTime / 1000);
    ORC_LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)", totalMs, totalMs / 1000.0);
    ORC_LOG_INFO("");
}

void EfmProcessor::showD24PipelineStatistics() const
{
    [[maybe_unused]] int64_t totalMs = (m_pipelineStats.f2ToF1Time
                    + m_pipelineStats.f1ToData24Time) / 1000;
    ORC_LOG_INFO("Decoder processing summary (general):");
    ORC_LOG_INFO("  F2 to F1 processing time: {} ms", m_pipelineStats.f2ToF1Time / 1000);
    ORC_LOG_INFO("  F1 to Data24 processing time: {} ms", m_pipelineStats.f1ToData24Time / 1000);
    ORC_LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)", totalMs, totalMs / 1000.0);
    ORC_LOG_INFO("");
}

void EfmProcessor::showAudioPipelineStatistics() const
{
    [[maybe_unused]] int64_t totalMs = (m_pipelineStats.data24ToAudioTime
                    + m_pipelineStats.audioCorrectionTime) / 1000;
    ORC_LOG_INFO("Decoder processing summary (audio):");
    ORC_LOG_INFO("  Data24 to Audio processing time: {} ms", m_pipelineStats.data24ToAudioTime / 1000);
    ORC_LOG_INFO("  Audio correction processing time: {} ms", m_pipelineStats.audioCorrectionTime / 1000);
    ORC_LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)", totalMs, totalMs / 1000.0);
    ORC_LOG_INFO("");
}

void EfmProcessor::showDataPipelineStatistics() const
{
    [[maybe_unused]] int64_t totalMs = (m_pipelineStats.data24ToRawSectorTime
                    + m_pipelineStats.rawSectorToSectorTime) / 1000;
    ORC_LOG_INFO("Decoder processing summary (data):");
    ORC_LOG_INFO("  Data24 to Raw Sector processing time: {} ms", m_pipelineStats.data24ToRawSectorTime / 1000);
    ORC_LOG_INFO("  Raw Sector to Sector processing time: {} ms", m_pipelineStats.rawSectorToSectorTime / 1000);
    ORC_LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)", totalMs, totalMs / 1000.0);
    ORC_LOG_INFO("");
}

void EfmProcessor::showAllStatistics() const
{
    // General pipeline statistics (matches efm-decoder-f2 output)
    m_tValuesToChannel.showStatistics();
    ORC_LOG_INFO("");
    m_channelToF3.showStatistics();
    ORC_LOG_INFO("");
    m_f3FrameToF2Section.showStatistics();
    ORC_LOG_INFO("");
    m_f2SectionCorrection.showStatistics();
    ORC_LOG_INFO("");
    showGeneralPipelineStatistics();

    // D24 pipeline statistics (matches efm-decoder-d24 output)
    m_f2SectionToF1Section.showStatistics();
    ORC_LOG_INFO("");
    m_f1SectionToData24Section.showStatistics();
    ORC_LOG_INFO("");
    showD24PipelineStatistics();

    if (m_audioMode) {
        // Audio pipeline statistics (matches efm-decoder-audio output)
        m_data24ToAudio.showStatistics();
        ORC_LOG_INFO("");
        if (!m_noAudioConcealment) {
            m_audioCorrection.showStatistics();
            ORC_LOG_INFO("");
        }
        showAudioPipelineStatistics();
    } else {
        // Data pipeline statistics (matches efm-decoder-data output)
        m_data24ToRawSector.showStatistics();
        ORC_LOG_INFO("");
        m_rawSectorToSector.showStatistics();
        ORC_LOG_INFO("");
        m_sectorCorrection.showStatistics();
        ORC_LOG_INFO("");
        showDataPipelineStatistics();
    }

    // Overall wall-clock time
    auto endTime = std::chrono::high_resolution_clock::now();
    [[maybe_unused]] int64_t wallTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - m_startTime).count();
    ORC_LOG_INFO("Overall wall-clock time: {} ms ({:.2f} seconds)", wallTimeMs, wallTimeMs / 1000.0);
}
