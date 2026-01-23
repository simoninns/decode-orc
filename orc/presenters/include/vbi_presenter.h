/*
 * File:        vbi_presenter.h
 * Module:      orc-presenters
 * Purpose:     VBI observation presenter - MVP architecture
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <node_id.h>
#include <field_id.h>
#include "vbi_view_models.h"

namespace orc {
    class DAG;
    enum class VbiSoundMode;
}

namespace orc::presenters {

class VbiPresenter {
public:
    explicit VbiPresenter(std::function<std::shared_ptr<const orc::DAG>()> dag_provider);
    ~VbiPresenter();

    VbiPresenter(const VbiPresenter&) = delete;
    VbiPresenter& operator=(const VbiPresenter&) = delete;
    VbiPresenter(VbiPresenter&&) noexcept;
    VbiPresenter& operator=(VbiPresenter&&) noexcept;

    // Fetch VBI for a single field; returns empty if unavailable
    std::optional<VBIFieldInfoView> getVbiForField(NodeID node_id, FieldID field_id) const;

    // Fetch VBI for both fields of a frame; if only one available, returns the available ones
    struct FrameVbiResult {
        std::optional<VBIFieldInfoView> field1;
        std::optional<VBIFieldInfoView> field2;
    };
    FrameVbiResult getVbiForFrame(NodeID node_id, FieldID field1, FieldID field2) const;

    // Public helper for sound mode conversion (for use in callbacks)
    static VbiSoundModeView mapSoundMode(orc::VbiSoundMode mode);

    // Static method for decoding VBI from observation context
    // This allows render_coordinator to decode VBI without including core headers
    static std::optional<VBIFieldInfoView> decodeVbiFromObservation(
        const void* observation_context_ptr,  // orc::ObservationContext* but opaque
        FieldID field_id);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orc::presenters
