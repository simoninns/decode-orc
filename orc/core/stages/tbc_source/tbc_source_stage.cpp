/******************************************************************************
 * tbc_source_stage.cpp
 *
 * TBC Source Stage Implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "tbc_source_stage.h"
#include <stage_registry.h>
#include <stdexcept>

namespace orc {

// Register this stage with the registry
static StageRegistration tbc_source_registration([]() {
    return std::make_shared<TBCSourceStage>();
});

std::vector<ArtifactPtr> TBCSourceStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, std::string>& parameters
) {
    // Source stage should have no inputs
    if (!inputs.empty()) {
        throw std::runtime_error("TBCSource stage should have no inputs");
    }

    // Get tbc_path parameter
    auto tbc_path_it = parameters.find("tbc_path");
    if (tbc_path_it == parameters.end()) {
        throw std::runtime_error("TBCSource stage requires 'tbc_path' parameter");
    }
    std::string tbc_path = tbc_path_it->second;

    // Get db_path parameter (optional)
    std::string db_path;
    auto db_path_it = parameters.find("db_path");
    if (db_path_it != parameters.end()) {
        db_path = db_path_it->second;
    } else {
        // Default: tbc_path + ".json"
        db_path = tbc_path + ".json";
    }

    // Check cache
    if (cached_representation_ && cached_tbc_path_ == tbc_path) {
        // Return cached representation
        return {cached_representation_};
    }

    // Load the TBC file
    try {
        cached_representation_ = create_tbc_representation(tbc_path, db_path);
        cached_tbc_path_ = tbc_path;
        return {cached_representation_};
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to load TBC file '") + tbc_path + "': " + e.what()
        );
    }
}

} // namespace orc
