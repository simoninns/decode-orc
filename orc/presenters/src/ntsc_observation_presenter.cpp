/*
 * File:        ntsc_observation_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     NTSC observation presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "../include/ntsc_observation_presenter.h"
#include "../../core/include/observation_context.h"
#include <variant>

namespace orc::presenters {

NtscFieldObservationsView NtscObservationPresenter::extractFieldObservations(
    FieldID field_id,
    const void* obs_context_handle)
{
    const auto* obs_context = static_cast<const orc::ObservationContext*>(obs_context_handle);
    NtscFieldObservationsView result;
    
    result.fm_code = extractFMCode(field_id, obs_context_handle);
    result.white_flag = extractWhiteFlag(field_id, obs_context_handle);
    
    return result;
}

std::optional<FMCodeView> NtscObservationPresenter::extractFMCode(
    FieldID field_id,
    const void* obs_context_handle)
{
    const auto* obs_context = static_cast<const orc::ObservationContext*>(obs_context_handle);
    // Get FM code observations (namespace "fm_code")
    auto present_obs = obs_context->get(field_id, "fm_code", "present");
    auto data_obs = obs_context->get(field_id, "fm_code", "data_value");
    auto flag_obs = obs_context->get(field_id, "fm_code", "field_flag");
    
    // Only return data if we have at least the present flag
    if (!present_obs) {
        return std::nullopt;
    }
    
    FMCodeView fm_code;
    
    // Extract present flag
    if (std::holds_alternative<bool>(*present_obs)) {
        fm_code.present = std::get<bool>(*present_obs);
    }
    
    // Extract data value
    if (data_obs && std::holds_alternative<int32_t>(*data_obs)) {
        fm_code.data_value = std::get<int32_t>(*data_obs);
    }
    
    // Extract field flag
    if (flag_obs && std::holds_alternative<bool>(*flag_obs)) {
        fm_code.field_flag = std::get<bool>(*flag_obs);
    }
    
    return fm_code;
}

std::optional<WhiteFlagView> NtscObservationPresenter::extractWhiteFlag(
    FieldID field_id,
    const void* obs_context_handle)
{
    const auto* obs_context = static_cast<const orc::ObservationContext*>(obs_context_handle);
    // Get white flag observations (namespace "white_flag")
    auto present_obs = obs_context->get(field_id, "white_flag", "present");
    
    if (!present_obs) {
        return std::nullopt;
    }
    
    WhiteFlagView white_flag;
    
    // Extract present flag
    if (std::holds_alternative<bool>(*present_obs)) {
        white_flag.present = std::get<bool>(*present_obs);
    }
    
    return white_flag;
}

} // namespace orc::presenters
