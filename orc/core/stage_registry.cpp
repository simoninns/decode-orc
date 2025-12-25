/*
 * File:        stage_registry.cpp
 * Module:      orc-core
 * Purpose:     Stage type registration
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */


#include "stage_registry.h"
#include <algorithm>

namespace orc {

// Forward declaration of force linking function
void force_stage_linking();

StageRegistry& StageRegistry::instance() {
    static StageRegistry registry;
    static bool initialized = false;
    if (!initialized) {
        force_stage_linking();
        initialized = true;
    }
    return registry;
}

void StageRegistry::register_stage(const std::string& stage_name, StageFactory factory) {
    if (factories_.find(stage_name) != factories_.end()) {
        throw StageRegistryError("Stage already registered: " + stage_name);
    }
    factories_[stage_name] = factory;
}

DAGStagePtr StageRegistry::create_stage(const std::string& stage_name) const {
    auto it = factories_.find(stage_name);
    if (it == factories_.end()) {
        throw StageRegistryError("Unknown stage: " + stage_name);
    }
    return it->second();
}

bool StageRegistry::has_stage(const std::string& stage_name) const {
    return factories_.find(stage_name) != factories_.end();
}

std::vector<std::string> StageRegistry::get_registered_stages() const {
    std::vector<std::string> names;
    names.reserve(factories_.size());
    for (const auto& pair : factories_) {
        names.push_back(pair.first);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string StageRegistry::get_default_transform_stage() {
    return "dropout_correct";
}

void StageRegistry::clear() {
    factories_.clear();
}

} // namespace orc
