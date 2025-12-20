#include "field_parity_observer.h"
#include "observation_history.h"
#include "logging.h"
#include <algorithm>
#include <cmath>
#include <numeric>

namespace orc {

std::vector<std::shared_ptr<Observation>> FieldParityObserver::process_field(
    const VideoFieldRepresentation& representation,
    FieldID field_id,
    const ObservationHistory& history) {
    
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
    std::vector<ClassifiedPulse> pulses = find_sync_pulses(field_data, video_params);
    
    if (pulses.size() < 15) {
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
    
    // If confidence is 0 (detection failed), try to use previous field's parity
    // This matches ld-decode's behavior when processVBlank returns None
    if (confidence_pct == 0 && field_id.is_valid()) {
        // Try to get previous field's parity from history
        if (field_id.value() > 0) {
            FieldID prev_field_id(field_id.value() - 1);
            auto prev_obs = history.get_observation(prev_field_id, "FieldParity");
            
            if (prev_obs) {
                auto* prev_parity = dynamic_cast<FieldParityObservation*>(prev_obs.get());
                if (prev_parity) {
                    // Flip the previous field's parity (ld-decode's method)
                    is_first_field = !prev_parity->is_first_field;
                    confidence_pct = 60;  // Medium-high confidence for prev-field method
                    ORC_LOG_DEBUG("FieldParityObserver: Using previous field parity for field {}: is_first_field={}", 
                                  field_id.value(), is_first_field);
                }
            }
        }
        
        // If still no confidence (first field or no history), use field_id fallback
        if (confidence_pct == 0) {
            is_first_field = (field_id.value() % 2 == 0);
            confidence_pct = 50;  // Medium confidence for field_id fallback
            ORC_LOG_DEBUG("FieldParityObserver: Using field_id fallback for field {}: is_first_field={}", 
                          field_id.value(), is_first_field);
        }
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

// Pulse types matching ld-decode
enum PulseType {
    HSYNC = 0,
    EQPL1 = 1,
    VSYNC = 2,
    EQPL2 = 3
};

struct ClassifiedPulse {
    size_t position;  // Middle of pulse
    PulseType type;
};

std::vector<ClassifiedPulse> FieldParityObserver::find_sync_pulses(
    const std::vector<uint16_t>& field_data,
    const VideoParameters& video_params,
    size_t max_lines) const {
    
    std::vector<ClassifiedPulse> pulses;
    
    // Estimate samples per line from field_width
    size_t samples_per_line = video_params.field_width;
    
    // Search first max_lines worth of samples
    size_t search_samples = std::min(field_data.size(), samples_per_line * max_lines);
    
    // Find sync level (black level)
    const uint16_t black_level = video_params.black_16b_ire;
    const uint16_t white_level = video_params.white_16b_ire;
    const double ire_per_unit = 100.0 / (white_level - black_level);
    const uint16_t sync_threshold = black_level - static_cast<uint16_t>(5.0 / ire_per_unit);
    
    // Sample rate approximation
    double samples_per_us;
    if (video_params.system == VideoSystem::PAL) {
        samples_per_us = 17.7; // MHz
    } else {
        samples_per_us = 14.3; // MHz
    }
    
    // Pulse width ranges matching ld-decode
    double hsync_min = (4.7 - 1.75) * samples_per_us;
    double hsync_max = (4.7 + 2.0) * samples_per_us;
    double eq_min = (2.3 - 0.5) * samples_per_us;
    double eq_max = (2.3 + 0.5) * samples_per_us;
    double vsync_min = (27.1 * 0.5) * samples_per_us;
    double vsync_max = (27.3 + 1.0) * samples_per_us;
    
    // Find and classify all pulses
    bool in_pulse = false;
    size_t pulse_start = 0;
    
    int hsync_count = 0;
    int eq_count = 0;
    int vsync_count = 0;
    
    for (size_t i = 0; i < search_samples; ++i) {
        bool below_threshold = field_data[i] < sync_threshold;
        
        if (below_threshold && !in_pulse) {
            pulse_start = i;
            in_pulse = true;
        } else if (!below_threshold && in_pulse) {
            size_t pulse_width = i - pulse_start;
            size_t pulse_middle = (pulse_start + i) / 2;
            
            // Classify pulse by width (matching ld-decode)
            ClassifiedPulse cp;
            cp.position = pulse_middle;
            
            if (pulse_width >= hsync_min && pulse_width <= hsync_max) {
                cp.type = HSYNC;
                pulses.push_back(cp);
                hsync_count++;
            } else if (pulse_width >= eq_min && pulse_width <= eq_max) {
                // ld-decode uses EQPL1 before vsync, EQPL2 after
                // For simplicity, use EQPL1 initially
                cp.type = EQPL1;
                pulses.push_back(cp);
                eq_count++;
            } else if (pulse_width >= vsync_min && pulse_width <= vsync_max) {
                cp.type = VSYNC;
                pulses.push_back(cp);
                vsync_count++;
            }
            // Ignore pulses that don't match any type (noise)
            
            in_pulse = false;
        }
    }
    
    ORC_LOG_TRACE("find_sync_pulses: Found {} total pulses (HSYNC={}, EQ={}, VSYNC={})",
                  pulses.size(), hsync_count, eq_count, vsync_count);
    
    // Debug: Show first 10 pulse types
    std::string pulse_sequence;
    for (size_t i = 0; i < std::min(pulses.size(), size_t(15)); ++i) {
        if (i > 0) pulse_sequence += ",";
        switch (pulses[i].type) {
            case HSYNC: pulse_sequence += "H"; break;
            case EQPL1: pulse_sequence += "E"; break;
            case VSYNC: pulse_sequence += "V"; break;
            case EQPL2: pulse_sequence += "E2"; break;
        }
    }
    ORC_LOG_TRACE("Pulse sequence (first 15): {}", pulse_sequence);
    
    return pulses;
}


std::pair<bool, int> FieldParityObserver::analyze_pal_parity(
    const std::vector<ClassifiedPulse>& pulses,
    const VideoParameters& video_params) const {
    
    // Match ld-decode's processVBlank fallback algorithm exactly:
    // 1. Find vblank range using getBlankRange logic
    // 2. Calculate gap1 and gap2
    // 3. Check PAL conditions and determine field parity
    
    double samples_per_line = video_params.field_width;
    
    ORC_LOG_TRACE("PAL parity: analyze called with {} pulses", pulses.size());
    
    if (pulses.size() < 15) {
        ORC_LOG_TRACE("PAL parity: Not enough pulses ({} < 15)", pulses.size());
        return {false, 0};
    }
    
    // getBlankRange: Find VSYNC pulses
    int firstvsync = -1;
    for (size_t i = 0; i < pulses.size(); ++i) {
        if (pulses[i].type == VSYNC) {
            firstvsync = i;
            break;
        }
    }
    
    ORC_LOG_TRACE("PAL parity: firstvsync = {}", firstvsync);
    
    if (firstvsync < 0) {
        ORC_LOG_TRACE("PAL parity: No VSYNC found");
        return {false, 0};
    }
    
    // Find the vblank range - the field might start in vblank, so we need to handle both cases:
    // Case 1: Field starts before vblank (normal) - search backward from VSYNC
    // Case 2: Field starts in vblank - use pulses from start to where HSYNC resumes
    
    int firstblank = -1;
    int lastblank = -1;
    
    if (firstvsync >= 10) {
        // Case 1: Normal case - search backward from VSYNC
        for (int newstart = firstvsync - 10; newstart <= firstvsync - 4; ++newstart) {
            if (newstart < 0) continue;
        
        // Find first non-HSYNC pulse from newstart
        int fb = -1;
        for (size_t i = newstart; i < pulses.size(); ++i) {
            if (pulses[i].type != HSYNC) {
                fb = i;
                break;
            }
        }
        
        if (fb < 0) continue;
        
        // Find where HSYNC resumes after fb
        int lb = -1;
        for (size_t i = fb; i < pulses.size(); ++i) {
            if (pulses[i].type == HSYNC) {
                lb = i - 1;
                break;
            }
        }
        
        if (lb < 0) continue;
        
        // Check if vblank range is long enough (>12 pulses)
        if ((lb - fb) > 12) {
            firstblank = fb;
            lastblank = lb;
            break;
        }
    }
    } else {
        // Case 2: Field starts in vblank - cannot use getBlankRange method
        // ld-decode falls back to using previous field parity (flipped)
        // Since we don't have prevfield context, return None to trigger fallback
        ORC_LOG_TRACE("PAL parity: Field starts in vblank (VSYNC at {}), cannot determine from vblank", firstvsync);
        return {false, 0};  // Return confidence 0 to trigger field_id fallback
    }
    
    if (firstblank < 0 || lastblank < 0) {
        ORC_LOG_TRACE("PAL parity: Could not find valid vblank range");
        return {false, 0};
    }
    
    // Check bounds for gap calculation
    if (firstblank < 1 || lastblank >= static_cast<int>(pulses.size()) - 1) {
        ORC_LOG_TRACE("PAL parity: Cannot calculate gaps (firstblank={}, lastblank={}, size={})",
                      firstblank, lastblank, pulses.size());
        
        // Alternative for fields starting in vblank: use pulse positions directly
        // First HSYNC after vblank tells us the line offset
        if (lastblank < static_cast<int>(pulses.size()) - 1) {
            size_t first_hsync_pos = pulses[lastblank + 1].position;
            double line_offset = static_cast<double>(first_hsync_pos) / samples_per_line;
            
            // PAL first field: first HSYNC after vblank around line 23-25
            // PAL second field: first HSYNC after vblank around line 310-311 (or 23-25 in field coords)
            // Check modulo to determine field
            double line_in_frame = fmod(line_offset, 1.0);
            
            ORC_LOG_TRACE("PAL parity (alt): first HSYNC at line offset {:.3f}, fractional={:.3f}",
                          line_offset, line_in_frame);
            
            bool is_first_field = (line_in_frame >= 0.45 && line_in_frame <= 0.55);
            int confidence = 40;
            
            ORC_LOG_TRACE("PAL parity (alt): is_first_field={} (confidence={}%)",
                          is_first_field, confidence);
            
            return {is_first_field, confidence};
        }
        
        return {false, 0};
    }
    
    // Calculate gaps exactly as ld-decode does:
    // gap1 = pulses[firstblank].position - pulses[firstblank-1].position
    // gap2 = pulses[lastblank+1].position - pulses[lastblank].position
    double gap1 = static_cast<double>(pulses[firstblank].position - pulses[firstblank - 1].position);
    double gap2 = static_cast<double>(pulses[lastblank + 1].position - pulses[lastblank].position);
    double gap1_in_lines = gap1 / samples_per_line;
    double gap2_in_lines = gap2 / samples_per_line;
    
    // PAL condition: abs(gap2 - gap1) should be close to 0
    double gap_diff = std::abs(gap2_in_lines - gap1_in_lines);
    
    if (gap_diff > 0.3) {
        ORC_LOG_TRACE("PAL parity: gap1={:.3f}H, gap2={:.3f}H, diff={:.3f}H (too large)",
                      gap1_in_lines, gap2_in_lines, gap_diff);
        return {false, 25};
    }
    
    // PAL field determination: if gap1 in range [0.45, 0.55], it's first field
    bool is_first_field = (gap1_in_lines >= 0.45 && gap1_in_lines <= 0.55);
    
    ORC_LOG_TRACE("PAL parity: firstblank={}, lastblank={}, gap1={:.3f}H, gap2={:.3f}H, is_first={}",
                  firstblank, lastblank, gap1_in_lines, gap2_in_lines, is_first_field);
    
    return {is_first_field, 50};
}

std::pair<bool, int> FieldParityObserver::analyze_ntsc_parity(
    const std::vector<ClassifiedPulse>& pulses,
    const VideoParameters& video_params) const {
    
    // Match ld-decode's process VBlank fallback algorithm for NTSC
    double samples_per_line = video_params.field_width;
    
    if (pulses.size() < 15) {
        return {false, 0};
    }
    
    // Same getBlankRange logic as PAL
    int firstvsync = -1;
    for (size_t i = 0; i < pulses.size(); ++i) {
        if (pulses[i].type == VSYNC) {
            firstvsync = i;
            break;
        }
    }
    
    if (firstvsync < 0 || firstvsync < 10) {
        return {false, 0};
    }
    
    int firstblank = -1;
    int lastblank = -1;
    
    for (int newstart = firstvsync - 10; newstart <= firstvsync - 4; ++newstart) {
        if (newstart < 0) continue;
        
        int fb = -1;
        for (size_t i = newstart; i < pulses.size(); ++i) {
            if (pulses[i].type != HSYNC) {
                fb = i;
                break;
            }
        }
        
        if (fb < 0) continue;
        
        int lb = -1;
        for (size_t i = fb; i < pulses.size(); ++i) {
            if (pulses[i].type == HSYNC) {
                lb = i - 1;
                break;
            }
        }
        
        if (lb < 0) continue;
        
        if ((lb - fb) > 12) {
            firstblank = fb;
            lastblank = lb;
            break;
        }
    }
    
    if (firstblank < 0 || lastblank < 0) {
        ORC_LOG_TRACE("NTSC parity: Could not find valid vblank range");
        return {false, 0};
    }
    
    if (firstblank < 1 || lastblank >= static_cast<int>(pulses.size()) - 1) {
        ORC_LOG_TRACE("NTSC parity: Invalid vblank indices");
        return {false, 0};
    }
    
    double gap1 = static_cast<double>(pulses[firstblank].position - pulses[firstblank - 1].position);
    double gap2 = static_cast<double>(pulses[lastblank + 1].position - pulses[lastblank].position);
    double gap1_in_lines = gap1 / samples_per_line;
    double gap2_in_lines = gap2 / samples_per_line;
    
    // NTSC condition: abs(gap2 + gap1) should be in range [1.4, 1.6]
    double gap_sum = std::abs(gap2_in_lines + gap1_in_lines);
    
    if (gap_sum < 1.4 || gap_sum > 1.6) {
        ORC_LOG_TRACE("NTSC parity: gap1={:.3f}H, gap2={:.3f}H, sum={:.3f}H (out of range)",
                      gap1_in_lines, gap2_in_lines, gap_sum);
        return {false, 25};
    }
    
    // NTSC field determination: if gap1 in range [0.95, 1.05], it's first field
    bool is_first_field = (gap1_in_lines >= 0.95 && gap1_in_lines <= 1.05);
    
    ORC_LOG_TRACE("NTSC parity: firstblank={}, lastblank={}, gap1={:.3f}H, gap2={:.3f}H, is_first={}",
                  firstblank, lastblank, gap1_in_lines, gap2_in_lines, is_first_field);
    
    return {is_first_field, 50};
}


} // namespace orc
