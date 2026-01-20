/*
 * File:        observation_context.cpp
 * Module:      orc-core
 * Purpose:     Pipeline-scoped observation storage implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "include/observation_context.h"

namespace orc {

void ObservationContext::set(FieldID field_id, 
                              const std::string& namespace_, 
                              const std::string& key, 
                              const ObservationValue& value)
{
    observations_[field_id][namespace_][key] = value;
}

std::optional<ObservationValue> ObservationContext::get(FieldID field_id,
                                                        const std::string& namespace_, 
                                                        const std::string& key) const
{
    auto field_it = observations_.find(field_id);
    if (field_it == observations_.end()) {
        return std::nullopt;
    }
    
    auto ns_it = field_it->second.find(namespace_);
    if (ns_it == field_it->second.end()) {
        return std::nullopt;
    }
    
    auto key_it = ns_it->second.find(key);
    if (key_it == ns_it->second.end()) {
        return std::nullopt;
    }
    
    return key_it->second;
}

bool ObservationContext::has(FieldID field_id,
                              const std::string& namespace_, 
                              const std::string& key) const
{
    auto field_it = observations_.find(field_id);
    if (field_it == observations_.end()) {
        return false;
    }
    
    auto ns_it = field_it->second.find(namespace_);
    if (ns_it == field_it->second.end()) {
        return false;
    }
    
    return ns_it->second.find(key) != ns_it->second.end();
}

std::vector<std::string> ObservationContext::get_keys(FieldID field_id,
                                                      const std::string& namespace_) const
{
    std::vector<std::string> keys;
    
    auto field_it = observations_.find(field_id);
    if (field_it == observations_.end()) {
        return keys;
    }
    
    auto ns_it = field_it->second.find(namespace_);
    if (ns_it == field_it->second.end()) {
        return keys;
    }
    
    for (const auto& [key, value] : ns_it->second) {
        keys.push_back(key);
    }
    
    return keys;
}

std::vector<std::string> ObservationContext::get_namespaces(FieldID field_id) const
{
    std::vector<std::string> namespaces;
    
    auto field_it = observations_.find(field_id);
    if (field_it == observations_.end()) {
        return namespaces;
    }
    
    for (const auto& [ns, keys] : field_it->second) {
        namespaces.push_back(ns);
    }
    
    return namespaces;
}

std::map<std::string, std::map<std::string, ObservationValue>> 
ObservationContext::get_all_observations(FieldID field_id) const
{
    auto field_it = observations_.find(field_id);
    if (field_it == observations_.end()) {
        return {};
    }
    
    return field_it->second;
}

void ObservationContext::clear()
{
    observations_.clear();
}

void ObservationContext::clear_field(FieldID field_id)
{
    observations_.erase(field_id);
}

} // namespace orc
