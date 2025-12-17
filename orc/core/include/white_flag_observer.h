// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025
// 
// Observer for NTSC white flag (line 11)
// Standard: IEC 60587-1986 §10.2.4

#ifndef ORC_CORE_WHITE_FLAG_OBSERVER_H
#define ORC_CORE_WHITE_FLAG_OBSERVER_H

#include "observer.h"

namespace orc {

class WhiteFlagObservation : public Observation {
public:
    bool white_flag_present = false;
    
    std::string observation_type() const override {
        return "WhiteFlag";
    }
};

class WhiteFlagObserver : public Observer {
public:
    WhiteFlagObserver() = default;
    
    std::string observer_name() const override { return "WhiteFlagObserver"; }
    std::string observer_version() const override { return "1.0.0"; }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id) override;
};

} // namespace orc

#endif // ORC_CORE_WHITE_FLAG_OBSERVER_H
