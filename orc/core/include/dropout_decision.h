/*
 * File:        dropout_decision.h
 * Module:      orc-core
 * Purpose:     Dropout decision management
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include <field_id.h>
#include <string>
#include <vector>
#include <cstdint>

namespace orc {

/// Represents a dropout region
struct DropoutRegion {
    enum class DetectionBasis {
        SAMPLE_DERIVED,   ///< Detected from signal analysis
        HINT_DERIVED,     ///< From decoder hints
        CORROBORATED      ///< Both sample and hint agree
    };
    
    uint32_t line;
    uint32_t start_sample;
    uint32_t end_sample;
    DetectionBasis basis;
    
    DropoutRegion() 
        : line(0), start_sample(0), end_sample(0)
        , basis(DetectionBasis::HINT_DERIVED) {}
};

/// Represents a user decision to modify dropout detection
/// 
/// Decisions are deltas applied against TBC hints:
/// - ADD: Create a new dropout region
/// - REMOVE: Mark a false positive dropout as not a dropout
/// - MODIFY: Adjust the boundaries of a detected dropout
struct DropoutDecision {
    enum class Action {
        ADD,     ///< Add a new dropout region
        REMOVE,  ///< Remove a false positive
        MODIFY   ///< Modify region boundaries
    };
    
    FieldID field_id;
    uint32_t line;
    uint32_t start_sample;
    uint32_t end_sample;
    Action action;
    std::string notes;  ///< Optional user notes
    
    DropoutDecision(FieldID fid, uint32_t ln, uint32_t start, uint32_t end, 
                    Action act, const std::string& n = "")
        : field_id(fid), line(ln), start_sample(start), end_sample(end)
        , action(act), notes(n) {}
};

/// Collection of user decisions for dropout modification
class DropoutDecisions {
public:
    void add_decision(const DropoutDecision& decision) {
        decisions_.push_back(decision);
    }
    
    /// Get all decisions for a specific field
    std::vector<DropoutDecision> get_decisions_for_field(FieldID field_id) const {
        std::vector<DropoutDecision> result;
        for (const auto& decision : decisions_) {
            if (decision.field_id == field_id) {
                result.push_back(decision);
            }
        }
        return result;
    }
    
    /// Apply decisions to dropout regions from TBC hints
    /// Returns the modified list of dropout regions
    std::vector<DropoutRegion> apply_decisions(
        FieldID field_id,
        const std::vector<DropoutRegion>& observations) const;
    
    const std::vector<DropoutDecision>& get_all() const { return decisions_; }
    size_t size() const { return decisions_.size(); }
    bool empty() const { return decisions_.empty(); }
    
private:
    std::vector<DropoutDecision> decisions_;
};

} // namespace orc
