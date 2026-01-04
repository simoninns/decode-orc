/*
 * File:        dropout_map_stage.cpp
 * Module:      orc-core
 * Purpose:     Dropout map stage implementation
 *
 * This stage modifies dropout hints without altering video data.
 * It allows per-field override of dropout regions - adding new dropouts,
 * removing false positives, or modifying boundaries.
 *
 * Hint Semantics: Outputs have modified dropout hints
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "dropout_map_stage.h"
#include "stage_registry.h"
#include "preview_helpers.h"
#include "logging.h"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace orc {

// Register the stage
ORC_REGISTER_STAGE(DropoutMapStage)

// Force linker to include this object file
void force_link_DropoutMapStage() {}

// ============================================================================
// DropoutMappedRepresentation Implementation
// ============================================================================

DropoutMappedRepresentation::DropoutMappedRepresentation(
    std::shared_ptr<const VideoFieldRepresentation> source,
    const std::map<uint64_t, FieldDropoutMap>& dropout_map)
    : VideoFieldRepresentationWrapper(
        source,
        ArtifactID("dropout_map_" + source->id().to_string() + 
                  "_" + std::to_string(reinterpret_cast<uintptr_t>(source.get()))),
        Provenance{
            "dropout_map",
            "1.0",
            {},  // Parameters stored in stage
            {source->id()},
            std::chrono::system_clock::now(),
            "",  // hostname
            "",  // user
            {}   // statistics
        })
    , dropout_map_(dropout_map)
{
}

std::vector<DropoutRegion> DropoutMappedRepresentation::get_dropout_hints(FieldID id) const {
    // Get source dropout hints
    std::vector<DropoutRegion> source_dropouts;
    if (source_) {
        source_dropouts = source_->get_dropout_hints(id);
    }
    
    // Check if we have modifications for this field
    auto it = dropout_map_.find(id.value());
    if (it == dropout_map_.end()) {
        // No modifications for this field, return source hints as-is
        return source_dropouts;
    }
    
    const FieldDropoutMap& modifications = it->second;
    return DropoutMapStage::apply_modifications(source_dropouts, modifications);
}

// ============================================================================
// DropoutMapStage Implementation
// ============================================================================

std::vector<ArtifactPtr> DropoutMapStage::execute(
    const std::vector<ArtifactPtr>& inputs,
    const std::map<std::string, ParameterValue>& parameters)
{
    ORC_LOG_INFO("DropoutMapStage::execute - starting with {} inputs", inputs.size());
    
    if (inputs.size() != 1) {
        throw std::runtime_error("DropoutMapStage requires exactly one input (ONE-to-ONE connection)");
    }
    
    // Extract parameters
    std::string dropout_map_str;
    if (parameters.count("dropout_map")) {
        dropout_map_str = std::get<std::string>(parameters.at("dropout_map"));
    }
    
    // Parse dropout map
    auto dropout_map = parse_dropout_map(dropout_map_str);
    
    ORC_LOG_INFO("DropoutMapStage: parsed {} field dropout mappings", dropout_map.size());
    
    // Process the single input
    auto source = std::dynamic_pointer_cast<const VideoFieldRepresentation>(inputs[0]);
    if (!source) {
        throw std::runtime_error("DropoutMapStage input must be VideoFieldRepresentation");
    }
    
    // Create wrapped representation with modified dropout hints
    auto mapped = std::make_shared<DropoutMappedRepresentation>(source, dropout_map);
    cached_output_ = mapped;
    
    ORC_LOG_INFO("DropoutMapStage: produced output with modified dropout hints");
    return {std::static_pointer_cast<VideoFieldRepresentation>(mapped)};
}

// ============================================================================
// Parameter Interface Implementation
// ============================================================================

std::vector<ParameterDescriptor> DropoutMapStage::get_parameter_descriptors(VideoSystem /*project_format*/) const
{
    std::vector<ParameterDescriptor> descriptors;
    
    // Dropout map parameter
    {
        ParameterDescriptor desc;
        desc.name = "dropout_map";
        desc.display_name = "Dropout Map";
        desc.description = "Per-field dropout overrides in JSON-like format: "
                          "[{field:0,add:[{line:10,start:100,end:200}],remove:[{line:15,start:50,end:75}]}]";
        desc.type = ParameterType::STRING;
        desc.constraints.default_value = std::string("[]");
        desc.constraints.required = false;
        descriptors.push_back(desc);
    }
    
    return descriptors;
}

