/*
 * File:        vfr_to_source_field_adapter.h
 * Module:      orc-core
 * Purpose:     Adapter interface — translates decode-orc VideoFieldRepresentation
 *              to the NN NTSC decoder's NnSourceField/NnVideoParameters types.
 *
 *              Phase 3: Interface declaration only.
 *              Phase 4: Full implementation in vfr_to_source_field_adapter.cpp.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#pragma once

#include "video_field_representation.h"
#include "decoders/nn_source_field.h"
#include <memory>
#include <optional>

namespace orc {

/**
 * @brief Adapts a VideoFieldRepresentation field to the NN decoder's types
 *
 * Maps:
 *   SourceParameters  ->  nn_ntsc::NnVideoParameters
 *   VFR field buffer  ->  nn_ntsc::NnSourceField::data
 *   field index parity -> nn_ntsc::NnSourceField::is_first_field
 *
 * Implemented in Phase 4 (vfr_to_source_field_adapter.cpp).
 */
class VfrToSourceFieldAdapter {
public:
    /**
     * @brief Convert one field from a VFR to an NnSourceField
     *
     * @param vfr       Source video field representation
     * @param field_id  FieldID of the field to convert
     * @return          Populated NnSourceField, or nullopt if conversion fails
     */
    static std::optional<nn_ntsc::NnSourceField> convert(
        const VideoFieldRepresentation& vfr,
        FieldID field_id);

    /**
     * @brief Extract NnVideoParameters from a VFR's SourceParameters
     *
     * @param vfr   Source video field representation
     * @return      Populated NnVideoParameters, or nullopt if unavailable
     */
    static std::optional<nn_ntsc::NnVideoParameters> extract_video_parameters(
        const VideoFieldRepresentation& vfr);
};

} // namespace orc
