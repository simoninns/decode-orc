#pragma once

#include "observer.h"
#include "tbc_metadata.h"
#include <optional>

namespace orc {

/**
 * Observation containing field parity detection result.
 * 
 * Field parity (first/second field, also known as odd/even field) is determined
 * by analyzing the VBlank sync pulse timing, matching ld-decode's processVBlank() logic.
 * 
 * For PAL:
 *   - First field: Gap between first EQ pulse and line 0 is ~0.5H
 *   - Second field: Gap is ~1.0H or ~2.0H
 * 
 * For NTSC:
 *   - First field: Gap is ~1.0H
 *   - Second field: Gap is ~0.5H
 */
struct FieldParityObservation : Observation {
    /// True if this is the first field (odd field), false if second field (even field)
    bool is_first_field;
    
    /// Confidence of the detection (0-100)
    /// 100 = high confidence from pulse analysis
    /// 50 = medium confidence from fallback method
    /// 0 = no confidence, detection failed
    int confidence_pct;
    
    FieldParityObservation(bool is_first, int conf)
        : is_first_field(is_first), confidence_pct(conf) {}
    
    std::string observation_type() const override { return "FieldParity"; }
};

/**
 * Observer that determines field parity (first/second field) by analyzing VBlank sync pulses.
 * 
 * This matches ld-decode's processVBlank() detection logic:
 * - Analyzes timing between VBlank pulses
 * - For PAL: First field has ~0.5H gap, second field has ~1.0H or ~2.0H
 * - For NTSC: First field has ~1.0H gap, second field has ~0.5H
 * 
 * This is essential for:
 * - Correct PAL phase detection (needs accurate line offsets)
 * - Handling non-sequential fields (dropped frames, editing)
 * - Architectural consistency (all metadata from signal analysis)
 */
class FieldParityObserver : public Observer {
public:
    FieldParityObserver() = default;
    
    std::string observer_name() const override {
        return "FieldParityObserver";
    }
    
    std::string observer_version() const override {
        return "1.0.0";
    }
    
    /**
     * Analyze VBlank sync pulses to determine field parity.
     * 
     * @param representation The field representation to analyze
     * @param field_id The field identifier
     * @return FieldParityObservation with is_first_field and confidence
     */
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id) override;

private:
    /**
     * Find sync pulses in the VBlank region (first ~20 lines).
     * Returns a vector of pulse locations (sample positions).
     */
    std::vector<size_t> find_sync_pulses(
        const std::vector<uint16_t>& field_data,
        const VideoParameters& video_params,
        size_t max_lines = 20) const;
    
    /**
     * Analyze pulse gaps to determine field parity for PAL.
     * Returns (is_first_field, confidence).
     */
    std::pair<bool, int> analyze_pal_parity(
        const std::vector<size_t>& pulses,
        const VideoParameters& video_params) const;
    
    /**
     * Analyze pulse gaps to determine field parity for NTSC.
     * Returns (is_first_field, confidence).
     */
    std::pair<bool, int> analyze_ntsc_parity(
        const std::vector<size_t>& pulses,
        const VideoParameters& video_params) const;
};

} // namespace orc
