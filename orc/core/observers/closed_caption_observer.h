/*
 * File:        closed_caption_observer.h
 * Module:      orc-core
 * Purpose:     Closed caption observer (EIA-608 line 21/22)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "observer.h"

namespace orc {

class ClosedCaptionObserver : public Observer {
public:
    ClosedCaptionObserver() = default;
    ~ClosedCaptionObserver() override = default;
    
    std::string observer_name() const override { return "ClosedCaptionObserver"; }
    std::string observer_version() const override { return "1.0.0"; }
    
    void process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        ObservationContext& context) override;
    
    std::vector<ObservationKey> get_provided_observations() const override;
    
private:
    struct DecodedCaption {
        uint8_t data0 = 0;
        uint8_t data1 = 0;
        bool parity_valid0 = false;
        bool parity_valid1 = false;
    };
    
    bool decode_line(const uint16_t* line_data,
                     size_t sample_count,
                     uint16_t zero_crossing,
                     size_t colorburst_end,
                     double samples_per_bit,
                     DecodedCaption& decoded) const;
};

} // namespace orc
