/*
 * File:        fm_code_observer.h
 * Module:      orc-core
 * Purpose:     FM code observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_FM_CODE_OBSERVER_H
#define ORC_CORE_FM_CODE_OBSERVER_H

#include "observer.h"

namespace orc {

class FmCodeObservation : public Observation {
public:
    uint32_t data_value = 0;  // 20-bit decoded data
    bool field_flag = false;  // Video field indicator bit
    
    std::string observation_type() const override {
        return "FmCode";
    }
};

class FmCodeObserver : public Observer {
public:
    FmCodeObserver() = default;
    
    std::string observer_name() const override { return "FmCodeObserver"; }
    std::string observer_version() const override { return "1.0.0"; }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) override;

private:
    bool decode_line(const uint16_t* line_data, size_t sample_count,
                    uint16_t zero_crossing, size_t active_start,
                    double jump_samples, FmCodeObservation& observation);
};

} // namespace orc

#endif // ORC_CORE_FM_CODE_OBSERVER_H
