/*
 * File:        observation_wrapper_representation.cpp
 * Module:      orc-core
 * Purpose:     Wrapper that attaches observations to field representations
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "observation_wrapper_representation.h"

namespace orc {

ObservationWrapperRepresentation::ObservationWrapperRepresentation(
    std::shared_ptr<VideoFieldRepresentation> source,
    std::map<FieldID, std::vector<std::shared_ptr<Observation>>> observations_map
)
    : VideoFieldRepresentationWrapper(
        std::move(source),
        ArtifactID("observation_wrapper"),
        Provenance()
      )
    , observations_map_(std::move(observations_map))
{
}

std::vector<std::shared_ptr<Observation>> ObservationWrapperRepresentation::get_observations(FieldID id) const
{
    auto it = observations_map_.find(id);
    if (it != observations_map_.end()) {
        return it->second;
    }
    
    // Fall back to source observations if we don't have any for this field
    return source_->get_observations(id);
}

} // namespace orc
