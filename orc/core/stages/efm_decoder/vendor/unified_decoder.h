/*
 * File:        unified_decoder.h
 * Module:      efm-decoder
 * Purpose:     Unified EFM to Audio/Data decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef UNIFIED_DECODER_H
#define UNIFIED_DECODER_H

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <logging.h>
#include "decoder_config.h"

#include "dec_tvaluestochannel.h"
#include "dec_channeltof3frame.h"
#include "dec_f3frametof2section.h"
#include "dec_f2sectioncorrection.h"
#include "dec_f2sectiontof1section.h"
#include "dec_f1sectiontodata24section.h"
#include "reader_data.h"

#include "dec_data24toaudio.h"
#include "dec_audiocorrection.h"
#include "writer_wav.h"
#include "writer_raw.h"
#include "writer_wav_metadata.h"

#include "dec_data24torawsector.h"
#include "dec_rawsectortosector.h"
#include "dec_sectorcorrection.h"
#include "writer_sector.h"
#include "writer_sector_metadata.h"

// Main orchestration class for unified decoder
// Coordinates shared pipeline and mode-specific branches
class UnifiedDecoder {
public:
    explicit UnifiedDecoder(const DecoderConfig& config);

    struct RunStatistics {
        int64_t sharedChannelToF3TimeMs{0};
        int64_t sharedF3ToF2TimeMs{0};
        int64_t sharedF2CorrectionTimeMs{0};
        int64_t sharedF2ToF1TimeMs{0};
        int64_t sharedF1ToData24TimeMs{0};
        int64_t audioData24ToAudioTimeMs{0};
        int64_t audioCorrectionTimeMs{0};
        int64_t dataData24ToRawSectorTimeMs{0};
        int64_t dataRawSectorToSectorTimeMs{0};
        int64_t data24SectionCount{0};
        bool autoNoTimecodesEnabled{false};
        bool noTimecodesActive{false};
        std::string sharedDecodeStatisticsText;
        std::string modeDecodeStatisticsText;
    };

    using ProgressCallback = std::function<void(size_t current, size_t total, const std::string& message)>;
    
    // Run the complete decode process
    // Returns exit code (0 = success, 1 = error)
    int run();

    RunStatistics getRunStatistics() const;

    void setCancellationCallback(std::function<bool()> callback);
    void setProgressCallback(ProgressCallback callback);
    
private:
    DecoderConfig config_;

    struct SharedPipelineStatistics {
        int64_t channelToF3Time{0};
        int64_t f3ToF2Time{0};
        int64_t f2CorrectionTime{0};
        int64_t f2SectionToF1SectionTime{0};
        int64_t f1ToData24Time{0};
    } sharedPipelineStats_;

    struct AudioPipelineStatistics {
        int64_t data24ToAudioTime{0};
        int64_t audioCorrectionTime{0};
    } audioPipelineStats_;

    struct DataPipelineStatistics {
        int64_t data24ToRawSectorTime{0};
        int64_t rawSectorToSectorTime{0};
    } dataPipelineStats_;

    TvaluesToChannel tValuesToChannel_;
    ChannelToF3Frame channelToF3_;
    F3FrameToF2Section f3FrameToF2Section_;
    F2SectionCorrection f2SectionCorrection_;
    F2SectionToF1Section f2SectionToF1Section_;
    F1SectionToData24Section f1SectionToData24Section_;
    ReaderData readerData_;

    Data24ToAudio data24ToAudio_;
    AudioCorrection audioCorrection_;
    WriterWav writerWav_;
    WriterRaw writerRaw_;
    WriterWavMetadata writerWavMetadata_;

    Data24ToRawSector data24ToRawSector_;
    RawSectorToSector rawSectorToSector_;
    SectorCorrection sectorCorrection_;
    WriterSector writerSector_;
    WriterSectorMetadata writerSectorMetadata_;

    bool probeForNoTimecode();
    bool runSharedDecodePipeline(const std::function<void(const Data24Section&)>& onData24Section);
    bool runAudioBranch();
    bool runDataBranch();
    void processSharedPipeline(const std::function<void(const Data24Section&)>& onData24Section,
                               bool traceFrameOutput,
                               int64_t& data24SectionCount);
    void processAudioPipeline();
    void processDataPipeline(bool traceRawSectorOutput);
    void showSharedPipelineStatistics() const;
    std::string sharedPipelineStatisticsText() const;
    void showAudioPipelineStatistics() const;
    void showDataPipelineStatistics() const;
    std::string deriveAudioLabelsPath() const;
    std::string deriveDataMetadataPath() const;
    bool isCancellationRequested() const;
    void emitProgress(size_t current, size_t total, const std::string& message) const;

    std::function<bool()> cancellationCallback_;
    ProgressCallback progressCallback_;
    RunStatistics runStatistics_;
};

#endif // UNIFIED_DECODER_H
