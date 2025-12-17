/******************************************************************************
 * vits_observer.h
 *
 * VITS (Vertical Interval Test Signal) quality observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#pragma once

#include "observer.h"
#include "tbc_metadata.h"
#include <optional>
#include <vector>

namespace orc {

// Line configuration for VITS extraction
struct VITSLineConfig {
    size_t line_number;         // Field line number (1-based)
    double start_us;            // Start time in microseconds
    double length_us;           // Length in microseconds
};

// VITS quality observation
class VITSQualityObservation : public Observation {
public:
    std::optional<double> white_snr;    // White flag SNR in dB
    std::optional<double> black_psnr;   // Black level PSNR in dB
    
    std::string observation_type() const override {
        return "VITSQuality";
    }
};

// VITS quality observer
class VITSQualityObserver : public Observer {
public:
    VITSQualityObserver();
    
    std::string observer_name() const override {
        return "VITSQualityObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id) override;
    
    void set_parameters(const std::map<std::string, std::string>& params) override;
    
private:
    // Configuration for PAL and NTSC
    std::vector<VITSLineConfig> pal_white_configs_;
    std::vector<VITSLineConfig> pal_black_configs_;
    std::vector<VITSLineConfig> ntsc_white_configs_;
    std::vector<VITSLineConfig> ntsc_black_configs_;
    
    // Validation range for white level (IRE)
    double white_ire_min_ = 90.0;
    double white_ire_max_ = 110.0;
    
    // Helper methods
    std::vector<double> get_field_line_slice(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        size_t field_line,
        double start_us,
        double length_us) const;
    
    double calculate_psnr(const std::vector<double>& data) const;
    double calc_mean(const std::vector<double>& data) const;
    double calc_std(const std::vector<double>& data) const;
    double round_to_decimal_places(double value, int places) const;
    
    void initialize_default_configs();
};

} // namespace orc
