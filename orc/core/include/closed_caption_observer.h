/*
 * File:        closed_caption_observer.h
 * Module:      orc-core
 * Purpose:     Closed caption observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#ifndef ORC_CORE_CLOSED_CAPTION_OBSERVER_H
#define ORC_CORE_CLOSED_CAPTION_OBSERVER_H

#include "observer.h"

namespace orc {

class ClosedCaptionObservation : public Observation {
public:
    uint8_t data0 = 0;  // First 7-bit character + parity
    uint8_t data1 = 0;  // Second 7-bit character + parity
    bool parity_valid[2] = {false, false};
    
    std::string observation_type() const override {
        return "ClosedCaption";
    }
};

class ClosedCaptionObserver : public Observer {
public:
    ClosedCaptionObserver() = default;
    
    std::string observer_name() const override { return "ClosedCaptionObserver"; }
    std::string observer_version() const override { return "1.0.0"; }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id) override;

private:
    bool decode_line(const uint16_t* line_data, size_t sample_count,
                    uint16_t zero_crossing, size_t colorburst_end,
                    double samples_per_bit, ClosedCaptionObservation& observation);
};

} // namespace orc

#endif // ORC_CORE_CLOSED_CAPTION_OBSERVER_H
