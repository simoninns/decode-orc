/*
 * File:        unified_decoder.cpp
 * Module:      efm-decoder
 * Purpose:     Unified EFM to Audio/Data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "unified_decoder.h"
#include <chrono>
#include <cctype>
#include <spdlog/spdlog.h>

#include "logging.h"

UnifiedDecoder::UnifiedDecoder(const DecoderConfig& config)
    : config_(config)
{
}

int UnifiedDecoder::run()
{
    if (config_.global.mode == DecoderMode::Audio) {
        return runAudioBranch() ? 0 : 1;
    }

    return runDataBranch() ? 0 : 1;
}

std::string UnifiedDecoder::deriveAudioLabelsPath() const
{
    const std::string& outputPath = config_.global.outputPath;
    if (outputPath.size() >= 4) {
        std::string suffix = outputPath.substr(outputPath.size() - 4);
        for (char& character : suffix) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        if (suffix == ".wav") {
            return outputPath.substr(0, outputPath.size() - 4) + ".txt";
        }
    }

    return outputPath + ".txt";
}

std::string UnifiedDecoder::deriveDataMetadataPath() const
{
    const std::string& outputPath = config_.global.outputPath;
    if (outputPath.size() >= 4) {
        std::string suffix = outputPath.substr(outputPath.size() - 4);
        for (char& character : suffix) {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        if (suffix == ".bin") {
            return outputPath.substr(0, outputPath.size() - 4) + ".bsm";
        }
    }

    return outputPath + ".bsm";
}

bool UnifiedDecoder::runAudioBranch()
{
    LOG_INFO("UnifiedDecoder::runAudioBranch(): Streaming Data24 sections to audio output");

    // Run probe to auto-detect no-timecode mode if needed
    // Skip probe if user explicitly set either --timecodes or --no-timecodes
    if (!config_.global.noTimecodes && !config_.global.forceTimecodes) {
        if (!probeForNoTimecode()) {
            return false;
        }
    }

    if (config_.audio.noWavHeader) {
        if (!writerRaw_.open(config_.global.outputPath)) {
            LOG_ERROR("UnifiedDecoder::runAudioBranch(): Failed to open raw output file: {}", config_.global.outputPath);
            return false;
        }
    } else {
        if (!writerWav_.open(config_.global.outputPath)) {
            LOG_ERROR("UnifiedDecoder::runAudioBranch(): Failed to open WAV output file: {}", config_.global.outputPath);
            return false;
        }
    }

    if (config_.audio.audacityLabels) {
        const std::string metadataPath = deriveAudioLabelsPath();
        if (!writerWavMetadata_.open(metadataPath, config_.audio.noAudioConcealment)) {
            if (writerWav_.isOpen()) {
                writerWav_.close();
            }
            if (writerRaw_.isOpen()) {
                writerRaw_.close();
            }
            LOG_ERROR("UnifiedDecoder::runAudioBranch(): Failed to open labels output file: {}", metadataPath);
            return false;
        }
        LOG_INFO("UnifiedDecoder::runAudioBranch(): Writing Audacity labels to {}", metadataPath);
    }

    bool zeroPadApplied = false;
    int64_t sectionCount = 0;

    auto onData24Section = [this, &zeroPadApplied, &sectionCount](const Data24Section& section) {
        if (config_.audio.zeroPad && !zeroPadApplied) {
            const int32_t requiredPadding = section.metadata.absoluteSectionTime().frames();
            if (requiredPadding > 0) {
                LOG_INFO("UnifiedDecoder::runAudioBranch(): Zero padding enabled, start time is {} and requires {} frames of padding",
                         section.metadata.absoluteSectionTime().toString(), requiredPadding);

                SectionTime currentTime(0, 0, 0);
                Data24Section zeroSection;
                zeroSection.metadata = section.metadata;
                zeroSection.metadata.setAbsoluteSectionTime(currentTime);
                zeroSection.metadata.setSectionTime(currentTime);

                for (int index = 0; index < 98; ++index) {
                    Data24 zeroData24;
                    zeroData24.setData(std::vector<uint8_t>(24, 0));
                    zeroData24.setErrorData(std::vector<bool>(24, false));
                    zeroData24.setPaddedData(std::vector<bool>(24, true));
                    zeroSection.pushFrame(zeroData24);
                }

                for (int32_t index = 0; index < requiredPadding; ++index) {
                    zeroSection.metadata.setAbsoluteSectionTime(currentTime);
                    zeroSection.metadata.setSectionTime(currentTime);

                    auto start = std::chrono::high_resolution_clock::now();
                    data24ToAudio_.pushSection(zeroSection);
                    auto elapsed = std::chrono::high_resolution_clock::now() - start;
                    audioPipelineStats_.data24ToAudioTime +=
                        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

                    processAudioPipeline();
                    currentTime++;
                }
            }

            zeroPadApplied = true;
        }

        auto start = std::chrono::high_resolution_clock::now();
        data24ToAudio_.pushSection(section);
        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        audioPipelineStats_.data24ToAudioTime +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

        processAudioPipeline();

        if ((sectionCount % 500) == 0) {
            LOG_INFO("Decoding Data24 Section {}", sectionCount);
        }

        ++sectionCount;
    };

    if (!runSharedDecodePipeline(onData24Section)) {
        if (writerWav_.isOpen()) {
            writerWav_.close();
        }
        if (writerRaw_.isOpen()) {
            writerRaw_.close();
        }
        if (writerWavMetadata_.isOpen()) {
            writerWavMetadata_.close();
        }
        return false;
    }

    LOG_INFO("Flushing audio decoding pipelines");
    if (!config_.audio.noAudioConcealment) {
        audioCorrection_.flush();
    }

    LOG_INFO("Processing final audio pipeline data");
    processAudioPipeline();

    data24ToAudio_.showStatistics();
    LOG_INFO("");
    if (!config_.audio.noAudioConcealment) {
        audioCorrection_.showStatistics();
        LOG_INFO("");
    }
    showAudioPipelineStatistics();

    if (writerWav_.isOpen()) {
        writerWav_.close();
    }
    if (writerRaw_.isOpen()) {
        writerRaw_.close();
    }
    if (writerWavMetadata_.isOpen()) {
        writerWavMetadata_.close();
    }

    LOG_INFO("Audio branch complete");
    return true;
}

bool UnifiedDecoder::runDataBranch()
{
    LOG_INFO("UnifiedDecoder::runDataBranch(): Streaming Data24 sections to data output");

    // Run probe to auto-detect no-timecode mode if needed
    // Skip probe if user explicitly set either --timecodes or --no-timecodes
    if (!config_.global.noTimecodes && !config_.global.forceTimecodes) {
        if (!probeForNoTimecode()) {
            return false;
        }
    }

    if (!writerSector_.open(config_.global.outputPath)) {
        LOG_ERROR("UnifiedDecoder::runDataBranch(): Failed to open data output file: {}", config_.global.outputPath);
        return false;
    }

    if (config_.data.outputMetadata) {
        const std::string metadataPath = deriveDataMetadataPath();
        if (!writerSectorMetadata_.open(metadataPath)) {
            if (writerSector_.isOpen()) {
                writerSector_.close();
            }
            LOG_ERROR("UnifiedDecoder::runDataBranch(): Failed to open metadata output file: {}", metadataPath);
            return false;
        }
        LOG_INFO("UnifiedDecoder::runDataBranch(): Writing bad-sector metadata to {}", metadataPath);
    }

    const bool traceRawSectorOutput =
        spdlog::default_logger_raw() != nullptr &&
        spdlog::default_logger_raw()->should_log(spdlog::level::trace);

    int64_t sectionCount = 0;
    auto onData24Section = [this, traceRawSectorOutput, &sectionCount](const Data24Section& section) {
        auto start = std::chrono::high_resolution_clock::now();
        data24ToRawSector_.pushSection(section);
        auto elapsed = std::chrono::high_resolution_clock::now() - start;
        dataPipelineStats_.data24ToRawSectorTime +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

        processDataPipeline(traceRawSectorOutput);

        if ((sectionCount % 500) == 0) {
            LOG_INFO("Decoding Data24 Section {}", sectionCount);
        }

        ++sectionCount;
    };

    if (!runSharedDecodePipeline(onData24Section)) {
        if (writerSector_.isOpen()) {
            writerSector_.close();
        }
        if (writerSectorMetadata_.isOpen()) {
            writerSectorMetadata_.close();
        }
        return false;
    }

    LOG_INFO("Flushing data decoding pipelines");

    LOG_INFO("Processing final data pipeline data");
    processDataPipeline(traceRawSectorOutput);

    data24ToRawSector_.showStatistics();
    LOG_INFO("");
    rawSectorToSector_.showStatistics();
    LOG_INFO("");
    sectorCorrection_.showStatistics();
    LOG_INFO("");
    showDataPipelineStatistics();

    if (writerSector_.isOpen()) {
        writerSector_.close();
    }
    if (writerSectorMetadata_.isOpen()) {
        writerSectorMetadata_.close();
    }

    LOG_INFO("Data branch complete");
    return true;
}

bool UnifiedDecoder::probeForNoTimecode()
{
    LOG_DEBUG("UnifiedDecoder::probeForNoTimecode(): Starting timecode probe");

    if (!readerData_.open(config_.global.inputPath)) {
        LOG_ERROR("UnifiedDecoder::probeForNoTimecode(): Failed to open input file: {}", config_.global.inputPath);
        return false;
    }

    // Create temporary pipeline components for probe
    TvaluesToChannel probeT2C;
    ChannelToF3Frame probeC2F3;
    F3FrameToF2Section probeF3toF2;
    F2SectionCorrection probeCorrectionStage;

    const int64_t totalSize = readerData_.size();
    int64_t processedSize = 0;
    const uint32_t probeMaxSections = 5000;  // Probe up to 5000 F2 sections
    uint32_t probeF2SectionCount = 0;
    constexpr uint32_t inputReadChunkSize = 64 * 1024;

    bool endOfData = false;

    // Process data through probe pipeline until we hit max sections or end of file
    while (!endOfData && probeF2SectionCount < probeMaxSections) {
        std::vector<uint8_t> tValues = readerData_.read(inputReadChunkSize);
        processedSize += static_cast<int64_t>(tValues.size());

        if (totalSize > 0 && processedSize > 20 * 1024 * 1024) {
            // Limit probe to ~20 MB max to keep overhead small
            break;
        }

        if (tValues.empty()) {
            endOfData = true;
        } else {
            probeT2C.pushFrame(tValues);
        }

        // Process through pipeline
        while (probeT2C.isReady()) {
            probeC2F3.pushFrame(probeT2C.popFrame());
        }

        while (probeC2F3.isReady()) {
            probeF3toF2.pushFrame(probeC2F3.popFrame());
        }

        while (probeF3toF2.isReady() && probeF2SectionCount < probeMaxSections) {
            F2Section f2Section = probeF3toF2.popSection();
            probeF2SectionCount++;

            // Record in probe stats
            if (f2Section.metadata.isValid()) {
                probeCorrectionStage.recordProbeSection(true, f2Section.metadata.absoluteSectionTime().frames());
            } else {
                probeCorrectionStage.recordProbeSection(false, -1);
            }
        }
    }

    readerData_.close();

    // Get probe statistics and decide
    TimecodeProbeStats probeStats = probeCorrectionStage.getProbeStats();
    bool shouldNoTimecodes = probeStats.shouldEnableNoTimecodes();

    LOG_DEBUG("UnifiedDecoder::probeForNoTimecode(): Probe complete");
    LOG_DEBUG("  Total sections probed: {}", probeStats.totalSections);
    LOG_DEBUG("  Valid metadata sections: {}", probeStats.validMetadataSections);
    LOG_DEBUG("  Longest contiguous run: {}", probeStats.longestContiguousRun);
    LOG_DEBUG("  Out-of-order sections: {}", probeStats.outOfOrderCount);
    LOG_DEBUG("  Large jump sections: {}", probeStats.largeJumpCount);
    LOG_DEBUG("  Decision: {}", shouldNoTimecodes ? "ENABLE no-timecodes mode" : "USE normal timecode mode");

    if (shouldNoTimecodes && !config_.global.noTimecodes) {
        LOG_WARN("No reliable Q-channel timecode detected in probe window; automatically enabling no-timecodes mode for this input.");
        config_.global.noTimecodes = true;
        return true;
    }

    return true;
}

bool UnifiedDecoder::runSharedDecodePipeline(const std::function<void(const Data24Section&)>& onData24Section)
{
    LOG_INFO("UnifiedDecoder::runSharedDecodePipeline(): Decoding EFM from file: {}", config_.global.inputPath);

    if (!readerData_.open(config_.global.inputPath)) {
        LOG_ERROR("UnifiedDecoder::runSharedDecodePipeline(): Failed to open input file: {}", config_.global.inputPath);
        return false;
    }

    f2SectionCorrection_.setNoTimecodes(config_.global.noTimecodes);

    const bool traceFrameOutput =
        spdlog::default_logger_raw() != nullptr &&
        spdlog::default_logger_raw()->should_log(spdlog::level::trace);

    const int64_t totalSize = readerData_.size();
    int64_t processedSize = 0;
    int lastProgress = 0;

    constexpr uint32_t inputReadChunkSize = 64 * 1024;

    bool endOfData = false;
    int64_t data24SectionCount = 0;
    while (!endOfData) {
        std::vector<uint8_t> tValues = readerData_.read(inputReadChunkSize);
        processedSize += static_cast<int64_t>(tValues.size());

        if (totalSize > 0) {
            const int progress = static_cast<int>((processedSize * 100) / totalSize);
            if (progress >= lastProgress + 5) {
                LOG_INFO("Progress: {}%", progress);
                lastProgress = progress;
            }
        }

        if (tValues.empty()) {
            endOfData = true;
        } else {
            tValuesToChannel_.pushFrame(tValues);
        }

        processSharedPipeline(onData24Section, traceFrameOutput, data24SectionCount);
    }

    LOG_INFO("Flushing shared decoding pipelines");
    f2SectionCorrection_.flush();

    LOG_INFO("Processing final shared pipeline data");
    processSharedPipeline(onData24Section, traceFrameOutput, data24SectionCount);

    if (!f2SectionCorrection_.isValid()) {
        LOG_WARN("Decoding FAILED");
        LOG_WARN("F2 Section Correction stage did not complete lead-in detection successfully.");
        LOG_WARN("This could be due to invalid input data or due to missing timecode information in the input EFM.");
        LOG_WARN("If you think the input EFM is valid - try running again with --no-timecodes.");
    } else {
        LOG_INFO("Shared decode complete");
    }

    tValuesToChannel_.showStatistics();
    LOG_INFO("");
    channelToF3_.showStatistics();
    LOG_INFO("");
    f3FrameToF2Section_.showStatistics();
    LOG_INFO("");
    f2SectionCorrection_.showStatistics();
    LOG_INFO("");
    f2SectionToF1Section_.showStatistics();
    LOG_INFO("");
    f1SectionToData24Section_.showStatistics();
    LOG_INFO("");
    showSharedPipelineStatistics();

    readerData_.close();

    LOG_INFO("Shared pipeline produced {} Data24 sections", data24SectionCount);
    return true;
}

void UnifiedDecoder::processSharedPipeline(const std::function<void(const Data24Section&)>& onData24Section,
                                           bool traceFrameOutput,
                                           int64_t& data24SectionCount)
{
    auto startTime = std::chrono::high_resolution_clock::now();

    while (tValuesToChannel_.isReady()) {
        const std::vector<uint8_t> channelData = tValuesToChannel_.popFrame();
        channelToF3_.pushFrame(channelData);
    }
    auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
    sharedPipelineStats_.channelToF3Time +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    startTime = std::chrono::high_resolution_clock::now();
    while (channelToF3_.isReady()) {
        const F3Frame f3Frame = channelToF3_.popFrame();
        f3FrameToF2Section_.pushFrame(f3Frame);
    }
    elapsed = std::chrono::high_resolution_clock::now() - startTime;
    sharedPipelineStats_.f3ToF2Time +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    startTime = std::chrono::high_resolution_clock::now();
    while (f3FrameToF2Section_.isReady()) {
        const F2Section section = f3FrameToF2Section_.popSection();
        f2SectionCorrection_.pushSection(section);
    }
    elapsed = std::chrono::high_resolution_clock::now() - startTime;
    sharedPipelineStats_.f2CorrectionTime +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    while (f2SectionCorrection_.isReady()) {
        const F2Section f2Section = f2SectionCorrection_.popSection();
        auto f2Start = std::chrono::high_resolution_clock::now();
        f2SectionToF1Section_.pushSection(f2Section);
        auto f2Elapsed = std::chrono::high_resolution_clock::now() - f2Start;
        sharedPipelineStats_.f2SectionToF1SectionTime +=
            std::chrono::duration_cast<std::chrono::nanoseconds>(f2Elapsed).count();
    }

    startTime = std::chrono::high_resolution_clock::now();
    while (f2SectionToF1Section_.isReady()) {
        F1Section f1Section = f2SectionToF1Section_.popSection();
        if (traceFrameOutput) {
            f1Section.showData();
        }
        f1SectionToData24Section_.pushSection(f1Section);
    }
    elapsed = std::chrono::high_resolution_clock::now() - startTime;
    sharedPipelineStats_.f1ToData24Time +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    while (f1SectionToData24Section_.isReady()) {
        Data24Section data24Section = f1SectionToData24Section_.popSection();
        if (traceFrameOutput) {
            data24Section.showData();
        }
        if (onData24Section) {
            onData24Section(data24Section);
        }
        ++data24SectionCount;
    }
}

void UnifiedDecoder::showSharedPipelineStatistics() const
{
    LOG_INFO("Decoder processing summary (shared):");
    LOG_INFO("  Channel to F3 processing time: {} ms", sharedPipelineStats_.channelToF3Time / 1000000);
    LOG_INFO("  F3 to F2 section processing time: {} ms", sharedPipelineStats_.f3ToF2Time / 1000000);
    LOG_INFO("  F2 correction processing time: {} ms", sharedPipelineStats_.f2CorrectionTime / 1000000);
    LOG_INFO("  F2 to F1 processing time: {} ms", sharedPipelineStats_.f2SectionToF1SectionTime / 1000000);
    LOG_INFO("  F1 to Data24 processing time: {} ms", sharedPipelineStats_.f1ToData24Time / 1000000);

    const int64_t totalProcessingTime =
        sharedPipelineStats_.channelToF3Time +
        sharedPipelineStats_.f3ToF2Time +
        sharedPipelineStats_.f2CorrectionTime +
        sharedPipelineStats_.f2SectionToF1SectionTime +
        sharedPipelineStats_.f1ToData24Time;
    const float totalProcessingTimeSeconds = totalProcessingTime / 1000000000.0f;

    LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)",
             totalProcessingTime / 1000000,
             totalProcessingTimeSeconds);
    LOG_INFO("");
}

void UnifiedDecoder::processAudioPipeline()
{
    if (config_.audio.noAudioConcealment) {
        while (data24ToAudio_.isReady()) {
            AudioSection audioSection = data24ToAudio_.popSection();
            if (config_.audio.noWavHeader) {
                writerRaw_.write(audioSection);
            } else {
                writerWav_.write(audioSection);
            }

            if (config_.audio.audacityLabels) {
                writerWavMetadata_.write(audioSection);
            }
        }
        return;
    }

    auto start = std::chrono::high_resolution_clock::now();
    while (data24ToAudio_.isReady()) {
        AudioSection audioSection = data24ToAudio_.popSection();
        audioCorrection_.pushSection(audioSection);
    }
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    audioPipelineStats_.audioCorrectionTime +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    while (audioCorrection_.isReady()) {
        AudioSection audioSection = audioCorrection_.popSection();
        if (config_.audio.noWavHeader) {
            writerRaw_.write(audioSection);
        } else {
            writerWav_.write(audioSection);
        }

        if (config_.audio.audacityLabels) {
            writerWavMetadata_.write(audioSection);
        }
    }
}

void UnifiedDecoder::processDataPipeline(bool traceRawSectorOutput)
{
    auto start = std::chrono::high_resolution_clock::now();
    while (data24ToRawSector_.isReady()) {
        RawSector rawSector = data24ToRawSector_.popSector();
        rawSectorToSector_.pushSector(rawSector);
        if (traceRawSectorOutput) {
            rawSector.showData();
        }
    }
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    dataPipelineStats_.rawSectorToSectorTime +=
        std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();

    while (rawSectorToSector_.isReady()) {
        Sector sector = rawSectorToSector_.popSector();
        sectorCorrection_.pushSector(sector);
    }

    while (sectorCorrection_.isReady()) {
        Sector sector = sectorCorrection_.popSector();
        writerSector_.write(sector);
        if (config_.data.outputMetadata) {
            writerSectorMetadata_.write(sector);
        }
    }
}

void UnifiedDecoder::showAudioPipelineStatistics() const
{
    LOG_INFO("Decoder processing summary (audio):");
    LOG_INFO("  Data24 to Audio processing time: {} ms", audioPipelineStats_.data24ToAudioTime / 1000000);
    LOG_INFO("  Audio correction processing time: {} ms", audioPipelineStats_.audioCorrectionTime / 1000000);

    const int64_t totalProcessingTime =
        audioPipelineStats_.data24ToAudioTime + audioPipelineStats_.audioCorrectionTime;
    const float totalProcessingTimeSeconds = totalProcessingTime / 1000000000.0f;
    LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)",
             totalProcessingTime / 1000000,
             totalProcessingTimeSeconds);
    LOG_INFO("");
}

void UnifiedDecoder::showDataPipelineStatistics() const
{
    LOG_INFO("Decoder processing summary (data):");
    LOG_INFO("  Data24 to Raw Sector processing time: {} ms", dataPipelineStats_.data24ToRawSectorTime / 1000000);
    LOG_INFO("  Raw Sector to Sector processing time: {} ms", dataPipelineStats_.rawSectorToSectorTime / 1000000);

    const int64_t totalProcessingTime =
        dataPipelineStats_.data24ToRawSectorTime + dataPipelineStats_.rawSectorToSectorTime;
    const float totalProcessingTimeSeconds = totalProcessingTime / 1000000000.0f;
    LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)",
             totalProcessingTime / 1000000,
             totalProcessingTimeSeconds);
    LOG_INFO("");
}
