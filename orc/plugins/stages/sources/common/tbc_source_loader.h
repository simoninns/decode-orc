/*
 * File:        tbc_source_loader.h
 * Module:      orc-core
 * Purpose:     Shared plugin-side loader helpers for TBC-backed source stages
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#pragma once

#include <string>
#include <memory>

#include <tbc_source_representation_factory.h>
#include <video_field_representation.h>

namespace orc::source_loader_shared {

inline std::shared_ptr<VideoFieldRepresentation> load_tbc_composite(
    const std::string& input_path,
    const std::string& db_path,
    const std::string& pcm_path,
    const std::string& efm_path,
    const std::string& ac3rf_path = "")
{
    return create_tbc_source_representation(input_path, db_path, pcm_path, efm_path, ac3rf_path);
}

inline std::shared_ptr<VideoFieldRepresentation> load_tbc_yc(
    const std::string& y_path,
    const std::string& c_path,
    const std::string& db_path,
    const std::string& pcm_path,
    const std::string& efm_path,
    const std::string& ac3rf_path = "")
{
    return create_tbc_yc_source_representation(y_path, c_path, db_path, pcm_path, efm_path, ac3rf_path);
}

} // namespace orc::source_loader_shared
