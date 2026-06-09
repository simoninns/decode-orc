/*
 * File:        tbc_source_representation_factory.h
 * Module:      orc-core
 * Purpose:     Public factory entry points for TBC-backed source
 * representations
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <memory>
#include <string>

#include "video_field_representation.h"

namespace orc {

std::shared_ptr<VideoFieldRepresentation> create_tbc_source_representation(
    const std::string& tbc_filename, const std::string& metadata_filename,
    const std::string& pcm_filename = "", const std::string& efm_filename = "",
    const std::string& ac3rf_filename = "");

std::shared_ptr<VideoFieldRepresentation> create_tbc_yc_source_representation(
    const std::string& y_filename, const std::string& c_filename,
    const std::string& metadata_filename, const std::string& pcm_filename = "",
    const std::string& efm_filename = "",
    const std::string& ac3rf_filename = "");

}  // namespace orc