/*
 * File:        burst_level_analysis_decoder.cpp
 * Module:      orc-core
 * Purpose:     Burst level analysis data extraction implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "burst_level_analysis_decoder.h"
#include "../../include/logging.h"

namespace orc {

BurstLevelAnalysisDecoder::BurstLevelAnalysisDecoder(std::shared_ptr<const DAG> dag)
    : dag_(dag)
{
    if (!dag) {
        throw std::invalid_argument("BurstLevelAnalysisDecoder: DAG cannot be null");
    }
    ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Created (stub implementation)");
}

void BurstLevelAnalysisDecoder::update_dag(std::shared_ptr<const DAG> dag)
{
    if (!dag) {
        throw std::invalid_argument("BurstLevelAnalysisDecoder: DAG cannot be null");
    }
    dag_ = dag;
    ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: DAG updated (stub implementation)");
}

void BurstLevelAnalysisDecoder::set_observation_cache(std::shared_ptr<ObservationCache> cache)
{
    if (!cache) {
        throw std::invalid_argument("BurstLevelAnalysisDecoder: ObservationCache cannot be null");
    }
    ORC_LOG_DEBUG("BurstLevelAnalysisDecoder: Observation cache updated (stub implementation)");
}

std::optional<FieldBurstLevelStats> BurstLevelAnalysisDecoder::get_burst_level_for_field(
    NodeID node_id,
    FieldID field_id)
{
    // Stub: burst level analysis removed as part of observer refactor
    ORC_LOG_WARN("BurstLevelAnalysisDecoder: Burst level analysis not available (observer refactor)");
    return std::nullopt;
}

std::vector<FieldBurstLevelStats> BurstLevelAnalysisDecoder::get_burst_level_for_all_fields(
    NodeID node_id,
    size_t max_fields,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    // Stub: burst level analysis removed as part of observer refactor
    ORC_LOG_WARN("BurstLevelAnalysisDecoder: Burst level analysis not available (observer refactor)");
    return std::vector<FieldBurstLevelStats>();
}

std::vector<FrameBurstLevelStats> BurstLevelAnalysisDecoder::get_burst_level_by_frames(
    NodeID node_id,
    size_t max_frames,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    (void)node_id;
    (void)max_frames;
    (void)progress_callback;
    ORC_LOG_WARN("BurstLevelAnalysisDecoder: Burst level analysis not available (observer refactor)");
    return std::vector<FrameBurstLevelStats>();
}

} // namespace orc
