/*
 * File:        section_csv_dumper.h
 * Purpose:     EFM pipeline instrumentation — Section fingerprint CSV writer (C++17)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns / instrumentation by decode-orc
 *
 * Header-only template class that writes a section-fingerprint CSV for any
 * Section type that exposes the same API as F2Section and Data24Section:
 *   section.metadata.absoluteSectionTime() → SectionTime
 *   section.metadata.sectionTime()         → SectionTime
 *   section.frame(int i)                   → const Frame &
 *     .countPadded()                       → uint32_t
 *     .countErrors()                       → uint32_t
 *     .data()                              → const std::vector<uint8_t>&
 *     .errorData()                         → const std::vector<uint8_t>&
 *     .paddedData()                        → const std::vector<uint8_t>&
 *
 * CSV format (see docs/efm-pipeline-instrumentation-plan.md §3):
 *   seq,abs_tc,rel_tc,is_gap,is_corrupt,frames,crc32_data,crc32_error,crc32_padded
 */

#pragma once

#include <fstream>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <zlib.h>

template <typename SectionType>
class SectionCsvDumper
{
public:
    SectionCsvDumper() : m_seq(0) {}

    // Open the CSV file for writing.  Returns false on failure.
    bool open(const std::string &filename)
    {
        m_file.open(filename, std::ios::out | std::ios::trunc);
        if (!m_file.is_open()) {
            return false;
        }
        m_file << "seq,abs_tc,rel_tc,is_gap,is_corrupt,frames,"
                  "crc32_data,crc32_error,crc32_padded\n";
        return true;
    }

    bool isOpen() const { return m_file.is_open(); }

    // Write one CSV row for the given section.
    void write(const SectionType &section)
    {
        if (!m_file.is_open()) return;

        // -- Timecodes -------------------------------------------------------
        auto abs = section.metadata.absoluteSectionTime();
        auto rel = section.metadata.sectionTime();

        char absTc[9], relTc[9];
        std::snprintf(absTc, sizeof(absTc), "%02d:%02d:%02d",
                      abs.minutes(), abs.seconds(), abs.frameNumber());
        std::snprintf(relTc, sizeof(relTc), "%02d:%02d:%02d",
                      rel.minutes(), rel.seconds(), rel.frameNumber());

        // -- is_gap: frame 0 has padded data set ----------------------------
        int isGap = (section.frame(0).countPadded() > 0) ? 1 : 0;

        // -- is_corrupt: any frame has error bytes set ----------------------
        int isCorrupt = 0;
        for (int i = 0; i < 98; ++i) {
            if (section.frame(i).countErrors() > 0) {
                isCorrupt = 1;
                break;
            }
        }

        // -- CRC32 of concatenated frame data / error / padded vectors ------
        uLong crcData   = crc32(0, nullptr, 0);
        uLong crcError  = crc32(0, nullptr, 0);
        uLong crcPadded = crc32(0, nullptr, 0);

        for (int i = 0; i < 98; ++i) {
            const auto &fd = section.frame(i).data();
            crcData = crc32(crcData,
                            reinterpret_cast<const Bytef*>(fd.data()),
                            static_cast<uInt>(fd.size()));

            const auto &fe = section.frame(i).errorData();
            crcError = crc32(crcError,
                             reinterpret_cast<const Bytef*>(fe.data()),
                             static_cast<uInt>(fe.size()));

            const auto &fp = section.frame(i).paddedData();
            crcPadded = crc32(crcPadded,
                              reinterpret_cast<const Bytef*>(fp.data()),
                              static_cast<uInt>(fp.size()));
        }

        char crcDataHex[9], crcErrorHex[9], crcPaddedHex[9];
        std::snprintf(crcDataHex,   sizeof(crcDataHex),   "%08x", static_cast<uint32_t>(crcData));
        std::snprintf(crcErrorHex,  sizeof(crcErrorHex),  "%08x", static_cast<uint32_t>(crcError));
        std::snprintf(crcPaddedHex, sizeof(crcPaddedHex), "%08x", static_cast<uint32_t>(crcPadded));

        m_file << m_seq   << ","
               << absTc   << ","
               << relTc   << ","
               << isGap   << ","
               << isCorrupt << ","
               << 98       << ","   // always 98 frames in a complete section
               << crcDataHex   << ","
               << crcErrorHex  << ","
               << crcPaddedHex << "\n";
        ++m_seq;
    }

    void close()
    {
        if (m_file.is_open()) {
            m_file.flush();
            m_file.close();
        }
    }

    ~SectionCsvDumper() { close(); }

private:
    std::ofstream m_file;
    int64_t       m_seq;
};