std::map<std::string, ParameterValue> DropoutMapStage::get_parameters() const
{
    std::map<std::string, ParameterValue> params;
    params["dropout_map"] = dropout_map_str_;
    return params;
}

bool DropoutMapStage::set_parameters(const std::map<std::string, ParameterValue>& params)
{
    if (params.count("dropout_map")) {
        try {
            dropout_map_str_ = std::get<std::string>(params.at("dropout_map"));
            // Parse and cache the dropout map
            cached_dropout_map_ = parse_dropout_map(dropout_map_str_);
            return true;
        } catch (const std::exception& e) {
            ORC_LOG_ERROR("DropoutMapStage: failed to parse dropout_map parameter: {}", e.what());
            return false;
        }
    }
    return true;
}

// ============================================================================
// Preview Interface Implementation
// ============================================================================

std::vector<PreviewOption> DropoutMapStage::get_preview_options() const
{
    return PreviewHelpers::get_standard_preview_options(cached_output_);
}

PreviewImage DropoutMapStage::render_preview(const std::string& option_id, uint64_t index,
                                            PreviewNavigationHint hint) const
{
    auto start_time = std::chrono::high_resolution_clock::now();
    auto result = PreviewHelpers::render_standard_preview(cached_output_, option_id, index, hint);
    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();
    ORC_LOG_INFO("DropoutMap PREVIEW: option '{}' index {} rendered in {} ms (hint={})",
                 option_id, index, duration_ms, hint == PreviewNavigationHint::Sequential ? "Sequential" : "Random");
    return result;
}

// ============================================================================
// Parsing and Encoding Utilities
// ============================================================================

std::map<uint64_t, FieldDropoutMap> DropoutMapStage::parse_dropout_map(const std::string& map_str)
{
    std::map<uint64_t, FieldDropoutMap> result;
    
    if (map_str.empty() || map_str == "[]") {
        return result;  // Empty map
    }
    
    // Simple parser for format: [{field:N,add:[...],remove:[...]}]
    // This is a simplified parser - for production use, consider a JSON library
    
    size_t pos = 0;
    auto skip_whitespace = [&]() {
        while (pos < map_str.length() && std::isspace(map_str[pos])) {
            pos++;
        }
    };
    
    auto expect_char = [&](char c) -> bool {
        skip_whitespace();
        if (pos < map_str.length() && map_str[pos] == c) {
            pos++;
            return true;
        }
        return false;
    };
    
    auto parse_uint = [&]() -> uint32_t {
        skip_whitespace();
        uint32_t value = 0;
        while (pos < map_str.length() && std::isdigit(map_str[pos])) {
            value = value * 10 + (map_str[pos] - '0');
            pos++;
        }
        return value;
    };
    
    auto parse_dropout_region = [&]() -> DropoutRegion {
        DropoutRegion region;
        region.basis = DropoutRegion::DetectionBasis::HINT_DERIVED;
        
        if (!expect_char('{')) return region;
        
        while (pos < map_str.length() && map_str[pos] != '}') {
            skip_whitespace();
            
            // Parse key
            std::string key;
            while (pos < map_str.length() && std::isalpha(map_str[pos])) {
                key += map_str[pos++];
            }
            
            if (!expect_char(':')) break;
            
            if (key == "line") {
                region.line = parse_uint();
            } else if (key == "start") {
                region.start_sample = parse_uint();
            } else if (key == "end") {
                region.end_sample = parse_uint();
            }
            
            expect_char(',');  // Optional comma
        }
        
        expect_char('}');
        return region;
    };
    
    auto parse_dropout_list = [&]() -> std::vector<DropoutRegion> {
        std::vector<DropoutRegion> regions;
        if (!expect_char('[')) return regions;
        
        while (pos < map_str.length() && map_str[pos] != ']') {
            skip_whitespace();
            if (map_str[pos] == '{') {
                regions.push_back(parse_dropout_region());
            }
            expect_char(',');  // Optional comma
            skip_whitespace();
        }
        
        expect_char(']');
        return regions;
    };
    
    // Parse top-level array
    if (!expect_char('[')) {
        ORC_LOG_ERROR("DropoutMapStage: dropout_map must start with '['");
        return result;
    }
    
    while (pos < map_str.length() && map_str[pos] != ']') {
        skip_whitespace();
        
        if (!expect_char('{')) break;
        
        FieldDropoutMap field_map;
        
        // Parse field entry
        while (pos < map_str.length() && map_str[pos] != '}') {
            skip_whitespace();
            
            // Parse key
            std::string key;
            while (pos < map_str.length() && std::isalpha(map_str[pos])) {
                key += map_str[pos++];
            }
            
            if (!expect_char(':')) break;
            
            if (key == "field") {
                uint32_t field_num = parse_uint();
                field_map.field_id = FieldID(field_num);
            } else if (key == "add") {
                field_map.additions = parse_dropout_list();
            } else if (key == "remove") {
                field_map.removals = parse_dropout_list();
            }
            
            expect_char(',');  // Optional comma
        }
        
        expect_char('}');
        
        // Add to result map
        result[field_map.field_id.value()] = field_map;
        
        expect_char(',');  // Optional comma between entries
    }
    
    return result;
}

