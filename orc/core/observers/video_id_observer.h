/*
 * File:        video_id_observer.h
 * Module:      orc-core
 * Purpose:     Video ID observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#ifndef ORC_CORE_VIDEO_ID_OBSERVER_H
#define ORC_CORE_VIDEO_ID_OBSERVER_H

#include "observer.h"

namespace orc {

class VideoIdObservation : public Observation {
public:
    uint16_t video_id_data = 0;  // 14-bit decoded message
    uint8_t word0 = 0;           // 2-bit word 0
    uint8_t word1 = 0;           // 4-bit word 1 (aspect ratio)
    uint8_t word2 = 0;           // 8-bit word 2 (CGMS-A, APS)
    
    std::string observation_type() const override {
        return "VideoID";
    }
};

class VideoIdObserver : public Observer {
public:
    VideoIdObserver() = default;
    
    std::string observer_name() const override { return "VideoIdObserver"; }
    std::string observer_version() const override { return "1.0.0"; }
    
    std::vector<std::shared_ptr<Observation>> process_field(
        const VideoFieldRepresentation& representation,
        FieldID field_id,
        const ObservationHistory& history) override;

private:
    bool decode_line(const uint16_t* line_data, size_t sample_count,
                    uint16_t zero_crossing, size_t colorburst_end,
                    double samples_per_bit, VideoIdObservation& observation);
};

} // namespace orc

#endif // ORC_CORE_VIDEO_ID_OBSERVER_H
