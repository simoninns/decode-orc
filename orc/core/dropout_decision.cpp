/*
 * File:        dropout_decision.cpp
 * Module:      orc-core
 * Purpose:     Dropout decision management
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dropout_decision.h"
#include <algorithm>

namespace orc {

std::vector<DropoutRegion> DropoutDecisions::apply_decisions(
    FieldID field_id,
    const std::vector<DropoutRegion>& observations) const
{
    std::vector<DropoutRegion> result = observations;
    
    // Get all decisions for this field
    auto field_decisions = get_decisions_for_field(field_id);
    
    for (const auto& decision : field_decisions) {
        switch (decision.action) {
            case DropoutDecision::Action::ADD:
            {
                // Add a new dropout region
                DropoutRegion new_region;
                new_region.line = decision.line;
                new_region.start_sample = decision.start_sample;
                new_region.end_sample = decision.end_sample;
                new_region.basis = DropoutRegion::DetectionBasis::SAMPLE_DERIVED;  // User decision
                result.push_back(new_region);
                break;
            }
            
            case DropoutDecision::Action::REMOVE:
            {
                // Remove dropout regions that overlap with this decision
                result.erase(
                    std::remove_if(result.begin(), result.end(),
                        [&decision](const DropoutRegion& region) {
                            if (region.line != decision.line) {
                                return false;
                            }
                            // Check for overlap
                            uint32_t overlap_start = std::max(region.start_sample, decision.start_sample);
                            uint32_t overlap_end = std::min(region.end_sample, decision.end_sample);
                            return overlap_start < overlap_end;
                        }),
                    result.end());
                break;
            }
            
            case DropoutDecision::Action::MODIFY:
            {
                // Find and modify matching dropout regions
                for (auto& region : result) {
                    if (region.line != decision.line) {
                        continue;
                    }
                    
                    // Check for overlap
                    uint32_t overlap_start = std::max(region.start_sample, decision.start_sample);
                    uint32_t overlap_end = std::min(region.end_sample, decision.end_sample);
                    
                    if (overlap_start < overlap_end) {
                        // Modify the region boundaries
                        region.start_sample = decision.start_sample;
                        region.end_sample = decision.end_sample;
                    }
                }
                break;
            }
        }
    }
    
    // Sort by line and start sample for consistency
    std::sort(result.begin(), result.end(),
        [](const DropoutRegion& a, const DropoutRegion& b) {
            if (a.line != b.line) {
                return a.line < b.line;
            }
            return a.start_sample < b.start_sample;
        });
    
    return result;
}

} // namespace orc
