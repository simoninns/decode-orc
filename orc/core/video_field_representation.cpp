/*
 * File:        video_field_representation.cpp
 * Module:      orc-core
 * Purpose:     Video field representation interface
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */


#include "video_field_representation.h"
#include "logging.h"

namespace orc {

VideoFieldRepresentationWrapper::VideoFieldRepresentationWrapper(
    std::shared_ptr<const VideoFieldRepresentation> source,
    ArtifactID id,
    Provenance prov)
    : VideoFieldRepresentation(std::move(id), std::move(prov))
    , source_(std::move(source))
    , cached_video_params_(source_ ? source_->get_video_parameters() : std::nullopt)
{
}

} // namespace orc
