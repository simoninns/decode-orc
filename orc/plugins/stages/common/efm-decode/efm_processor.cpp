/*
 * File:        efm_processor.cpp
 * Purpose:     efm-decoder - Unified EFM decoder
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "efm_processor.h"

#include <orc/stage/logging.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <string>
#include <utility>

EfmProcessor::EfmProcessor()
    : m_audioMode(true),
      m_noTimecodes(false),
      m_audacityLabels(false),
      m_noAudioConcealment(false),
      m_ignorePreemphasis(false),
      m_zeroPad(false),
      m_noWavHeader(false),
      m_outputMetadata(false),
      m_reportOutput(false) {}

EfmProcessor::~EfmProcessor() {
  // If beginStream() started the back-end thread but finishStream() was never
  // reached (e.g. the caller cancelled mid-stream and destroyed the processor,
  // or the front end threw), tear the thread down cleanly instead of letting a
  // still-joinable std::thread call std::terminate.
  if (m_sectionQueue) m_sectionQueue->abort();
  if (m_backEndThread.joinable()) m_backEndThread.join();
}

// ---------------------------------------------------------------------------
// Configuration setters
// ---------------------------------------------------------------------------

void EfmProcessor::setAudioMode(bool audioMode) { m_audioMode = audioMode; }

void EfmProcessor::setNoTimecodes(bool noTimecodes) {
  m_noTimecodes = noTimecodes;
}

void EfmProcessor::setAudacityLabels(bool audacityLabels) {
  m_audacityLabels = audacityLabels;
}

void EfmProcessor::setNoAudioConcealment(bool noAudioConcealment) {
  m_noAudioConcealment = noAudioConcealment;
}

void EfmProcessor::setIgnorePreemphasis(bool ignorePreemphasis) {
  m_ignorePreemphasis = ignorePreemphasis;
}

void EfmProcessor::setZeroPad(bool zeroPad) { m_zeroPad = zeroPad; }

void EfmProcessor::setNoWavHeader(bool noWavHeader) {
  m_noWavHeader = noWavHeader;
}

void EfmProcessor::setOutputMetadata(bool outputMetadata) {
  m_outputMetadata = outputMetadata;
}

void EfmProcessor::setReportOutput(bool reportOutput) {
  m_reportOutput = reportOutput;
}

void EfmProcessor::setReportFilename(const std::string& reportFilename) {
  m_reportFilename = reportFilename;
}

// ---------------------------------------------------------------------------
// Streaming API
// ---------------------------------------------------------------------------

bool EfmProcessor::beginStream(const std::string& outputFilename,
                               int64_t totalTValues) {
  m_outputFilename = outputFilename;
  m_zeroPadApplied = false;
  m_totalTValues = totalTValues;
  m_processedTValues = 0;
  m_lastProgress = 0;
  m_lastError.clear();

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
      if (dotPos != std::string::npos &&
          dotPos == labelsFilename.length() - 4) {
        labelsFilename = labelsFilename.substr(0, dotPos) + ".txt";
      } else {
        labelsFilename += ".txt";
      }
      m_writerWavMetadata.open(labelsFilename, m_noAudioConcealment,
                               !m_ignorePreemphasis);
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

  // P-9: start the section-level back-end thread. The front end runs on the
  // caller's thread (pushChunk) and feeds F2 sections through m_sectionQueue.
  m_backEndError = nullptr;
  m_sectionQueue =
      std::make_unique<BoundedQueue<F2Section>>(kSectionQueueCapacity);
  m_backEndThread = std::thread([this] { backEndLoop(); });

  return true;
}

void EfmProcessor::pushChunk(const std::vector<uint8_t>& chunk) {
  if (chunk.empty()) {
    return;
  }

  m_processedTValues += static_cast<int64_t>(chunk.size());

  // Log progress at 5 % intervals (based on caller-supplied total)
  if (m_totalTValues > 0) {
    int progress =
        static_cast<int>((m_processedTValues * 100) / m_totalTValues);
    if (progress >= m_lastProgress + 5) {
      ORC_LOG_INFO("Progress: {} %", progress);
      m_lastProgress = progress;
    }
  }

  m_tValuesToChannel.pushFrame(chunk);
  drainFrontEnd();
}

bool EfmProcessor::finishStream() {
  // Final front-end drain with no new input (mirrors the empty-chunk
  // end-of-data pass that the old buffer loop performed before breaking).
  drainFrontEnd();

  // Flush the front-end tail. F2SectionCorrection must be flushed here; its
  // output is enqueued for the back end, which will not flush its own tail
  // (F2SectionToF1Section / AudioCorrection) until every real section has been
  // consumed - preserving the original flush ordering across the thread split.
  ORC_LOG_INFO("Flushing decoding pipelines");
  m_f2SectionCorrection.flush();
  drainFrontEnd();

  // No more sections will be produced: close the hand-off queue and wait for
  // the back-end thread to drain it, flush its tail (E-7), and finish writing.
  ORC_LOG_INFO("Processing final pipeline data");
  m_sectionQueue->close();
  if (m_backEndThread.joinable()) m_backEndThread.join();

  // Re-raise any exception the back-end thread caught, on this thread, so the
  // efm::EfmDecodeError stage-boundary handler still catches it (R-1).
  if (m_backEndError) {
    std::exception_ptr e = m_backEndError;
    m_backEndError = nullptr;
    std::rethrow_exception(e);
  }

  // Validate and show statistics
  bool success = true;
  if (!m_f2SectionCorrection.isValid()) {
    success = false;

    // Build a message that names the actual failure so the user knows what to
    // do, rather than a bare "returned false".  The two diagnostic counters
    // distinguish "no EFM frame sync at all" from "decoded, but no usable
    // lead-in timecodes".
    const int32_t received = m_f2SectionCorrection.receivedSections();
    const int32_t validMeta = m_f2SectionCorrection.validMetadataSections();

    if (received == 0) {
      m_lastError =
          "EFM lead-in not found: no F2 sections could be decoded from the "
          "EFM data at all (frame sync failed). The EFM appears to be invalid, "
          "corrupt, or not actually EFM data.";
    } else if (validMeta == 0) {
      m_lastError =
          "EFM lead-in not found: sections were decoded but none carried valid "
          "subcode timecodes. This is normal for early CAV LaserDiscs that "
          "pre-date the EFM timecode specification - enable the 'No Timecodes' "
          "parameter on the EFM sink stage and try again.";
    } else {
      m_lastError =
          "EFM lead-in not found: no run of consecutive sections with valid, "
          "contiguous timecodes was located. The input EFM may be too short or "
          "too damaged near the start, or may lack ECMA-130 timecodes - if you "
          "believe the EFM is valid, enable the 'No Timecodes' parameter on "
          "the "
          "EFM sink stage and try again.";
    }

    ORC_LOG_WARN("Decoding FAILED: {}", m_lastError);
  } else {
    ORC_LOG_INFO("Decoding complete");

    if (m_reportOutput) {
      std::string reportFilename = m_reportFilename.empty()
                                       ? m_outputFilename + ".txt"
                                       : m_reportFilename;
      ORC_LOG_INFO("Writing decoding report to: {}", reportFilename);
      auto reportSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
          reportFilename, true);
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
  if (m_writerWav.isOpen()) m_writerWav.close();
  if (m_writerRaw.isOpen()) m_writerRaw.close();
  if (m_writerWavMetadata.isOpen()) m_writerWavMetadata.close();
  if (m_writerSector.isOpen()) m_writerSector.close();
  if (m_writerSectorMetadata.isOpen()) m_writerSectorMetadata.close();

  return success;
}

// ---------------------------------------------------------------------------
// Internal pipeline helpers
// ---------------------------------------------------------------------------

bool EfmProcessor::drainFrontEnd() {
  // -----------------------------------------------------------------------
  // Front end: T-values → Channel → F3 → F2Section → F2SectionCorrection
  // -----------------------------------------------------------------------

  auto t0 = std::chrono::high_resolution_clock::now();
  while (m_tValuesToChannel.isReady()) {
    m_channelToF3.pushFrame(m_tValuesToChannel.popFrame());
  }
  m_pipelineStats.channelToF3Time +=
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - t0)
          .count();

  t0 = std::chrono::high_resolution_clock::now();
  while (m_channelToF3.isReady()) {
    F3Frame f3Frame = m_channelToF3.popFrame();
    // P-1: move the frame into the next stage to avoid a deep copy.
    m_f3FrameToF2Section.pushFrame(std::move(f3Frame));
  }
  m_pipelineStats.f3ToF2Time +=
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - t0)
          .count();

  t0 = std::chrono::high_resolution_clock::now();
  while (m_f3FrameToF2Section.isReady()) {
    // P-1: move the section into the next stage (F2SectionCorrection has an
    // rvalue pushSection overload) to avoid a whole-section deep copy.
    m_f2SectionCorrection.pushSection(m_f3FrameToF2Section.popSection());
  }
  m_pipelineStats.f2CorrectionTime +=
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - t0)
          .count();

  // Hand every completed F2 section to the back-end thread. push() blocks while
  // the queue is full (back-pressure) and returns false only if the back end
  // has aborted (error / cancel), in which case we stop feeding - the stored
  // error is re-raised from finishStream().
  while (m_f2SectionCorrection.isReady()) {
    if (!m_sectionQueue->push(m_f2SectionCorrection.popSection())) {
      return false;
    }
  }
  return true;
}

void EfmProcessor::backEndLoop() {
  // Runs on m_backEndThread. Any decoder exception is captured and re-raised on
  // the caller's thread in finishStream() (R-1); it must never escape here.
  try {
    // -------------------------------------------------------------------
    // Back end: F2Section → F2SectionToF1Section → F1SectionToData24Section
    // → audio / data pipeline → writers.
    // -------------------------------------------------------------------
    F2Section f2Section;
    while (m_sectionQueue->pop(f2Section)) {
      auto t0 = std::chrono::high_resolution_clock::now();
      // P-1: move the section into the next stage to avoid a deep copy.
      m_f2SectionToF1Section.pushSection(std::move(f2Section));
      m_pipelineStats.f2ToF1Time +=
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::high_resolution_clock::now() - t0)
              .count();
      drainBackEnd(m_zeroPadApplied);
    }

    // Queue closed and drained: every real F2 section has now been pushed into
    // F2SectionToF1Section. E-7: recover the CIRC delay-line tail - the newest
    // ~111 genuine F2 frames are still held inside its delay lines; flush
    // pushes padding through the chain to carry that trapped tail out as F1
    // sections.
    m_f2SectionToF1Section.flush();
    drainBackEnd(m_zeroPadApplied);

    // Now all Data24 sections have been pushed into AudioCorrection; flush it
    // to release its internal lookahead buffer, then drain once more.
    if (m_audioMode && !m_noAudioConcealment) {
      m_audioCorrection.flush();
      drainAudioPipeline();
    }
  } catch (...) {
    m_backEndError = std::current_exception();
    // Wake any front-end producer blocked on a full queue so finishStream()
    // (or the destructor) can join without deadlocking.
    m_sectionQueue->abort();
  }
}

void EfmProcessor::drainBackEnd(bool& zeroPadApplied) {
  auto t0 = std::chrono::high_resolution_clock::now();
  while (m_f2SectionToF1Section.isReady()) {
    F1Section f1Section = m_f2SectionToF1Section.popSection();
    f1Section.showData();
    // P-1: move the section into the next stage to avoid a deep copy.
    m_f1SectionToData24Section.pushSection(std::move(f1Section));
  }
  m_pipelineStats.f1ToData24Time +=
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - t0)
          .count();

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
        int32_t requiredPadding =
            data24Section.metadata.absoluteSectionTime().frames();
        if (requiredPadding > 0) {
          ORC_LOG_INFO(
              "Zero padding enabled, start time is {} and requires {} frames "
              "of padding",
              data24Section.metadata.absoluteSectionTime().toString(),
              requiredPadding);

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
                    std::chrono::high_resolution_clock::now() - pad_t0)
                    .count();
            drainAudioPipeline();
            ++zeroTime;
          }
        }
      }

      auto audio_t0 = std::chrono::high_resolution_clock::now();
      // P-1: last use of data24Section on this branch - move it in.
      m_data24ToAudio.pushSection(std::move(data24Section));
      m_pipelineStats.data24ToAudioTime +=
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::high_resolution_clock::now() - audio_t0)
              .count();
      drainAudioPipeline();

    } else {
      auto data_t0 = std::chrono::high_resolution_clock::now();
      // P-1: last use of data24Section on this branch - move it in.
      m_data24ToRawSector.pushSection(std::move(data24Section));
      m_pipelineStats.data24ToRawSectorTime +=
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::high_resolution_clock::now() - data_t0)
              .count();
      drainDataPipeline();
    }
  }
}

void EfmProcessor::drainAudioPipeline() {
  if (m_noAudioConcealment) {
    // Bypass correction — write decoded audio directly
    while (m_data24ToAudio.isReady()) {
      AudioSection audioSection = m_data24ToAudio.popSection();
      // Q-8: undo 50/15 us pre-emphasis before writing (unless disabled).
      if (!m_ignorePreemphasis) m_audioDeemphasis.applySection(audioSection);
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
      // P-1: move the section into correction to avoid a deep copy.
      m_audioCorrection.pushSection(std::move(audioSection));
    }
    m_pipelineStats.audioCorrectionTime +=
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now() - t0)
            .count();

    while (m_audioCorrection.isReady()) {
      AudioSection audioSection = m_audioCorrection.popSection();
      // Q-8: undo 50/15 us pre-emphasis before writing (unless disabled).
      if (!m_ignorePreemphasis) m_audioDeemphasis.applySection(audioSection);
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

void EfmProcessor::drainDataPipeline() {
  auto t0 = std::chrono::high_resolution_clock::now();
  while (m_data24ToRawSector.isReady()) {
    RawSector rawSector = m_data24ToRawSector.popSector();
    m_rawSectorToSector.pushSector(rawSector);
    rawSector.showData();
  }
  m_pipelineStats.rawSectorToSectorTime +=
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::high_resolution_clock::now() - t0)
          .count();

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

void EfmProcessor::showGeneralPipelineStatistics() const {
  [[maybe_unused]] int64_t totalMs =
      (m_pipelineStats.channelToF3Time + m_pipelineStats.f3ToF2Time +
       m_pipelineStats.f2CorrectionTime) /
      1000;
  ORC_LOG_INFO("Decoder processing summary (general):");
  ORC_LOG_INFO("  Channel to F3 processing time: {} ms",
               m_pipelineStats.channelToF3Time / 1000);
  ORC_LOG_INFO("  F3 to F2 section processing time: {} ms",
               m_pipelineStats.f3ToF2Time / 1000);
  ORC_LOG_INFO("  F2 correction processing time: {} ms",
               m_pipelineStats.f2CorrectionTime / 1000);
  ORC_LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)", totalMs,
               totalMs / 1000.0);
  ORC_LOG_INFO("");
}

void EfmProcessor::showD24PipelineStatistics() const {
  [[maybe_unused]] int64_t totalMs =
      (m_pipelineStats.f2ToF1Time + m_pipelineStats.f1ToData24Time) / 1000;
  ORC_LOG_INFO("Decoder processing summary (general):");
  ORC_LOG_INFO("  F2 to F1 processing time: {} ms",
               m_pipelineStats.f2ToF1Time / 1000);
  ORC_LOG_INFO("  F1 to Data24 processing time: {} ms",
               m_pipelineStats.f1ToData24Time / 1000);
  ORC_LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)", totalMs,
               totalMs / 1000.0);
  ORC_LOG_INFO("");
}

void EfmProcessor::showAudioPipelineStatistics() const {
  [[maybe_unused]] int64_t totalMs = (m_pipelineStats.data24ToAudioTime +
                                      m_pipelineStats.audioCorrectionTime) /
                                     1000;
  ORC_LOG_INFO("Decoder processing summary (audio):");
  ORC_LOG_INFO("  Data24 to Audio processing time: {} ms",
               m_pipelineStats.data24ToAudioTime / 1000);
  ORC_LOG_INFO("  Audio correction processing time: {} ms",
               m_pipelineStats.audioCorrectionTime / 1000);
  ORC_LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)", totalMs,
               totalMs / 1000.0);
  ORC_LOG_INFO("");
}

void EfmProcessor::showDataPipelineStatistics() const {
  [[maybe_unused]] int64_t totalMs = (m_pipelineStats.data24ToRawSectorTime +
                                      m_pipelineStats.rawSectorToSectorTime) /
                                     1000;
  ORC_LOG_INFO("Decoder processing summary (data):");
  ORC_LOG_INFO("  Data24 to Raw Sector processing time: {} ms",
               m_pipelineStats.data24ToRawSectorTime / 1000);
  ORC_LOG_INFO("  Raw Sector to Sector processing time: {} ms",
               m_pipelineStats.rawSectorToSectorTime / 1000);
  ORC_LOG_INFO("  Total processing time: {} ms ({:.2f} seconds)", totalMs,
               totalMs / 1000.0);
  ORC_LOG_INFO("");
}

// ---------------------------------------------------------------------------
// Curated decode report (Parts A-E)
// ---------------------------------------------------------------------------
//
// The report is written line-by-line via ORC_LOG_INFO (the report sink uses a
// raw "%v" pattern). Part A/B/C are the user-facing summary, disc contents and
// quality figures; Part D is the raw per-stage diagnostics; Part E is timing.
// The most decision-relevant information is deliberately at the top.

namespace {

// Repeat a (possibly multi-byte UTF-8) unit string `count` times.
std::string repeatStr(const char* unit, int count) {
  std::string s;
  for (int i = 0; i < count; ++i) s += unit;
  return s;
}

// Group an integer with thousands separators: 1234567 -> "1,234,567".
std::string commas(uint64_t value) {
  std::string digits = std::to_string(value);
  std::string out;
  int count = 0;
  for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
    if (count != 0 && count % 3 == 0) out.push_back(',');
    out.push_back(*it);
    ++count;
  }
  std::reverse(out.begin(), out.end());
  return out;
}

template <typename N, typename D>
double percentOf(N numerator, D denominator) {
  const double d = static_cast<double>(denominator);
  return d > 0.0 ? (static_cast<double>(numerator) * 100.0 / d) : 0.0;
}

template <typename N, typename D>
std::string fmtPercent(N numerator, D denominator, int precision) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.*f %%", precision,
                percentOf(numerator, denominator));
  return buf;
}

std::string fmtBytes(uint64_t bytes) {
  char buf[48];
  double b = static_cast<double>(bytes);
  if (b < 1024.0) {
    std::snprintf(buf, sizeof(buf), "%llu B",
                  static_cast<unsigned long long>(bytes));
  } else if (b < 1024.0 * 1024.0) {
    std::snprintf(buf, sizeof(buf), "%.2f KB", b / 1024.0);
  } else if (b < 1024.0 * 1024.0 * 1024.0) {
    std::snprintf(buf, sizeof(buf), "%.2f MB", b / (1024.0 * 1024.0));
  } else {
    std::snprintf(buf, sizeof(buf), "%.2f GB", b / (1024.0 * 1024.0 * 1024.0));
  }
  return buf;
}

// Format a raw 12-character ISRC as CC-OOO-YY-NNNNN; "-" if blank.
std::string fmtIsrc(const std::string& raw) {
  if (raw.empty()) return "-";
  if (raw.size() != 12) return raw;
  return raw.substr(0, 2) + "-" + raw.substr(2, 3) + "-" + raw.substr(5, 2) +
         "-" + raw.substr(7, 5);
}

// Place an ASCII string into a fixed-width field. Table cell contents are all
// ASCII, so byte width equals display width here.
std::string cell(const std::string& content, int width, char align) {
  std::string s = content;
  if (static_cast<int>(s.size()) > width) s = s.substr(0, width);
  int pad = width - static_cast<int>(s.size());
  if (align == 'r') return std::string(pad, ' ') + s;
  if (align == 'l') return s + std::string(pad, ' ');
  int left = pad / 2;  // centre
  return std::string(left, ' ') + s + std::string(pad - left, ' ');
}

// 80-column double rule and centred title used for the report head/foot.
std::string ruleDouble() { return repeatStr("═", 80); }
std::string centred(const std::string& text) {
  int pad = 80 - static_cast<int>(text.size());
  if (pad <= 0) return text;
  return std::string(pad / 2, ' ') + text;
}

// Part banner (ASCII title so the 78-wide interior stays byte-aligned).
std::string bannerTop() { return "╔" + repeatStr("═", 78) + "╗"; }
std::string bannerBottom() { return "╚" + repeatStr("═", 78) + "╝"; }
std::string bannerMid(const std::string& title) {
  return "║" + cell("  " + title, 78, 'l') + "║";
}

// Track-table geometry (interior column widths).
constexpr int kW_Trk = 5;
constexpr int kW_Time = 12;
constexpr int kW_Type = 9;
constexpr int kW_Pre = 5;
constexpr int kW_Cpy = 6;
constexpr int kW_Isrc = 17;

std::string trackBorder(const char* left, const char* mid, const char* right) {
  const int widths[] = {kW_Trk,  kW_Time, kW_Time, kW_Time,
                        kW_Type, kW_Pre,  kW_Cpy,  kW_Isrc};
  std::string s = left;
  for (int i = 0; i < 8; ++i) {
    s += repeatStr("─", widths[i]);
    s += (i < 7) ? mid : right;
  }
  return s;
}

std::string trackRow(const std::string& trk, const std::string& start,
                     const std::string& end, const std::string& dur,
                     const std::string& type, const std::string& pre,
                     const std::string& cpy, const std::string& isrc) {
  return "│" + cell(trk, kW_Trk, 'c') + "│" + cell(start, kW_Time, 'c') + "│" +
         cell(end, kW_Time, 'c') + "│" + cell(dur, kW_Time, 'c') + "│" +
         cell(" " + type, kW_Type, 'l') + "│" + cell(pre, kW_Pre, 'c') + "│" +
         cell(cpy, kW_Cpy, 'c') + "│" + cell(" " + isrc, kW_Isrc, 'l') + "│";
}

}  // namespace

void EfmProcessor::showReportHeader() const {
  std::time_t now = std::time(nullptr);
  char timeBuf[32] = {0};
  if (const std::tm* tmv = std::localtime(&now)) {
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tmv);
  }
  const std::string reportFile =
      m_reportFilename.empty() ? m_outputFilename + ".txt" : m_reportFilename;

  ORC_LOG_INFO("{}", ruleDouble());
  ORC_LOG_INFO("{}", centred("E F M   D E C O D E   R E P O R T"));
  ORC_LOG_INFO("{}", ruleDouble());
  ORC_LOG_INFO("  Generated       : {}", timeBuf);
  ORC_LOG_INFO("  Output file     : {}", m_outputFilename);
  ORC_LOG_INFO("  Report file     : {}", reportFile);
  ORC_LOG_INFO("  Decode mode     : {}",
               m_audioMode ? "AUDIO (CD-DA)" : "DATA (CD-ROM)");
  ORC_LOG_INFO("  Result          : SUCCESS");
  ORC_LOG_INFO("{}", ruleDouble());
  ORC_LOG_INFO("");
}

void EfmProcessor::showSummary() const {
  const F2SectionCorrection& f2 = m_f2SectionCorrection;

  ORC_LOG_INFO("{}", bannerTop());
  ORC_LOG_INFO("{}", bannerMid("PART A - DECODE SUMMARY"));
  ORC_LOG_INFO("{}", bannerBottom());
  ORC_LOG_INFO("");

  // Duration and track count.
  std::string duration = "N/A";
  if (f2.totalSections() > 0 &&
      f2.absoluteEndTime() >= f2.absoluteStartTime()) {
    duration = (f2.absoluteEndTime() - f2.absoluteStartTime()).toString();
  }

  // Grade + concealment.
  const bool concealmentRan = m_audioMode && !m_noAudioConcealment;
  const uint64_t concealed = m_audioCorrection.concealedSamples();
  const uint64_t silenced = m_audioCorrection.silencedSamples();
  const uint64_t totalMono =
      m_audioCorrection.validSamples() + concealed + silenced;
  const double concealPct = percentOf(concealed + silenced, totalMono);

  const uint64_t totalBytes = m_f1SectionToData24Section.totalBytes();
  const uint64_t corruptBytes = m_f1SectionToData24Section.corruptBytes();
  const double dataLossPct = percentOf(corruptBytes, totalBytes);

  std::string grade;
  if (m_audioMode) {
    if (concealPct == 0.0 && f2.missingSections() == 0 &&
        m_f2SectionToF1Section.errorC2s() == 0) {
      grade = "EXCELLENT";
    } else if (concealPct < 0.10) {
      grade = "GOOD";
    } else if (concealPct < 1.0) {
      grade = "FAIR";
    } else {
      grade = "POOR";
    }
    if (!concealmentRan) grade += " (concealment disabled)";
  } else {
    if (dataLossPct == 0.0 && m_rawSectorToSector.invalidSectors() == 0) {
      grade = "EXCELLENT";
    } else if (dataLossPct < 0.10) {
      grade = "GOOD";
    } else if (dataLossPct < 1.0) {
      grade = "FAIR";
    } else {
      grade = "POOR";
    }
  }

  // Q-channel timecode status.
  std::string timecodes;
  if (m_noTimecodes) {
    timecodes = "Not used (No Timecodes mode)";
  } else if (f2.validMetadataSections() == 0) {
    timecodes = "None found";
  } else {
    timecodes = (f2.outOfOrderSections() == 0) ? "Valid and contiguous"
                                               : "Valid (with discontinuities)";
  }

  ORC_LOG_INFO("  Overall assessment : {}", grade);
  ORC_LOG_INFO("  Disc duration      : {}   ({} sections)", duration,
               commas(f2.totalSections()));
  ORC_LOG_INFO("  Tracks recovered   : {}", f2.trackNumbers().size());
  ORC_LOG_INFO("  Q-channel timecodes: {}", timecodes);
  ORC_LOG_INFO("");

  if (m_audioMode) {
    ORC_LOG_INFO("  Audio integrity");
    if (concealmentRan) {
      ORC_LOG_INFO("    Samples decoded    : {}", commas(totalMono));
      ORC_LOG_INFO("    Concealed (interp) : {}   ({})", commas(concealed),
                   fmtPercent(concealed, totalMono, 4));
      ORC_LOG_INFO("    Silenced (muted)   : {}   ({})", commas(silenced),
                   fmtPercent(silenced, totalMono, 4));
    } else {
      ORC_LOG_INFO("    Concealment        : disabled (audio emitted as-is)");
    }
  } else {
    const uint32_t good = m_rawSectorToSector.validSectors();
    const uint32_t corrected = m_rawSectorToSector.correctedSectors();
    const uint32_t bad = m_rawSectorToSector.invalidSectors();
    ORC_LOG_INFO("  Sector recovery");
    ORC_LOG_INFO("    Good / corrected / uncorrectable : {} / {} / {}",
                 commas(good), commas(corrected), commas(bad));
  }
  ORC_LOG_INFO("");

  ORC_LOG_INFO("  Error-correction health");
  ORC_LOG_INFO("    C1 uncorrectable   : {}",
               fmtPercent(m_f2SectionToF1Section.errorC1s(),
                          m_f2SectionToF1Section.validC1s() +
                              m_f2SectionToF1Section.fixedC1s() +
                              m_f2SectionToF1Section.errorC1s(),
                          4));
  ORC_LOG_INFO("    C2 uncorrectable   : {}",
               fmtPercent(m_f2SectionToF1Section.errorC2s(),
                          m_f2SectionToF1Section.validC2s() +
                              m_f2SectionToF1Section.fixedC2s() +
                              m_f2SectionToF1Section.errorC2s(),
                          4));
  ORC_LOG_INFO("    Data loss (post-C2): {}",
               fmtPercent(corruptBytes, totalBytes, 4));
  ORC_LOG_INFO("    Sections missing   : {}   (reconstructed by interpolation)",
               commas(f2.missingSections()));
  ORC_LOG_INFO("    Sections out-of-order / uncorrectable : {} / {}",
               commas(f2.outOfOrderSections()),
               commas(f2.uncorrectableSections()));
  ORC_LOG_INFO("");

  // Pre-emphasis track list and copy-protection summary.
  const auto& trackNumbers = f2.trackNumbers();
  const auto& preemphasis = f2.trackPreemphasis();
  const auto& copyProhibited = f2.trackCopyProhibited();
  std::string preTracks;
  for (size_t i = 0; i < trackNumbers.size(); ++i) {
    if (preemphasis[i]) {
      if (!preTracks.empty()) preTracks += ", ";
      preTracks += std::to_string(trackNumbers[i]);
    }
  }
  bool anyProhibited = false, anyPermitted = false;
  for (bool prohibited : copyProhibited) {
    if (prohibited) {
      anyProhibited = true;
    } else {
      anyPermitted = true;
    }
  }
  std::string copyStr = "N/A";
  if (anyProhibited && anyPermitted) {
    copyStr = "Mixed (see track table)";
  } else if (anyProhibited) {
    copyStr = "Prohibited (all tracks)";
  } else if (anyPermitted) {
    copyStr = "Permitted (all tracks)";
  }

  size_t isrcCount = 0;
  for (const std::string& code : f2.trackIsrc()) {
    if (!code.empty()) ++isrcCount;
  }

  ORC_LOG_INFO("  Q-channel highlights");
  if (preTracks.empty()) {
    ORC_LOG_INFO("    Pre-emphasis       : None");
  } else {
    ORC_LOG_INFO("    Pre-emphasis       : PRESENT on tracks {}  ({})",
                 preTracks,
                 m_ignorePreemphasis ? "flag ignored, no de-emphasis"
                                     : "50/15us de-emphasis applied");
  }
  ORC_LOG_INFO("    Copy protection    : {}", copyStr);
  ORC_LOG_INFO("    Catalogue number   : {}",
               f2.catalogueNumber().empty() ? "none" : f2.catalogueNumber());
  if (isrcCount > 0) {
    ORC_LOG_INFO("    ISRC codes         : {} recovered   (see track table)",
                 isrcCount);
  } else {
    ORC_LOG_INFO("    ISRC codes         : none");
  }
  ORC_LOG_INFO("");

  // Warnings.
  std::vector<std::string> warnings;
  if (f2.missingSections() > 0) {
    warnings.push_back(
        commas(f2.missingSections()) +
        " section(s) were missing and reconstructed by interpolation.");
  }
  if (f2.outOfOrderSections() > 0) {
    warnings.push_back(commas(f2.outOfOrderSections()) +
                       " section(s) arrived out of order.");
  }
  if (f2.uncorrectableSections() > 0) {
    warnings.push_back(commas(f2.uncorrectableSections()) +
                       " section(s) were uncorrectable.");
  }
  if (!preTracks.empty()) {
    warnings.push_back(
        std::string("Pre-emphasis flagged on track(s) ") + preTracks + "; " +
        (m_ignorePreemphasis
             ? "flag ignored, raw pre-emphasised signal kept."
             : "a 50/15us de-emphasis filter was applied to the output."));
  }
  if (m_audioMode && concealmentRan && silenced > 0) {
    warnings.push_back(commas(silenced) +
                       " audio sample(s) were muted (unrecoverable).");
  }

  ORC_LOG_INFO("  Warnings");
  if (warnings.empty()) {
    ORC_LOG_INFO("    None.");
  } else {
    for (const std::string& warning : warnings) {
      ORC_LOG_INFO("    ! {}", warning);
    }
  }
  ORC_LOG_INFO("");

  ORC_LOG_INFO(
      "  Assessment scale:  EXCELLENT  no concealment, no missing sections, C2 "
      "clean");
  if (m_audioMode) {
    ORC_LOG_INFO(
        "                     GOOD       concealment < 0.10 %, few missing "
        "sections");
    ORC_LOG_INFO("                     FAIR       concealment < 1.0 %");
    ORC_LOG_INFO(
        "                     POOR       concealment >= 1.0 % or many "
        "uncorrectable");
  } else {
    ORC_LOG_INFO("                     GOOD       data loss < 0.10 %");
    ORC_LOG_INFO("                     FAIR       data loss < 1.0 %");
    ORC_LOG_INFO(
        "                     POOR       data loss >= 1.0 % or uncorrectable "
        "sectors");
  }
  ORC_LOG_INFO("");
}

void EfmProcessor::showDiscContents() const {
  const F2SectionCorrection& f2 = m_f2SectionCorrection;

  ORC_LOG_INFO("{}", bannerTop());
  ORC_LOG_INFO("{}", bannerMid("PART B - DISC CONTENTS (Q-channel)"));
  ORC_LOG_INFO("{}", bannerBottom());
  ORC_LOG_INFO("");

  ORC_LOG_INFO("  Absolute time span : {}  ->  {}",
               f2.absoluteStartTime().toString(),
               f2.absoluteEndTime().toString());
  ORC_LOG_INFO("");

  ORC_LOG_INFO("  Q-channel mode distribution (sections)");
  ORC_LOG_INFO("    Mode 1  CD position / time      : {}",
               commas(f2.qmode1Sections()));
  ORC_LOG_INFO("    Mode 2  Media catalogue number  : {}",
               commas(f2.qmode2Sections()));
  ORC_LOG_INFO("    Mode 3  ISRC (ISO 3901)         : {}",
               commas(f2.qmode3Sections()));
  ORC_LOG_INFO("    Mode 4  LaserDisc data          : {}",
               commas(f2.qmode4Sections()));
  ORC_LOG_INFO("");

  ORC_LOG_INFO("  Media catalogue number (UPC/EAN) : {}",
               f2.catalogueNumber().empty() ? "none" : f2.catalogueNumber());
  ORC_LOG_INFO("");

  // Q-6: lead-in table of contents (POINT/PMIN/PSEC/PFRAME, IEC 60908 §17.5.1).
  ORC_LOG_INFO("  Lead-in table of contents (Q-channel)");
  if (!f2.hasToc()) {
    ORC_LOG_INFO(
        "    Not available (capture does not include a decodable lead-in).");
  } else {
    ORC_LOG_INFO("    First track    : {}", f2.tocFirstTrack());
    ORC_LOG_INFO("    Last track     : {}", f2.tocLastTrack());
    ORC_LOG_INFO("    Lead-out start : {}", f2.tocLeadOutStart().toString());
    const auto& tocTracks = f2.tocTrackNumbers();
    const auto& tocStarts = f2.tocTrackStartTimes();
    if (tocTracks.empty()) {
      ORC_LOG_INFO("    (no per-track TOC entries decoded)");
    } else {
      for (size_t i = 0; i < tocTracks.size(); ++i) {
        ORC_LOG_INFO("    Track {:>2} start : {}", tocTracks[i],
                     tocStarts[i].toString());
      }
    }
  }
  ORC_LOG_INFO("");

  ORC_LOG_INFO("  Track table");
  const auto& trackNumbers = f2.trackNumbers();
  if (trackNumbers.empty()) {
    ORC_LOG_INFO("    (no user tracks decoded)");
  } else {
    const auto& absStart = f2.trackAbsStartTimes();
    const auto& absEnd = f2.trackAbsEndTimes();
    const auto& preemphasis = f2.trackPreemphasis();
    const auto& preemphasisVaried = f2.trackPreemphasisVaried();
    const auto& copyProhibited = f2.trackCopyProhibited();
    const auto& isAudio = f2.trackIsAudio();
    const auto& is2Channel = f2.trackIs2Channel();
    const auto& isrc = f2.trackIsrc();

    ORC_LOG_INFO("  {}", trackBorder("┌", "┬", "┐"));
    ORC_LOG_INFO("  {}", trackRow("Trk", "Start", "End", "Duration", "Type",
                                  "Pre", "Cpy", "ISRC"));
    ORC_LOG_INFO("  {}", trackBorder("├", "┼", "┤"));
    for (size_t i = 0; i < trackNumbers.size(); ++i) {
      std::string type =
          !isAudio[i] ? "Data" : (is2Channel[i] ? "Audio" : "Audio4");
      std::string pre =
          preemphasis[i] ? (preemphasisVaried[i] ? "YES*" : "YES") : "no";
      std::string cpy = copyProhibited[i] ? "prot" : "ok";
      std::string dur = (absEnd[i] >= absStart[i])
                            ? (absEnd[i] - absStart[i]).toString()
                            : "N/A";
      ORC_LOG_INFO("  {}",
                   trackRow(std::to_string(trackNumbers[i]),
                            absStart[i].toString(), absEnd[i].toString(), dur,
                            type, pre, cpy, fmtIsrc(isrc[i])));
    }
    ORC_LOG_INFO("  {}", trackBorder("└", "┴", "┘"));
  }
  ORC_LOG_INFO("");
  ORC_LOG_INFO("  Legend");
  ORC_LOG_INFO(
      "    Type  Audio = 2-channel audio, Audio4 = 4-channel, Data = data "
      "track");
  ORC_LOG_INFO(
      "    Pre   50/15us pre-emphasis flag: YES = set, no = clear, YES* = "
      "varied within track");
  ORC_LOG_INFO("    Cpy   prot = copy prohibited, ok = copying permitted");
  ORC_LOG_INFO(
      "    ISRC  ISO 3901 recording code (Q-mode 3); - if none carried");
  ORC_LOG_INFO("");
  ORC_LOG_INFO(
      "  Note: track times are absolute (whole-disc); track-relative times are "
      "in Part D.");
  ORC_LOG_INFO("");
}

void EfmProcessor::showQuality() const {
  const F2SectionCorrection& f2 = m_f2SectionCorrection;

  ORC_LOG_INFO("{}", bannerTop());
  ORC_LOG_INFO("{}", bannerMid("PART C - SIGNAL & ERROR-CORRECTION QUALITY"));
  ORC_LOG_INFO("{}", bannerBottom());
  ORC_LOG_INFO("");

  ORC_LOG_INFO("  Section-level integrity (F2)");
  ORC_LOG_INFO("    Total sections         : {}   ({} F2 frames)",
               commas(f2.totalSections()),
               commas(static_cast<uint64_t>(f2.totalSections()) * 98));
  ORC_LOG_INFO("    Corrected (Q repaired) : {}",
               commas(f2.correctedSections()));
  ORC_LOG_INFO("    Missing (interpolated) : {}", commas(f2.missingSections()));
  ORC_LOG_INFO("    Padding                : {}", commas(f2.paddingSections()));
  ORC_LOG_INFO("    Pre-lead-in            : {}",
               commas(f2.preLeadinSections()));
  ORC_LOG_INFO("    Out of order           : {}",
               commas(f2.outOfOrderSections()));
  ORC_LOG_INFO("    Uncorrectable          : {}",
               commas(f2.uncorrectableSections()));
  ORC_LOG_INFO("");

  const int32_t c1total = m_f2SectionToF1Section.validC1s() +
                          m_f2SectionToF1Section.fixedC1s() +
                          m_f2SectionToF1Section.errorC1s();
  const int32_t c2total = m_f2SectionToF1Section.validC2s() +
                          m_f2SectionToF1Section.fixedC2s() +
                          m_f2SectionToF1Section.errorC2s();
  ORC_LOG_INFO("  CIRC error correction");
  ORC_LOG_INFO("    C1 :  valid {}   fixed {}   uncorrectable {}   ({})",
               commas(m_f2SectionToF1Section.validC1s()),
               commas(m_f2SectionToF1Section.fixedC1s()),
               commas(m_f2SectionToF1Section.errorC1s()),
               fmtPercent(m_f2SectionToF1Section.errorC1s(), c1total, 4));
  ORC_LOG_INFO("    C2 :  valid {}   fixed {}   uncorrectable {}   ({})",
               commas(m_f2SectionToF1Section.validC2s()),
               commas(m_f2SectionToF1Section.fixedC2s()),
               commas(m_f2SectionToF1Section.errorC2s()),
               fmtPercent(m_f2SectionToF1Section.errorC2s(), c2total, 4));
  ORC_LOG_INFO("");

  const uint64_t totalBytes = m_f1SectionToData24Section.totalBytes();
  const uint64_t corruptBytes = m_f1SectionToData24Section.corruptBytes();
  const uint64_t paddedBytes = m_f1SectionToData24Section.paddedBytes();
  const uint64_t validBytes =
      totalBytes >= corruptBytes ? totalBytes - corruptBytes : 0;
  ORC_LOG_INFO("  Byte-level data integrity (Data24)");
  ORC_LOG_INFO("    Total     : {}", fmtBytes(totalBytes));
  ORC_LOG_INFO("    Valid     : {}", fmtBytes(validBytes));
  ORC_LOG_INFO("    Corrupt   : {}", fmtBytes(corruptBytes));
  ORC_LOG_INFO("    Padded    : {}", fmtBytes(paddedBytes));
  ORC_LOG_INFO("    Data loss : {}", fmtPercent(corruptBytes, totalBytes, 4));
  ORC_LOG_INFO("");

  if (m_audioMode) {
    if (!m_noAudioConcealment) {
      const uint64_t concealed = m_audioCorrection.concealedSamples();
      const uint64_t silenced = m_audioCorrection.silencedSamples();
      const uint64_t valid = m_audioCorrection.validSamples();
      ORC_LOG_INFO("  Audio concealment");
      ORC_LOG_INFO("    Total mono samples : {}",
                   commas(valid + concealed + silenced));
      ORC_LOG_INFO("    Valid              : {}", commas(valid));
      ORC_LOG_INFO("    Concealed (interp) : {}", commas(concealed));
      ORC_LOG_INFO("    Silenced (muted)   : {}", commas(silenced));
      ORC_LOG_INFO("");
    }
    if (!m_ignorePreemphasis) {
      ORC_LOG_INFO("  Pre-emphasis / de-emphasis");
      ORC_LOG_INFO("    De-emphasised sections : {}",
                   commas(m_audioDeemphasis.deemphasisedSections()));
      ORC_LOG_INFO("    Pass-through sections  : {}",
                   commas(m_audioDeemphasis.passThroughSections()));
      ORC_LOG_INFO("");
    }
  } else {
    ORC_LOG_INFO("  Sector recovery (RSPC)");
    ORC_LOG_INFO("    Valid / corrected / uncorrectable : {} / {} / {}",
                 commas(m_rawSectorToSector.validSectors()),
                 commas(m_rawSectorToSector.correctedSectors()),
                 commas(m_rawSectorToSector.invalidSectors()));
    ORC_LOG_INFO("    Good sectors / missing (gap-filled) : {} / {}",
                 commas(m_sectorCorrection.goodSectors()),
                 commas(m_sectorCorrection.missingSectors()));
    ORC_LOG_INFO("");
  }
}

void EfmProcessor::showAllStatistics() const {
  // ---- Curated, user-facing sections (most important first) -------------
  showReportHeader();
  showSummary();       // Part A
  showDiscContents();  // Part B
  showQuality();       // Part C

  // ---- Part D: raw per-stage diagnostics (developer detail) -------------
  ORC_LOG_INFO("{}", bannerTop());
  ORC_LOG_INFO("{}",
               bannerMid("PART D - PIPELINE STAGE DETAIL (developer detail)"));
  ORC_LOG_INFO("{}", bannerBottom());
  ORC_LOG_INFO("");

  m_tValuesToChannel.showStatistics();
  ORC_LOG_INFO("");
  m_channelToF3.showStatistics();
  ORC_LOG_INFO("");
  m_f3FrameToF2Section.showStatistics();
  ORC_LOG_INFO("");
  m_f2SectionCorrection.showStatistics();
  ORC_LOG_INFO("");
  m_f2SectionToF1Section.showStatistics();
  ORC_LOG_INFO("");
  m_f1SectionToData24Section.showStatistics();
  ORC_LOG_INFO("");

  if (m_audioMode) {
    m_data24ToAudio.showStatistics();
    ORC_LOG_INFO("");
    if (!m_noAudioConcealment) {
      m_audioCorrection.showStatistics();
      ORC_LOG_INFO("");
    }
    if (!m_ignorePreemphasis) {
      m_audioDeemphasis.showStatistics();
      ORC_LOG_INFO("");
    }
  } else {
    m_data24ToRawSector.showStatistics();
    ORC_LOG_INFO("");
    m_rawSectorToSector.showStatistics();
    ORC_LOG_INFO("");
    m_sectorCorrection.showStatistics();
    ORC_LOG_INFO("");
  }

  // ---- Part E: processing performance -----------------------------------
  ORC_LOG_INFO("{}", bannerTop());
  ORC_LOG_INFO("{}", bannerMid("PART E - PROCESSING PERFORMANCE"));
  ORC_LOG_INFO("{}", bannerBottom());
  ORC_LOG_INFO("");

  showGeneralPipelineStatistics();
  showD24PipelineStatistics();
  if (m_audioMode) {
    showAudioPipelineStatistics();
  } else {
    showDataPipelineStatistics();
  }

  auto endTime = std::chrono::high_resolution_clock::now();
  [[maybe_unused]] int64_t wallTimeMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(endTime -
                                                            m_startTime)
          .count();
  ORC_LOG_INFO("Overall wall-clock time: {} ms ({:.2f} seconds)", wallTimeMs,
               wallTimeMs / 1000.0);

  ORC_LOG_INFO("");
  ORC_LOG_INFO("{}", ruleDouble());
  ORC_LOG_INFO("{}", centred("END OF DECODE REPORT"));
  ORC_LOG_INFO("{}", ruleDouble());
}
