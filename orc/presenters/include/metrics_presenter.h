/*
 * File:        metrics_presenter.h
 * Module:      orc-presenters
 * Purpose:     Quality metrics presenter - MVP architecture wrapper for ObservationContext
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <field_id.h>
#include <string>

// Forward declare core type to avoid exposing it in GUI layer
namespace orc {
    class ObservationContext;
}

namespace orc::presenters {

/**
 * @brief Quality metrics extracted from observation context
 * 
 * This is a public API type that wraps core ObservationContext
 * to hide its complexity from the GUI layer.
 */
struct QualityMetrics {
    double white_snr = 0.0;           ///< White SNR value (dB)
    double black_psnr = 0.0;          ///< Black PSNR value (dB)
    double burst_level = 0.0;         ///< Burst level (IRE)
    double quality_score = 0.0;       ///< Overall quality score (0-100)
    size_t dropout_count = 0;         ///< Count of dropouts
    
    bool has_white_snr = false;       ///< Whether white SNR is available
    bool has_black_psnr = false;      ///< Whether black PSNR is available
    bool has_burst_level = false;     ///< Whether burst level is available
    bool has_quality_score = false;   ///< Whether quality score is available
    bool has_dropout_count = false;   ///< Whether dropout count is available
};

/**
 * @brief Metrics presenter - Provides clean access to quality metrics
 * 
 * This presenter wraps the core ObservationContext and provides a clean
 * public API for accessing quality metrics in the GUI layer.
 * 
 * The GUI never needs to know about ObservationContext, ObservationValue,
 * or variant types. This presenter extracts, converts, and presents
 * the data in a form the GUI understands.
 */
class MetricsPresenter {
public:
    /**
     * @brief Extract quality metrics for a single field
     * 
     * @param field_id Field to extract metrics for
     * @param obs_context Core observation context (wrapped by this presenter)
     * @return Quality metrics
     */
    static QualityMetrics extractFieldMetrics(
        FieldID field_id,
        const orc::ObservationContext& obs_context
    );
    
    /**
     * @brief Extract and average quality metrics for two fields (frame)
     * 
     * @param field1_id First field of frame
     * @param field2_id Second field of frame
     * @param obs_context Core observation context (wrapped by this presenter)
     * @return Averaged quality metrics
     */
    static QualityMetrics extractFrameMetrics(
        FieldID field1_id,
        FieldID field2_id,
        const orc::ObservationContext& obs_context
    );

private:
    /**
     * @brief Extract a double value from observation context
     * 
     * @param obs_context Context to extract from
     * @param field_id Field ID
     * @param namespace_ Observation namespace
     * @param key Observation key
     * @param out_value Output value (set if found)
     * @return True if value was found and extracted
     */
    static bool extractDoubleValue(
        const orc::ObservationContext& obs_context,
        FieldID field_id,
        const std::string& namespace_,
        const std::string& key,
        double& out_value
    );
    
    /**
     * @brief Extract an int32_t value from observation context
     * 
     * @param obs_context Context to extract from
     * @param field_id Field ID
     * @param namespace_ Observation namespace
     * @param key Observation key
     * @param out_value Output value (set if found)
     * @return True if value was found and extracted
     */
    static bool extractInt32Value(
        const orc::ObservationContext& obs_context,
        FieldID field_id,
        const std::string& namespace_,
        const std::string& key,
        int32_t& out_value
    );
};

} // namespace orc::presenters
