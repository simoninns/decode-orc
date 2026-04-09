/*
 * File:        vfr_to_source_field_adapter.cpp
 * Module:      orc-core
 * Purpose:     Adapter — translates VideoFieldRepresentation fields to
 *              NnSourceField / NnVideoParameters for the NN NTSC decoder.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "vfr_to_source_field_adapter.h"
#include "logging.h"

namespace orc {

// ============================================================================
// extract_video_parameters
// ============================================================================

std::optional<nn_ntsc::NnVideoParameters>
VfrToSourceFieldAdapter::extract_video_parameters(const VideoFieldRepresentation &vfr)
{
    auto params_opt = vfr.get_video_parameters();
    if (!params_opt) {
        ORC_LOG_ERROR("VfrToSourceFieldAdapter: VFR has no video parameters");
        return std::nullopt;
    }

    const auto &src = *params_opt;
    if (!src.is_valid()) {
        ORC_LOG_ERROR("VfrToSourceFieldAdapter: SourceParameters are not valid "
                      "(system={}, field_width={})",
                      static_cast<int>(src.system), src.field_width);
        return std::nullopt;
    }

    nn_ntsc::NnVideoParameters nn;
    nn.fieldWidth            = src.field_width;
    nn.fieldHeight           = src.field_height;
    nn.colourBurstStart      = src.colour_burst_start;
    nn.colourBurstEnd        = src.colour_burst_end;
    nn.activeVideoStart      = src.active_video_start;
    nn.activeVideoEnd        = src.active_video_end;
    nn.firstActiveFrameLine  = src.first_active_frame_line;
    nn.lastActiveFrameLine   = src.last_active_frame_line;

    // IRE levels: SourceParameters stores them as int32_t; NnVideoParameters wants double
    nn.white16bIre = static_cast<double>(src.white_16b_ire);
    nn.black16bIre = static_cast<double>(src.black_16b_ire);

    nn.sampleRate  = src.sample_rate;
    nn.fSC         = src.fsc;
    nn.isWidescreen = src.is_widescreen;

    return nn;
}

// ============================================================================
// convert
// ============================================================================

std::optional<nn_ntsc::NnSourceField>
VfrToSourceFieldAdapter::convert(const VideoFieldRepresentation &vfr, FieldID field_id)
{
    if (!vfr.has_field(field_id)) {
        ORC_LOG_ERROR("VfrToSourceFieldAdapter: Field {} is not present in VFR",
                      field_id.value());
        return std::nullopt;
    }

    nn_ntsc::NnSourceField sf;
    sf.seq_no = static_cast<int32_t>(field_id.value());

    // --- is_first_field -------------------------------------------------------
    // Priority: parity hint (from metadata) > descriptor parity > index parity
    auto parity_hint = vfr.get_field_parity_hint(field_id);
    if (parity_hint) {
        sf.is_first_field = parity_hint->is_first_field;
    } else {
        auto desc_opt = vfr.get_descriptor(field_id);
        if (desc_opt) {
            sf.is_first_field = (desc_opt->parity == FieldParity::Top);
        } else {
            // Fallback: even field index → first/top field
            sf.is_first_field = (field_id.value() % 2 == 0);
        }
    }

    // --- field_phase_id -------------------------------------------------------
    auto phase_hint = vfr.get_field_phase_hint(field_id);
    sf.field_phase_id = phase_hint ? phase_hint->field_phase_id : 0;

    // --- raw sample data ------------------------------------------------------
    if (vfr.has_separate_channels()) {
        // Y/C source: the NN comb filter is trained on composite signals, so
        // feeding only the luma channel will produce poor-quality chroma.
        // A warning is logged so users understand the limitation.
        ORC_LOG_WARN("VfrToSourceFieldAdapter: Field {} belongs to a Y/C source. "
                     "NN chroma decoder expects a composite signal; using the luma "
                     "channel — chroma output quality will be reduced.",
                     field_id.value());
        sf.data = vfr.get_field_luma(field_id);
    } else {
        sf.data = vfr.get_field(field_id);
    }

    if (sf.data.empty()) {
        ORC_LOG_ERROR("VfrToSourceFieldAdapter: Field {} returned empty sample data",
                      field_id.value());
        return std::nullopt;
    }

    return sf;
}

} // namespace orc
