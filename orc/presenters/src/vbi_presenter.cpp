/*
 * File:        vbi_presenter.cpp
 * Module:      orc-presenters
 * Purpose:     VBI observation presenter implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include "vbi_presenter.h"
#include "vbi_view_models.h"
#include "../core/include/vbi_decoder.h"
#include "../core/include/dag_field_renderer.h"
#include "../core/include/video_field_representation.h"
#include "../core/include/vbi_types.h"

namespace orc::presenters {

class VbiPresenter::Impl {
public:
    explicit Impl(std::function<std::shared_ptr<const orc::DAG>()> dag_provider)
        : dag_provider_(std::move(dag_provider)) {}

    std::function<std::shared_ptr<const orc::DAG>()> dag_provider_;
};

VbiPresenter::VbiPresenter(std::function<std::shared_ptr<const orc::DAG>()> dag_provider)
    : impl_(std::make_unique<Impl>(std::move(dag_provider))) {}

VbiPresenter::~VbiPresenter() = default;
VbiPresenter::VbiPresenter(VbiPresenter&&) noexcept = default;
VbiPresenter& VbiPresenter::operator=(VbiPresenter&&) noexcept = default;

VbiSoundModeView VbiPresenter::mapSoundMode(orc::VbiSoundMode m) {
    switch (m) {
        case orc::VbiSoundMode::STEREO: return VbiSoundModeView::STEREO;
        case orc::VbiSoundMode::MONO: return VbiSoundModeView::MONO;
        case orc::VbiSoundMode::AUDIO_SUBCARRIERS_OFF: return VbiSoundModeView::AUDIO_SUBCARRIERS_OFF;
        case orc::VbiSoundMode::BILINGUAL: return VbiSoundModeView::BILINGUAL;
        case orc::VbiSoundMode::STEREO_STEREO: return VbiSoundModeView::STEREO_STEREO;
        case orc::VbiSoundMode::STEREO_BILINGUAL: return VbiSoundModeView::STEREO_BILINGUAL;
        case orc::VbiSoundMode::CROSS_CHANNEL_STEREO: return VbiSoundModeView::CROSS_CHANNEL_STEREO;
        case orc::VbiSoundMode::BILINGUAL_BILINGUAL: return VbiSoundModeView::BILINGUAL_BILINGUAL;
        case orc::VbiSoundMode::MONO_DUMP: return VbiSoundModeView::MONO_DUMP;
        case orc::VbiSoundMode::STEREO_DUMP: return VbiSoundModeView::STEREO_DUMP;
        case orc::VbiSoundMode::BILINGUAL_DUMP: return VbiSoundModeView::BILINGUAL_DUMP;
        case orc::VbiSoundMode::FUTURE_USE: return VbiSoundModeView::FUTURE_USE;
        default: return VbiSoundModeView::UNKNOWN;
    }
}

namespace {

    VBIFieldInfoView toView(const orc::VBIFieldInfo& src) {
        VBIFieldInfoView v;
        v.field_id = src.field_id.value();
        v.vbi_data = src.vbi_data;
        v.picture_number = src.picture_number;
        if (src.clv_timecode.has_value()) {
            CLVTimecodeView tc{};
            tc.hours = src.clv_timecode->hours;
            tc.minutes = src.clv_timecode->minutes;
            tc.seconds = src.clv_timecode->seconds;
            tc.picture_number = src.clv_timecode->picture_number;
            v.clv_timecode = tc;
        }
        v.chapter_number = src.chapter_number;
        v.stop_code_present = src.stop_code_present;
        v.lead_in = src.lead_in;
        v.lead_out = src.lead_out;
        v.user_code = src.user_code;

        if (src.programme_status.has_value()) {
            ProgrammeStatusView ps{};
            ps.cx_enabled = src.programme_status->cx_enabled;
            ps.is_12_inch = src.programme_status->is_12_inch;
            ps.is_side_1 = src.programme_status->is_side_1;
            ps.has_teletext = src.programme_status->has_teletext;
            ps.is_digital = src.programme_status->is_digital;
            ps.sound_mode = VbiPresenter::mapSoundMode(src.programme_status->sound_mode);
            ps.is_fm_multiplex = src.programme_status->is_fm_multiplex;
            ps.is_programme_dump = src.programme_status->is_programme_dump;
            ps.parity_valid = src.programme_status->parity_valid;
            v.programme_status = ps;
        }

        if (src.amendment2_status.has_value()) {
            Amendment2StatusView a{};
            a.copy_permitted = src.amendment2_status->copy_permitted;
            a.is_video_standard = src.amendment2_status->is_video_standard;
            a.sound_mode = VbiPresenter::mapSoundMode(src.amendment2_status->sound_mode);
            v.amendment2_status = a;
        }

        v.has_vbi_data = src.has_vbi_data;
        v.error_message = src.error_message;
        return v;
    }
}

std::optional<VBIFieldInfoView> VbiPresenter::getVbiForField(NodeID node_id, FieldID field_id) const
{
    auto dag = impl_->dag_provider_ ? impl_->dag_provider_() : nullptr;
    if (!dag) return std::nullopt;

    try {
        DAGFieldRenderer renderer(dag);
        auto render_result = renderer.render_field_at_node(node_id, field_id);
        if (!render_result.is_valid || !render_result.representation) return std::nullopt;

        const auto& obs = renderer.get_observation_context();
        auto vbi = VBIDecoder::decode_vbi(obs, field_id);
        if (!vbi.has_value()) return std::nullopt;
        return toView(*vbi);
    } catch (...) {
        return std::nullopt;
    }
}

VbiPresenter::FrameVbiResult VbiPresenter::getVbiForFrame(NodeID node_id, FieldID field1, FieldID field2) const
{
    FrameVbiResult out;
    out.field1 = getVbiForField(node_id, field1);
    out.field2 = getVbiForField(node_id, field2);
    return out;
}

} // namespace orc::presenters