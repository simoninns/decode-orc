/*
 * File:        metrics_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     Quality metrics presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "metrics_presenter.h"
#include "../core/include/observation_context.h"
#include <cmath>
#include <variant>

namespace orc::presenters {

QualityMetrics MetricsPresenter::extractFieldMetrics(
    FieldID field_id,
    const void* obs_context_handle)
{
    const auto* obs_context = static_cast<const orc::ObservationContext*>(obs_context_handle);
    QualityMetrics metrics;
    
    // Extract disc quality metrics from observation context
    double quality_score = 0.0;
    if (extractDoubleValue(obs_context_handle, field_id, "disc_quality", "quality_score", quality_score)) {
        metrics.quality_score = quality_score;
        metrics.has_quality_score = true;
    }
    
    int32_t dropout_count = 0;
    if (extractInt32Value(obs_context_handle, field_id, "disc_quality", "dropout_count", dropout_count)) {
        metrics.dropout_count = static_cast<size_t>(dropout_count);
        metrics.has_dropout_count = true;
    }
    
    // Extract burst level from observation context
    double burst_level = 0.0;
    if (extractDoubleValue(obs_context_handle, field_id, "burst_level", "median_burst_ire", burst_level)) {
        metrics.burst_level = burst_level;
        metrics.has_burst_level = true;
    }
    
    // Extract white SNR from observation context
    double white_snr = 0.0;
    if (extractDoubleValue(obs_context_handle, field_id, "white_snr", "snr_db", white_snr)) {
        metrics.white_snr = white_snr;
        metrics.has_white_snr = true;
    }
    
    // Extract black PSNR from observation context
    double black_psnr = 0.0;
    if (extractDoubleValue(obs_context_handle, field_id, "black_psnr", "psnr_db", black_psnr)) {
        metrics.black_psnr = black_psnr;
        metrics.has_black_psnr = true;
    }
    
    return metrics;
}

QualityMetrics MetricsPresenter::extractFrameMetrics(
    FieldID field1_id,
    FieldID field2_id,
    const void* obs_context_handle)
{
    // Extract metrics for both fields
    auto field1_metrics = extractFieldMetrics(field1_id, obs_context_handle);
    auto field2_metrics = extractFieldMetrics(field2_id, obs_context_handle);
    
    // Create averaged metrics for the frame
    QualityMetrics frame_metrics;
    
    // Average metrics that are present
    int valid_count = 0;
    
    if (field1_metrics.has_white_snr) {
        frame_metrics.white_snr += field1_metrics.white_snr;
        frame_metrics.has_white_snr = true;
        valid_count++;
    }
    if (field2_metrics.has_white_snr) {
        frame_metrics.white_snr += field2_metrics.white_snr;
        if (valid_count > 0) frame_metrics.has_white_snr = true;
        valid_count++;
    }
    if (valid_count > 0) {
        frame_metrics.white_snr /= valid_count;
    }
    valid_count = 0;
    
    if (field1_metrics.has_black_psnr) {
        frame_metrics.black_psnr += field1_metrics.black_psnr;
        frame_metrics.has_black_psnr = true;
        valid_count++;
    }
    if (field2_metrics.has_black_psnr) {
        frame_metrics.black_psnr += field2_metrics.black_psnr;
        if (valid_count > 0) frame_metrics.has_black_psnr = true;
        valid_count++;
    }
    if (valid_count > 0) {
        frame_metrics.black_psnr /= valid_count;
    }
    valid_count = 0;
    
    if (field1_metrics.has_burst_level) {
        frame_metrics.burst_level += field1_metrics.burst_level;
        frame_metrics.has_burst_level = true;
        valid_count++;
    }
    if (field2_metrics.has_burst_level) {
        frame_metrics.burst_level += field2_metrics.burst_level;
        if (valid_count > 0) frame_metrics.has_burst_level = true;
        valid_count++;
    }
    if (valid_count > 0) {
        frame_metrics.burst_level /= valid_count;
    }
    valid_count = 0;
    
    if (field1_metrics.has_quality_score) {
        frame_metrics.quality_score += field1_metrics.quality_score;
        frame_metrics.has_quality_score = true;
        valid_count++;
    }
    if (field2_metrics.has_quality_score) {
        frame_metrics.quality_score += field2_metrics.quality_score;
        if (valid_count > 0) frame_metrics.has_quality_score = true;
        valid_count++;
    }
    if (valid_count > 0) {
        frame_metrics.quality_score /= valid_count;
    }
    valid_count = 0;
    
    if (field1_metrics.has_dropout_count) {
        frame_metrics.dropout_count += field1_metrics.dropout_count;
        frame_metrics.has_dropout_count = true;
        valid_count++;
    }
    if (field2_metrics.has_dropout_count) {
        frame_metrics.dropout_count += field2_metrics.dropout_count;
        if (valid_count > 0) frame_metrics.has_dropout_count = true;
        valid_count++;
    }
    if (valid_count > 0 && valid_count < 2) {
        frame_metrics.dropout_count /= valid_count;
    }
    
    return frame_metrics;
}

bool MetricsPresenter::extractDoubleValue(
    const void* obs_context_handle,
    FieldID field_id,
    const std::string& namespace_,
    const std::string& key,
    double& out_value)
{
    const auto* obs_context = static_cast<const orc::ObservationContext*>(obs_context_handle);
    auto value_opt = obs_context->get(field_id, namespace_, key);
    if (!value_opt) {
        return false;
    }
    
    // Try to extract as double
    if (std::holds_alternative<double>(*value_opt)) {
        out_value = std::get<double>(*value_opt);
        return true;
    }
    
    // Try to convert from int32_t
    if (std::holds_alternative<int32_t>(*value_opt)) {
        out_value = static_cast<double>(std::get<int32_t>(*value_opt));
        return true;
    }
    
    // Try to convert from int64_t
    if (std::holds_alternative<int64_t>(*value_opt)) {
        out_value = static_cast<double>(std::get<int64_t>(*value_opt));
        return true;
    }
    
    return false;
}

bool MetricsPresenter::extractInt32Value(
    const void* obs_context_handle,
    FieldID field_id,
    const std::string& namespace_,
    const std::string& key,
    int32_t& out_value)
{
    const auto* obs_context = static_cast<const orc::ObservationContext*>(obs_context_handle);
    auto value_opt = obs_context->get(field_id, namespace_, key);
    if (!value_opt) {
        return false;
    }
    
    // Try to extract as int32_t
    if (std::holds_alternative<int32_t>(*value_opt)) {
        out_value = std::get<int32_t>(*value_opt);
        return true;
    }
    
    // Try to convert from int64_t (if it fits)
    if (std::holds_alternative<int64_t>(*value_opt)) {
        int64_t val = std::get<int64_t>(*value_opt);
        if (val >= INT32_MIN && val <= INT32_MAX) {
            out_value = static_cast<int32_t>(val);
            return true;
        }
    }
    
    // Try to convert from double
    if (std::holds_alternative<double>(*value_opt)) {
        double val = std::get<double>(*value_opt);
        if (val >= INT32_MIN && val <= INT32_MAX && std::isfinite(val)) {
            out_value = static_cast<int32_t>(val);
            return true;
        }
    }
    
    return false;
}

} // namespace orc::presenters
