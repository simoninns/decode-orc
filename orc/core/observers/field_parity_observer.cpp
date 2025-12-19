#include "field_parity_observer.h"
#include "logging.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace orc {

std::vector<std::shared_ptr<Observation>> FieldParityObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id) {
    
    std::vector<std::shared_ptr<Observation>> observations;
    
    // Get video parameters
    auto video_params_opt = representation.get_video_parameters();
    if (!video_params_opt.has_value()) {
        // No video params - use fallback (field_id % 2)
        bool is_first = (field_id.value() % 2 == 0);
        auto obs = std::make_shared<FieldParityObservation>(is_first, 0);
        obs->field_id = field_id;
        obs->detection_basis = DetectionBasis::SAMPLE_DERIVED;
        obs->confidence = ConfidenceLevel::NONE;
        obs->observer_version = observer_version();
        observations.push_back(obs);
        return observations;
    }
    
    const auto& video_params = video_params_opt.value();
    
    // Get the field data
    auto field_data = representation.get_field(field_id);
    if (field_data.empty()) {
        // No data, can't determine parity - use fallback
        bool is_first = (field_id.value() % 2 == 0);
        auto obs = std::make_shared<FieldParityObservation>(is_first, 25);
        obs->field_id = field_id;
        obs->detection_basis = DetectionBasis::SAMPLE_DERIVED;
        obs->confidence = ConfidenceLevel::LOW;
        obs->observer_version = observer_version();
        observations.push_back(obs);
        return observations;
    }
    
    // Find sync pulses in VBlank region
    std::vector<size_t> pulses = find_sync_pulses(field_data, video_params);
    
    if (pulses.size() < 4) {
        // Not enough pulses to analyze - use fallback
        bool is_first = (field_id.value() % 2 == 0);
        auto obs = std::make_shared<FieldParityObservation>(is_first, 25);
        obs->field_id = field_id;
        obs->detection_basis = DetectionBasis::SAMPLE_DERIVED;
        obs->confidence = ConfidenceLevel::LOW;
        obs->observer_version = observer_version();
        observations.push_back(obs);
        return observations;
    }
    
    // Analyze based on video system
    bool is_first_field;
    int confidence_pct;
    
    if (video_params.system == VideoSystem::PAL) {
        std::tie(is_first_field, confidence_pct) = analyze_pal_parity(pulses, video_params);
    } else {
        std::tie(is_first_field, confidence_pct) = analyze_ntsc_parity(pulses, video_params);
    }
    
    // Map confidence percentage to ConfidenceLevel
    ConfidenceLevel conf_level;
    if (confidence_pct >= 75) {
        conf_level = ConfidenceLevel::HIGH;
    } else if (confidence_pct >= 50) {
        conf_level = ConfidenceLevel::MEDIUM;
    } else if (confidence_pct >= 25) {
        conf_level = ConfidenceLevel::LOW;
    } else {
        conf_level = ConfidenceLevel::NONE;
    }
    
    auto obs = std::make_shared<FieldParityObservation>(is_first_field, confidence_pct);
    obs->field_id = field_id;
    obs->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    obs->confidence = conf_level;
    obs->observer_version = observer_version();
    
    ORC_LOG_DEBUG("FieldParityObserver: Field {} is_first_field={} (confidence={}%)",
                  field_id.value(), is_first_field, confidence_pct);
    
    observations.push_back(obs);
    return observations;
}

std::vector<size_t> FieldParityObserver::find_sync_pulses(
    const std::vector<uint16_t>& field_data,
    const VideoParameters& video_params,
    size_t max_lines) const {
    
    std::vector<size_t> pulses;
    
    // Estimate samples per line from field_width
    size_t samples_per_line = video_params.field_width;
    
    // Search first max_lines worth of samples
    size_t search_samples = std::min(field_data.size(), samples_per_line * max_lines);
    
    // Find sync level (black level)
    // Sync tips are typically at 0 IRE (below black level)
    const uint16_t black_level = video_params.black_16b_ire;
    const uint16_t white_level = video_params.white_16b_ire;
    const double ire_per_unit = 100.0 / (white_level - black_level);
    
    // Sync tips are below black level
    // Allow some threshold for noise
    const uint16_t sync_threshold = black_level - static_cast<uint16_t>(5.0 / ire_per_unit);
    
    // Find all samples below sync threshold
    bool in_pulse = false;
    size_t pulse_start = 0;
    
    for (size_t i = 0; i < search_samples; ++i) {
        bool below_threshold = field_data[i] < sync_threshold;
        
        if (below_threshold && !in_pulse) {
            // Start of pulse
            pulse_start = i;
            in_pulse = true;
        } else if (!below_threshold && in_pulse) {
            // End of pulse - record the middle
            size_t pulse_middle = (pulse_start + i) / 2;
            pulses.push_back(pulse_middle);
            in_pulse = false;
        }
    }
    
    return pulses;
}

