/*
 * File:        timecode_probe.h
 * Module:      efm-decoder
 * Purpose:     Timecode probe utilities for auto-detecting no-timecode EFM
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef TIMECODE_PROBE_H
#define TIMECODE_PROBE_H

#include <cstdint>
#include <vector>

/**
 * @class TimecodeProbeStats
 * @brief Collects statistics during probe window to detect no-timecode EFM.
 */
class TimecodeProbeStats
{
public:
    TimecodeProbeStats()
        : totalSections(0),
          validMetadataSections(0),
          longestContiguousRun(0),
          currentContiguousRun(0),
          lastValidFrames(-1),
          outOfOrderCount(0),
          largeJumpCount(0)
    {
    }

    /**
     * Add a section result to the probe statistics.
     * @param isValid Whether the section metadata is valid.
     * @param absoluteFrames The absolute time in frames (or -1 if invalid).
     */
    void recordSection(bool isValid, int32_t absoluteFrames)
    {
        totalSections++;

        if (isValid) {
            validMetadataSections++;

            // Check contiguity
            if (lastValidFrames >= 0) {
                int32_t expectedFrames = lastValidFrames + 1;
                if (absoluteFrames == expectedFrames) {
                    // Contiguous
                    currentContiguousRun++;
                    if (currentContiguousRun > longestContiguousRun) {
                        longestContiguousRun = currentContiguousRun;
                    }
                } else {
                    // Break in contiguity
                    if (absoluteFrames < lastValidFrames) {
                        outOfOrderCount++;
                    } else if (absoluteFrames - lastValidFrames > 1) {
                        largeJumpCount++;
                    }
                    currentContiguousRun = 1;
                }
            } else {
                currentContiguousRun = 1;
            }

            lastValidFrames = absoluteFrames;
        }
    }

    /**
     * Decide whether to enable no-timecode mode based on probe statistics.
     * @return true if no-timecode should be enabled, false for normal timecode mode.
     */
    bool shouldEnableNoTimecodes() const
    {
        if (totalSections == 0) {
            return false; // Not enough data to decide
        }

        // Compute ratios
        double validRatio = static_cast<double>(validMetadataSections) / static_cast<double>(totalSections);
        double instabilityRatio = static_cast<double>(outOfOrderCount + largeJumpCount) / static_cast<double>(validMetadataSections > 0 ? validMetadataSections : 1);

        // Check: if we have high valid ratio but poor time progression, likely no-timecode
        // Lead-in sections are all valid with same time (0:00:00), then times should progress
        
        // Conservative thresholds
        const double validRatioThreshold = 0.90;      // >= 90% valid metadata (lead-in)
        const uint32_t contiguousThreshold = 10;      // But longestRun < 10 (not enough contiguity)
        const double instabilityThreshold = 0.10;     // Low instability in lead-in

        // Trigger no-timecode mode only if:
        // - High valid metadata (all lead-in sections are valid)
        // - Poor contiguity (absolute times not incrementing properly after lead-in)
        // - Low instability in what we have (lead-in portion is stable)
        bool highValidRatio = validRatio >= validRatioThreshold;
        bool poorContiguity = longestContiguousRun < contiguousThreshold;
        bool stableLeadIn = instabilityRatio < instabilityThreshold;

        return highValidRatio && poorContiguity && stableLeadIn;
    }

    // Statistics members
    uint32_t totalSections;
    uint32_t validMetadataSections;
    uint32_t longestContiguousRun;
    uint32_t currentContiguousRun;
    int32_t lastValidFrames;
    uint32_t outOfOrderCount;
    uint32_t largeJumpCount;
};

#endif // TIMECODE_PROBE_H
