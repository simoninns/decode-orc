/*
 * File:        biphase_observer.h
 * Module:      orc-core
 * Purpose:     Biphase VBI data extraction observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_BIPHASE_OBSERVER_H
#define ORC_CORE_BIPHASE_OBSERVER_H

#include "observer.h"
#include <memory>

namespace orc {

// Forward declarations
class ObservationContext;
class VideoFieldRepresentation;
class FieldID;

/**
 * @brief Observer that extracts biphase-encoded VBI data
 * 
 * This observer reads VBI data from the video field representation
 * and populates the observation context with raw biphase data.
 * The data can then be decoded by VBIDecoder or other analysis tools.
 */
class BiphaseObserver : public Observer {
public:
    BiphaseObserver() = default;
    ~BiphaseObserver() override = default;
    
    std::string observer_name() const override { return "BiphaseObserver"; }
    std::string observer_version() const override { return "1.0.0"; }
    
    void process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        ObservationContext& context) override;
    
    std::vector<ObservationKey> get_provided_observations() const override {
        return {
            {"biphase", "vbi_line_16", ObservationType::INT32, "VBI line 16 raw data"},
            {"biphase", "vbi_line_17", ObservationType::INT32, "VBI line 17 raw data"},
            {"biphase", "vbi_line_18", ObservationType::INT32, "VBI line 18 raw data"},
            {"biphase", "picture_number", ObservationType::INT32, "CAV picture number (if available)"},
            {"biphase", "chapter", ObservationType::INT32, "Chapter number (if available)"},
        };
    }
};

} // namespace orc

#endif // ORC_CORE_BIPHASE_OBSERVER_H

