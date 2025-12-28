/*
 * File:        snr_analysis_observer.cpp
 * Module:      orc-core
 * Purpose:     SNR analysis observer implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "snr_analysis_observer.h"
#include "observation_history.h"
#include "vits_observer.h"
#include "biphase_observer.h"
#include "logging.h"
#include <memory>

namespace orc {

std::vector<std::shared_ptr<Observation>> SNRAnalysisObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history)
{
    (void)representation;  // Currently unused
    std::vector<std::shared_ptr<Observation>> observations;
    
    // Create observation
    auto obs = std::make_shared<SNRAnalysisObservation>();
    
    // Look for VITS quality observations in the history
    auto vits_observations = history.get_observations(field_id, "VITSQuality");
    
    if (!vits_observations.empty()) {
        // Get the most recent VITS observation
        auto vits_obs = std::dynamic_pointer_cast<VITSQualityObservation>(vits_observations.back());
        
        if (vits_obs) {
            // Extract white SNR if available and mode permits
            if ((mode_ == SNRAnalysisMode::WHITE_SNR || mode_ == SNRAnalysisMode::BOTH) && 
                vits_obs->white_snr.has_value()) {
                obs->white_snr = vits_obs->white_snr.value();
                obs->has_white_snr = true;
            }
            
            // Extract black PSNR if available and mode permits
            if ((mode_ == SNRAnalysisMode::BLACK_PSNR || mode_ == SNRAnalysisMode::BOTH) && 
                vits_obs->black_psnr.has_value()) {
                obs->black_psnr = vits_obs->black_psnr.value();
                obs->has_black_psnr = true;
            }
        }
    } else {
        ORC_LOG_DEBUG("SNRAnalysisObserver: Field {} has no VITS observations", field_id.value());
    }
    
    // Try to get frame number from Biphase observations
    auto biphase_observations = history.get_observations(field_id, "Biphase");
    if (!biphase_observations.empty()) {
        auto biphase_obs = std::dynamic_pointer_cast<BiphaseObservation>(biphase_observations.back());
        if (biphase_obs && biphase_obs->picture_number.has_value()) {
            obs->frame_number = biphase_obs->picture_number;
        }
    }
    
    observations.push_back(obs);
    return observations;
}

} // namespace orc
