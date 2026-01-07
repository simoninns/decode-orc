/*
 * File:        snr_analysis_observer.h
 * Module:      orc-core
 * Purpose:     SNR (Signal-to-Noise Ratio) analysis observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "observer.h"
#include <cstdint>

namespace orc {

/**
 * @brief SNR analysis mode
 */
enum class SNRAnalysisMode {
    WHITE_SNR,      ///< Analyze white (peak) SNR only
    BLACK_PSNR,     ///< Analyze black (PSNR) only
    BOTH            ///< Analyze both white SNR and black PSNR
};

/**
 * @brief Observation for SNR analysis
 * 
 * This observer extracts SNR (Signal-to-Noise Ratio) metrics from the 
 * VITS (Vertical Interval Test Signals) metadata. These metrics are 
 * calculated during the decoding process and stored in the TBC metadata.
 * 
 * White SNR measures the signal-to-noise ratio for white (peak) levels.
 * Black PSNR measures the peak signal-to-noise ratio for black levels.
 */
class SNRAnalysisObservation : public Observation {
public:
    /// White SNR value (dB)
    double white_snr = 0.0;
    
    /// Black PSNR value (dB)
    double black_psnr = 0.0;
    
    /// Whether white SNR data is available
    bool has_white_snr = false;
    
    /// Whether black PSNR data is available
    bool has_black_psnr = false;
    
    /// Frame number (if available from VBI)
    std::optional<int32_t> frame_number;
    
    std::string observation_type() const override {
        return "SNRAnalysis";
    }
};

/**
 * @brief Observer for SNR analysis
 * 
 * Extracts SNR metrics from VITS (Vertical Interval Test Signals) metadata.
 * The SNR values are calculated during the decoding process and stored in
 * the TBC metadata's VitsMetrics structure.
 * 
 * Supports three modes:
 * - WHITE_SNR: Extract only white SNR values
 * - BLACK_PSNR: Extract only black PSNR values
 * - BOTH: Extract both metrics
 * 
 * This is equivalent to ld-analyse's white/black SNR analysis functionality.
 */
class SNRAnalysisObserver : public Observer {
public:
    SNRAnalysisObserver(SNRAnalysisMode mode = SNRAnalysisMode::BOTH)
        : mode_(mode) {}
    
    std::string observer_name() const override {
        return "SNRAnalysisObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) override;
    
    /**
     * @brief Set the analysis mode
     */
    void set_mode(SNRAnalysisMode mode) {
        mode_ = mode;
    }
    
    /**
     * @brief Get the current analysis mode
     */
    SNRAnalysisMode get_mode() const {
        return mode_;
    }

private:
    SNRAnalysisMode mode_;
};

} // namespace orc
