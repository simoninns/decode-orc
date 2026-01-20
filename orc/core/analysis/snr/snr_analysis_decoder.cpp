/*
 * File:        snr_analysis_decoder.cpp
 * Module:      orc-core
 * Purpose:     SNR analysis data extraction implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "snr_analysis_decoder.h"
#include "../../include/logging.h"

namespace orc {

SNRAnalysisDecoder::SNRAnalysisDecoder(std::shared_ptr<const DAG> dag)
{
    if (!dag) {
        throw std::invalid_argument("SNRAnalysisDecoder: DAG cannot be null");
    }
    ORC_LOG_DEBUG("SNRAnalysisDecoder: Created (stub implementation)");
}

void SNRAnalysisDecoder::set_observation_cache(std::shared_ptr<ObservationCache> cache)
{
    if (!cache) {
        throw std::invalid_argument("SNRAnalysisDecoder: ObservationCache cannot be null");
    }
    ORC_LOG_DEBUG("SNRAnalysisDecoder: Observation cache updated (stub)");
}

std::optional<FieldSNRStats> SNRAnalysisDecoder::get_snr_for_field(
    NodeID node_id,
    FieldID field_id,
    SNRAnalysisMode mode)
{
    (void)node_id;
    (void)field_id;
    (void)mode;
    ORC_LOG_WARN("SNRAnalysisDecoder: SNR analysis not available (observer refactor)");
    return std::nullopt;
}

std::vector<FieldSNRStats> SNRAnalysisDecoder::get_snr_for_all_fields(
    NodeID node_id,
    SNRAnalysisMode mode,
    size_t max_fields,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    (void)node_id;
    (void)mode;
    (void)max_fields;
    (void)progress_callback;
    ORC_LOG_WARN("SNRAnalysisDecoder: SNR analysis not available (observer refactor)");
    return std::vector<FieldSNRStats>();
}

std::vector<FrameSNRStats> SNRAnalysisDecoder::get_snr_by_frames(
    NodeID node_id,
    SNRAnalysisMode mode,
    size_t max_frames,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    (void)node_id;
    (void)mode;
    (void)max_frames;
    (void)progress_callback;
    ORC_LOG_WARN("SNRAnalysisDecoder: SNR analysis not available (observer refactor)");
    return std::vector<FrameSNRStats>();
}

} // namespace orc
