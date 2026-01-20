/*
 * File:        dropout_analysis_decoder.cpp
 * Module:      orc-core
 * Purpose:     Dropout analysis data extraction implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "dropout_analysis_decoder.h"
#include "../../include/logging.h"

namespace orc {

DropoutAnalysisDecoder::DropoutAnalysisDecoder(std::shared_ptr<const DAG> dag)
{
    if (!dag) {
        throw std::invalid_argument("DropoutAnalysisDecoder: DAG cannot be null");
    }
    ORC_LOG_DEBUG("DropoutAnalysisDecoder: Created (stub implementation)");
}

void DropoutAnalysisDecoder::set_observation_cache(std::shared_ptr<ObservationCache> cache)
{
    if (!cache) {
        throw std::invalid_argument("DropoutAnalysisDecoder: ObservationCache cannot be null");
    }
    ORC_LOG_DEBUG("DropoutAnalysisDecoder: Observation cache updated (stub)");
}

std::optional<FieldDropoutStats> DropoutAnalysisDecoder::get_dropout_for_field(
    NodeID node_id,
    FieldID field_id,
    DropoutAnalysisMode mode)
{
    (void)node_id;
    (void)field_id;
    (void)mode;
    ORC_LOG_WARN("DropoutAnalysisDecoder: Dropout analysis not available (observer refactor)");
    return std::nullopt;
}

std::vector<FieldDropoutStats> DropoutAnalysisDecoder::get_dropout_for_all_fields(
    NodeID node_id,
    DropoutAnalysisMode mode,
    size_t max_fields,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    (void)node_id;
    (void)mode;
    (void)max_fields;
    (void)progress_callback;
    ORC_LOG_WARN("DropoutAnalysisDecoder: Dropout analysis not available (observer refactor)");
    return std::vector<FieldDropoutStats>();
}

std::vector<FrameDropoutStats> DropoutAnalysisDecoder::get_dropout_by_frames(
    NodeID node_id,
    DropoutAnalysisMode mode,
    size_t max_frames,
    std::function<void(size_t, size_t, const std::string&)> progress_callback)
{
    (void)node_id;
    (void)mode;
    (void)max_frames;
    (void)progress_callback;
    ORC_LOG_WARN("DropoutAnalysisDecoder: Dropout analysis not available (observer refactor)");
    return std::vector<FrameDropoutStats>();
}

} // namespace orc