std::pair<bool, int> FieldParityObserver::analyze_pal_parity(
    const std::vector<size_t>& pulses,
    const VideoParameters& video_params) const {
    
    // PAL field detection from ld-decode's processVBlank():
    // 
    // First field: Gap between first EQ pulse and line 0 is ~0.5H
    // Second field: Gap is ~1.0H or ~2.0H
    //
    // The code checks: inrange((gap1 / inlinelen), 0.45, 0.55)
    // If true, it's the first field
    
    double samples_per_line = video_params.field_width;
    
    // Look at the gap between early pulses
    // In VBlank, we have equalizing pulses, then vsync pulses, then more eq pulses
    // The pattern differs between first and second field
    
    if (pulses.size() < 4) {
        return {false, 0}; // Not enough data
    }
    
    // Check the first few pulse gaps
    std::vector<double> gaps;
    for (size_t i = 1; i < std::min(size_t(6), pulses.size()); ++i) {
        double gap = static_cast<double>(pulses[i] - pulses[i-1]);
        double gap_in_lines = gap / samples_per_line;
        gaps.push_back(gap_in_lines);
    }
    
    // For PAL:
    // - First field has gaps around 0.5H
    // - Second field has gaps around 1.0H or 2.0H
    
    // Count gaps in the 0.45-0.55H range (first field indicator)
    int half_line_gaps = 0;
    int full_line_gaps = 0;
    
    for (double gap : gaps) {
        if (gap >= 0.45 && gap <= 0.55) {
            half_line_gaps++;
        } else if (gap >= 0.95 && gap <= 1.05) {
            full_line_gaps++;
        } else if (gap >= 1.95 && gap <= 2.05) {
            full_line_gaps++;
        }
    }
    
    // More half-line gaps suggests first field
    // More full-line gaps suggests second field
    if (half_line_gaps > full_line_gaps) {
        return {true, 75}; // First field
    } else if (full_line_gaps > half_line_gaps) {
        return {false, 75}; // Second field
    } else {
        // Ambiguous - use first gap as tie-breaker
        if (!gaps.empty()) {
            bool is_first = (gaps[0] >= 0.45 && gaps[0] <= 0.55);
            return {is_first, 50};
        }
        return {false, 25};
    }
}

std::pair<bool, int> FieldParityObserver::analyze_ntsc_parity(
    const std::vector<size_t>& pulses,
    const VideoParameters& video_params) const {
    
    // NTSC field detection from ld-decode's processVBlank():
    //
    // First field: Gap is ~1.0H
    // Second field: Gap is ~0.5H
    //
    // The code checks: inrange((gap1 / inlinelen), 0.95, 1.05)
    // If true, it's the first field
    
    double samples_per_line = video_params.field_width;
    
    if (pulses.size() < 4) {
        return {false, 0}; // Not enough data
    }
    
    // Check the first few pulse gaps
    std::vector<double> gaps;
    for (size_t i = 1; i < std::min(size_t(6), pulses.size()); ++i) {
        double gap = static_cast<double>(pulses[i] - pulses[i-1]);
        double gap_in_lines = gap / samples_per_line;
        gaps.push_back(gap_in_lines);
    }
    
    // For NTSC:
    // - First field has gaps around 1.0H
    // - Second field has gaps around 0.5H
    
    int half_line_gaps = 0;
    int full_line_gaps = 0;
    
    for (double gap : gaps) {
        if (gap >= 0.45 && gap <= 0.55) {
            half_line_gaps++;
        } else if (gap >= 0.95 && gap <= 1.05) {
            full_line_gaps++;
        }
    }
    
    // More full-line gaps suggests first field for NTSC
    // More half-line gaps suggests second field for NTSC
    if (full_line_gaps > half_line_gaps) {
        return {true, 75}; // First field
    } else if (half_line_gaps > full_line_gaps) {
        return {false, 75}; // Second field
    } else {
        // Ambiguous - use first gap as tie-breaker
        if (!gaps.empty()) {
            bool is_first = (gaps[0] >= 0.95 && gaps[0] <= 1.05);
            return {is_first, 50};
        }
        return {false, 25};
    }
}

} // namespace orc
