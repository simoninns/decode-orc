/*
 * File:        efm_processor.h
 * Purpose:     efm-decoder - Unified EFM decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef EFM_PROCESSOR_H
#define EFM_PROCESSOR_H

#include <string>
#include <vector>
#include <chrono>
#include <cstdint>

#include "decoders.h"

// General pipeline decoders
#include "dec_tvaluestochannel.h"
#include "dec_channeltof3frame.h"
#include "dec_f3frametof2section.h"
#include "dec_f2sectioncorrection.h"

// D24 pipeline decoders
#include "dec_f2sectiontof1section.h"
#include "dec_f1sectiontodata24section.h"

// Audio pipeline decoders
#include "dec_data24toaudio.h"
#include "dec_audiocorrection.h"

// Data pipeline decoders
#include "dec_data24torawsector.h"
#include "dec_rawsectortosector.h"
#include "dec_sectorcorrection.h"

// Audio output writers
#include "writer_wav.h"
#include "writer_raw.h"
#include "writer_wav_metadata.h"

// Data output writers
#include "writer_sector.h"
#include "writer_sector_metadata.h"

class EfmProcessor
{
public:
    EfmProcessor();

    // Streaming API: feed t-values field-by-field without a temporary buffer.
    // Call beginStream() once, then pushChunk() for each field's samples,
    // then finishStream() to flush and finalise output.
    bool beginStream(const std::string &outputFilename, int64_t totalTValues = 0);
    void pushChunk(const std::vector<uint8_t> &chunk);
    bool finishStream();

    // Convenience wrapper: buffers all t-values then calls the streaming API.
    bool processFromBuffer(const std::vector<uint8_t> &tValues, const std::string &outputFilename);

    // Mode selection
    void setAudioMode(bool audioMode);   // true = audio (default), false = data

    // EFM options
    void setNoTimecodes(bool noTimecodes);

    // Audio options
    void setAudacityLabels(bool audacityLabels);
    void setNoAudioConcealment(bool noAudioConcealment);
    void setZeroPad(bool zeroPad);
    void setNoWavHeader(bool noWavHeader);

    // Data options
    void setOutputMetadata(bool outputMetadata);

    // Report options
    void setReportOutput(bool reportOutput);

    // Statistics output
    void showAllStatistics() const;

private:
    // -----------------------------------------------------------------------
    // Configuration flags
    // -----------------------------------------------------------------------
    bool m_audioMode;           // true = audio path, false = data path
    bool m_noTimecodes;
    bool m_audacityLabels;
    bool m_noAudioConcealment;
    bool m_zeroPad;
    bool m_noWavHeader;
    bool m_outputMetadata;
    bool m_reportOutput;

    // -----------------------------------------------------------------------
    // Pipeline decoder instances
    // -----------------------------------------------------------------------

    // General pipeline (EFM → F2 section)
    TvaluesToChannel     m_tValuesToChannel;
    ChannelToF3Frame     m_channelToF3;
    F3FrameToF2Section   m_f3FrameToF2Section;
    F2SectionCorrection  m_f2SectionCorrection;

    // D24 pipeline (F2 section → Data24 section)
    F2SectionToF1Section     m_f2SectionToF1Section;
    F1SectionToData24Section m_f1SectionToData24Section;

    // Audio pipeline (Data24 section → audio)
    Data24ToAudio   m_data24ToAudio;
    AudioCorrection m_audioCorrection;

    // Data pipeline (Data24 section → ECMA-130 sectors)
    Data24ToRawSector m_data24ToRawSector;
    RawSectorToSector m_rawSectorToSector;
    SectorCorrection  m_sectorCorrection;

    // -----------------------------------------------------------------------
    // I/O instances
    // -----------------------------------------------------------------------
    WriterWav           m_writerWav;
    WriterRaw           m_writerRaw;
    WriterWavMetadata   m_writerWavMetadata;
    WriterSector        m_writerSector;
    WriterSectorMetadata m_writerSectorMetadata;

    // -----------------------------------------------------------------------
    // Pipeline statistics
    // -----------------------------------------------------------------------
    struct AllPipelineStatistics {
        // General pipeline timing (µs)
        int64_t channelToF3Time{0};
        int64_t f3ToF2Time{0};
        int64_t f2CorrectionTime{0};

        // D24 pipeline timing (µs)
        int64_t f2ToF1Time{0};
        int64_t f1ToData24Time{0};

        // Audio pipeline timing (µs)
        int64_t data24ToAudioTime{0};
        int64_t audioCorrectionTime{0};

        // Data pipeline timing (µs)
        int64_t data24ToRawSectorTime{0};
        int64_t rawSectorToSectorTime{0};
    } m_pipelineStats;

    // Overall wall-clock start time
    std::chrono::high_resolution_clock::time_point m_startTime;

    // -----------------------------------------------------------------------
    // Streaming-API state (populated by beginStream / used by pushChunk+finishStream)
    // -----------------------------------------------------------------------
    std::string m_outputFilename;
    bool        m_zeroPadApplied{false};
    int64_t     m_totalTValues{0};
    int64_t     m_processedTValues{0};
    int         m_lastProgress{0};

    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------

    // Per-iteration pipeline drain helpers
    void drainPipeline(bool &zeroPadApplied);
    void drainAudioPipeline();
    void drainDataPipeline();

    void showGeneralPipelineStatistics() const;
    void showD24PipelineStatistics() const;
    void showAudioPipelineStatistics() const;
    void showDataPipelineStatistics() const;
};

#endif // EFM_PROCESSOR_H
