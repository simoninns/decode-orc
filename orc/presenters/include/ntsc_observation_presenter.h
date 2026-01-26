/*
 * File:        ntsc_observation_presenter.h
 * Module:      orc-presenters
 * Purpose:     NTSC observation presenter - MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <field_id.h>
#include "ntsc_observation_view_models.h"

// Forward declare core type to avoid exposing it in GUI layer
namespace orc {
    class ObservationContext;
}

namespace orc::presenters {

/**
 * @brief Presenter for NTSC-specific observations
 * 
 * Extracts NTSC-specific observation data (FM code, white flag)
 * from the core ObservationContext and presents it in a form
 * suitable for GUI consumption.
 * 
 * This presenter hides the complexity of the variant-based
 * observation storage from the GUI layer.
 */
class NtscObservationPresenter {
public:
    /**
     * @brief Extract NTSC observations for a single field
     * 
     * @param field_id Field to extract observations for
     * @param obs_context_ptr Opaque pointer to observation context
     * @return NTSC observations (may have empty optionals if data unavailable)
     */
    static NtscFieldObservationsView extractFieldObservations(
        FieldID field_id,
        const void* obs_context_ptr
    );

private:
    /**
     * @brief Extract FM code data from observation context
     */
    static std::optional<FMCodeView> extractFMCode(
        FieldID field_id,
        const void* obs_context_ptr
    );

    /**
     * @brief Extract white flag data from observation context
     */
    static std::optional<WhiteFlagView> extractWhiteFlag(
        FieldID field_id,
        const void* obs_context_ptr
    );
};

} // namespace orc::presenters
