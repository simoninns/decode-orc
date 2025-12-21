/*
 * File:        pal_phase_observer.cpp
 * Module:      orc-core
 * Purpose:     PAL field phase ID observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "pal_phase_observer.h"
#include "observation_history.h"
#include "logging.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace orc {

std::vector<std::shared_ptr<Observation>> PALPhaseObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    
    auto obs = std::make_shared<PALPhaseObservation>();
    obs->field_id = field_id;
    obs->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    obs->observer_version = observer_version();
    obs->observer_parameters = parameters_;
    obs->field_phase_id = -1;  // Unknown by default
    
    // Get video parameters
    auto video_params_opt = representation.get_video_parameters();
    if (!video_params_opt.has_value()) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    const auto& video_params = video_params_opt.value();
    
    // Only works for PAL
    if (video_params.system != VideoSystem::PAL) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    // Get field parity from observation history (if FieldParityObserver ran before us)
    // This is critical for correct PAL phase detection!
    bool is_first_field = false;
    auto parity_from_history = history.get_observation(field_id, "FieldParity");
    
    if (parity_from_history) {
        // Use the already-calculated parity from history
        auto* parity_obs = dynamic_cast<FieldParityObservation*>(parity_from_history.get());
        if (parity_obs && parity_obs->confidence_pct >= 25) {
            is_first_field = parity_obs->is_first_field;
        } else {
            obs->confidence = ConfidenceLevel::LOW;
            return {obs};  // Can't determine field parity reliably
        }
    } else {
        // Fallback: Calculate field parity ourselves (if FieldParityObserver not in pipeline)
        FieldParityObserver parity_observer;
        auto parity_observations = parity_observer.process_field(representation, field_id, history);
        
        if (parity_observations.empty()) {
            obs->confidence = ConfidenceLevel::NONE;
            return {obs};
        }
        
        auto* parity_obs = dynamic_cast<FieldParityObservation*>(parity_observations[0].get());
        if (!parity_obs || parity_obs->confidence_pct < 25) {
            obs->confidence = ConfidenceLevel::LOW;
            return {obs};  // Can't determine field parity reliably
        }
        is_first_field = parity_obs->is_first_field;
    }
    
    // Get median burst level for comparison
    double median_burst = 0.0;
    std::vector<double> burst_levels;
    for (size_t line = 11; line < 300; ++line) {
        auto level_opt = get_burst_level(representation, field_id, line, video_params);
        if (level_opt.has_value()) {
            burst_levels.push_back(level_opt.value());
        }
    }
    
    if (burst_levels.empty()) {
        obs->confidence = ConfidenceLevel::NONE;
        return {obs};
    }
    
    std::sort(burst_levels.begin(), burst_levels.end());
    median_burst = burst_levels[burst_levels.size() / 2];
    
    // Step 1: Determine 4-field sequence using burst presence/absence on line 6
    // PAL spec uses 1-based line numbering, but our get_line() uses 0-based indexing
    // So "line 6" in PAL spec corresponds to get_line(field_id, 5)
    // Lines 7, 8 in spec correspond to get_line indices 6, 7
    auto burst6 = get_burst_level(representation, field_id, 5, video_params);  // Spec line 6
    auto phase7_opt = measure_burst_phase(representation, field_id, 6, video_params);  // Spec line 7
    auto phase8_opt = measure_burst_phase(representation, field_id, 7, video_params);  // Spec line 8
    
    if (!phase7_opt.has_value()) {
        obs->confidence = ConfidenceLevel::LOW;
        return {obs};
    }
    
    double phase7 = phase7_opt.value();
    
    // Determine 4-field position using burst presence on line 6 (as per ld-decode)
    // Field 1: First field, no burst on line 6
    // Field 2: Second field, burst on line 6  
    // Field 3: First field, burst on line 6
    // Field 4: Second field, no burst on line 6
    int phase_4field;
    if (burst6.has_value()) {
        double burst_level = burst6.value();
        bool hasburst;
        
        // Use ld-decode's threshold logic with adjusted ranges:
        // Strong burst: >= 70% of median
        // Weak/no burst: < 30% of median  
        // Ambiguous: 30-70% - use field parity to make educated guess
        if (burst_level >= median_burst * 0.7) {
            hasburst = true;
        } else if (burst_level < median_burst * 0.3) {
            hasburst = false;
        } else {
            // Ambiguous burst level - use field parity for educated guess
            // Burst on line 6 tends to appear in fields 2 and 3
            // No burst tends to appear in fields 1 and 4
            // So guess: first field → likely field 3 (burst), second field → likely field 4 (no burst)
            obs->confidence = ConfidenceLevel::MEDIUM;
            hasburst = is_first_field;  // First field more likely to have burst if ambiguous
            ORC_LOG_TRACE("PALPhaseObserver: Field {} first={} burst6={:.0f} median={:.0f} AMBIGUOUS → guessing hasburst={}",
                         field_id.value(), is_first_field, burst_level, median_burst, hasburst);
        }
        
        // Map (is_first_field, hasburst) to 4-field position
        if (is_first_field && !hasburst) phase_4field = 1;
        else if (!is_first_field && hasburst) phase_4field = 2;
        else if (is_first_field && hasburst) phase_4field = 3;
        else phase_4field = 4;  // (!is_first_field && !hasburst)
        
        ORC_LOG_TRACE("PALPhaseObserver: Field {} first={} burst6={:.0f} median={:.0f} hasburst={} → phase_4field={}",
                     field_id.value(), is_first_field, burst_level, median_burst, hasburst, phase_4field);
    } else {
        // No burst measurement - use fallback
        obs->confidence = ConfidenceLevel::LOW;
        return {obs};
    }
    

    // Normalize to 0-360 range
    while (phase7 < 0) phase7 += 360.0;
    while (phase7 >= 360.0) phase7 -= 360.0;
    
    // PAL 8-field sequence discrimination using I/Q-demodulated burst phase
    // The phase on line 7 follows a specific pattern across the 8-field sequence
    // Combined with the 4-field position, we can determine the exact field phase
    
    // Observed phase patterns from I/Q demodulation:
    // Phase ~245-265°: corresponds to certain fields in the 8-field sequence
    // Phase ~65-85°: corresponds to other fields
    
    // Determine if fields 1-4 or 5-8 using burst phase on line 7
    // Following ld-decode logic: phase ~0-90° suggests first four fields (1-4)
    // phase ~180-270° suggests second four fields (5-8)
    // Using simplified threshold at 135° (midpoint between 90° and 180°)
    int final_phase;
    bool is_firstfour = (phase7 < 135.0 || phase7 > 315.0);  // Close to 0° (with wrapping)
    
    // Special case for phase_4field == 2: reverse the determination
    // This is a quirk of the PAL 8-field sequence (see ld-decode)
    if (phase_4field == 2) {
        is_firstfour = !is_firstfour;
    }
    
    // Calculate final phase ID (1-8)
    final_phase = phase_4field + (is_firstfour ? 0 : 4);
    
    obs->field_phase_id = final_phase;
    obs->confidence = ConfidenceLevel::HIGH;
    
    ORC_LOG_DEBUG("PALPhaseObserver: Field {} phase7={:.1f}° phase_4field={} is_firstfour={} → phase_id={}",
                  field_id.value(), phase7, phase_4field, is_firstfour, obs->field_phase_id);
    
    return {obs};
}

std::optional<double> PALPhaseObserver::get_burst_level(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    size_t line,
    const VideoParameters& video_params) const
{
    const auto* line_data = representation.get_line(field_id, line);
    if (!line_data) {
        return std::nullopt;
    }
    
    size_t burst_start = static_cast<size_t>(video_params.colour_burst_start);
    size_t burst_end = static_cast<size_t>(video_params.colour_burst_end);
    
    if (burst_end <= burst_start) {
        return std::nullopt;
    }
    
    // Collect burst samples
    std::vector<double> burst_samples;
    for (size_t idx = burst_start; idx <= burst_end; ++idx) {
        burst_samples.push_back(static_cast<double>(line_data[idx]));
    }
    
    if (burst_samples.size() < 4) {
        return std::nullopt;
    }
    
    // Calculate mean and subtract it
    double mean = std::accumulate(burst_samples.begin(), burst_samples.end(), 0.0) / burst_samples.size();
    
    std::vector<double> centered;
    for (double sample : burst_samples) {
        centered.push_back(sample - mean);
    }
    
    // Calculate RMS * sqrt(2) to get peak amplitude
    double rms = calculate_rms(centered);
    return rms * std::sqrt(2.0);
}

std::optional<double> PALPhaseObserver::measure_burst_phase(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    size_t line,
    const VideoParameters& video_params) const
{
    const auto* line_data = representation.get_line(field_id, line);
    if (!line_data) {
        return std::nullopt;
    }
    
    // PAL subcarrier frequency and sampling parameters
    const double fsc_hz = 4433618.75;  // PAL subcarrier frequency in Hz
    const double sample_rate_hz = video_params.sample_rate;
    const double angular_freq = 2.0 * M_PI * fsc_hz / sample_rate_hz;
    
    // Burst timing from video parameters
    size_t burst_start = static_cast<size_t>(video_params.colour_burst_start);
    size_t burst_end = static_cast<size_t>(video_params.colour_burst_end);
    
    if (burst_end <= burst_start || burst_end >= static_cast<size_t>(video_params.field_width)) {
        return std::nullopt;
    }
    
    size_t burst_len = burst_end - burst_start;
    if (burst_len < 8) {
        return std::nullopt;
    }
    
    // Extract burst samples and remove DC component
    std::vector<double> burst;
    double sum = 0.0;
    for (size_t i = burst_start; i < burst_end; ++i) {
        double sample = static_cast<double>(line_data[i]);
        burst.push_back(sample);
        sum += sample;
    }
    double mean = sum / burst.size();
    for (double& s : burst) {
        s -= mean;
    }
    
    // I/Q demodulation: correlate burst with sin and cos reference at subcarrier frequency
    // This acts as a phase detector locked to the subcarrier
    double I = 0.0;  // In-phase component
    double Q = 0.0;  // Quadrature component
    
    for (size_t i = 0; i < burst.size(); ++i) {
        double phase = angular_freq * (burst_start + i);
        I += burst[i] * std::cos(phase);
        Q += burst[i] * std::sin(phase);
    }
    
    // Normalize by burst length
    I /= burst.size();
    Q /= burst.size();
    
    // Calculate phase angle from I/Q components
    // atan2 gives phase in radians (-π to π)
    double phase_rad = std::atan2(Q, I);
    
    // Convert to degrees (0-360)
    double phase_deg = phase_rad * 180.0 / M_PI;
    if (phase_deg < 0) {
        phase_deg += 360.0;
    }
    
    return phase_deg;
}

double PALPhaseObserver::calculate_rms(const std::vector<double>& data) const {
    if (data.empty()) {
        return 0.0;
    }
    
    double sum_squares = 0.0;
    for (double val : data) {
        sum_squares += val * val;
    }
    
    return std::sqrt(sum_squares / data.size());
}

} // namespace orc