std::string DropoutMapStage::encode_dropout_map(const std::map<uint64_t, FieldDropoutMap>& map)
{
    if (map.empty()) {
        return "[]";
    }
    
    std::ostringstream oss;
    oss << "[";
    
    bool first_field = true;
    for (const auto& [field_num, field_map] : map) {
        if (!first_field) oss << ",";
        first_field = false;
        
        oss << "{field:" << field_num;
        
        // Encode additions
        if (!field_map.additions.empty()) {
            oss << ",add:[";
            bool first_region = true;
            for (const auto& region : field_map.additions) {
                if (!first_region) oss << ",";
                first_region = false;
                oss << "{line:" << region.line 
                    << ",start:" << region.start_sample 
                    << ",end:" << region.end_sample << "}";
            }
            oss << "]";
        }
        
        // Encode removals
        if (!field_map.removals.empty()) {
            oss << ",remove:[";
            bool first_region = true;
            for (const auto& region : field_map.removals) {
                if (!first_region) oss << ",";
                first_region = false;
                oss << "{line:" << region.line 
                    << ",start:" << region.start_sample 
                    << ",end:" << region.end_sample << "}";
            }
            oss << "]";
        }
        
        oss << "}";
    }
    
    oss << "]";
    return oss.str();
}

std::vector<DropoutRegion> DropoutMapStage::apply_modifications(
    const std::vector<DropoutRegion>& source_dropouts,
    const FieldDropoutMap& modifications)
{
    // Start with source dropouts
    std::vector<DropoutRegion> result = source_dropouts;
    
    // Remove specified dropouts
    // For each removal, we remove any source dropout that matches the line and overlaps the range
    for (const auto& removal : modifications.removals) {
        result.erase(
            std::remove_if(result.begin(), result.end(),
                [&removal](const DropoutRegion& region) {
                    // Remove if on same line and overlaps with removal region
                    if (region.line != removal.line) return false;
                    
                    // Check for overlap
                    return !(region.end_sample < removal.start_sample || 
                            region.start_sample > removal.end_sample);
                }),
            result.end()
        );
    }
    
    // Add new dropouts
    for (const auto& addition : modifications.additions) {
        result.push_back(addition);
    }
    
    // Sort by line, then by start_sample for consistency
    std::sort(result.begin(), result.end(),
        [](const DropoutRegion& a, const DropoutRegion& b) {
            if (a.line != b.line) return a.line < b.line;
            return a.start_sample < b.start_sample;
        });
    
    return result;
}

} // namespace orc
