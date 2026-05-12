/*
 * File:        burst_level_analysis_sink_deps_interface.h
 * Module:      orc-core
 * Purpose:     Interface for BurstLevelAnalysisSinkStage dependencies
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#ifndef ORC_CORE_BURST_LEVEL_ANALYSIS_SINK_DEPS_INTERFACE_H
#define ORC_CORE_BURST_LEVEL_ANALYSIS_SINK_DEPS_INTERFACE_H

#include "burst_level_analysis_types.h"
#include "observation_context_interface.h"
#include "triggerable_stage.h"
#include "video_field_representation.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace orc
{
    struct BurstAnalysisComputeOptions
    {
        std::string output_path;
        bool write_csv{false};
        size_t max_frames{1000};
    };

    struct BurstAnalysisComputeResult
    {
        bool success{false};
        std::string message;
        std::vector<FrameBurstLevelStats> frame_stats;
        int32_t total_frames{0};
    };

    class IBurstLevelAnalysisSinkStageDeps
    {
    public:
        virtual ~IBurstLevelAnalysisSinkStageDeps() = default;

        virtual void init(
            TriggerProgressCallback progress_callback,
            std::atomic<bool>* cancel_requested) = 0;

        virtual BurstAnalysisComputeResult compute_and_analyze(
            VideoFieldRepresentation* representation,
            IObservationContext& observation_context,
            BurstAnalysisComputeOptions options) = 0;

        virtual bool write_csv(
            const std::string& path,
            const std::vector<FrameBurstLevelStats>& frame_stats) = 0;
    };
} // namespace orc

#endif // ORC_CORE_BURST_LEVEL_ANALYSIS_SINK_DEPS_INTERFACE_H
