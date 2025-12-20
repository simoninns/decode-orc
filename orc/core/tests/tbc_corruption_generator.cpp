/*
 * File:        tbc_corruption_generator.cpp
 * Module:      orc-core/tests
 * Purpose:     Generate corrupted TBC test data for disc mapper testing
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 */

#include "tbc_reader.h"
#include "tbc_metadata.h"
#include "logging.h"
#include "version.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <algorithm>
#include <cstring>

namespace orc {

struct CorruptionPattern {
    std::string name;
    std::string description;
    
    // Corruption parameters
    int skip_fields = 0;        // Number of fields to skip at once
    int repeat_fields = 0;      // Number of fields to repeat
    int gap_size = 0;           // Size of gaps to create
    double corruption_rate = 0.0; // Probability of corruption (0.0-1.0)
    
    // Specific field ranges to corrupt (if empty, use random)
    std::vector<std::pair<int, int>> corruption_ranges;
};

class TBCCorruptionGenerator {
public:
    TBCCorruptionGenerator(const std::string& input_tbc, 
                          const std::string& output_tbc,
                          const CorruptionPattern& pattern)
        : input_path_(input_tbc)
        , output_path_(output_tbc)
        , pattern_(pattern)
        , rng_(std::random_device{}()) {}
    
    bool generate() {
        ORC_LOG_INFO("=== TBC Corruption Generator ===");
        ORC_LOG_INFO("Input:  {}", input_path_);
        ORC_LOG_INFO("Output: {}", output_path_);
        ORC_LOG_INFO("Pattern: {}", pattern_.name);
        
        // Open input TBC
        TBCReader reader(input_path_);
        if (!reader.is_open()) {
            ORC_LOG_ERROR("Failed to open input TBC: {}", input_path_);
            return false;
        }
        
        auto metadata = reader.get_metadata();
        if (!metadata) {
            ORC_LOG_ERROR("Failed to read metadata from input TBC");
            return false;
        }
        
        auto video_params = reader.get_video_parameters();
        if (!video_params) {
            ORC_LOG_ERROR("Failed to get video parameters");
            return false;
        }
        
        ORC_LOG_INFO("Input TBC: {} fields, {}x{} samples",
                     metadata->number_of_fields,
                     video_params->field_width,
                     video_params->field_height);
        
        // Open output TBC
        std::ofstream output(output_path_, std::ios::binary);
        if (!output) {
            ORC_LOG_ERROR("Failed to open output TBC: {}", output_path_);
            return false;
        }
        
        // Apply corruption pattern
        std::vector<int> field_mapping = generate_field_mapping(metadata->number_of_fields);
        
        ORC_LOG_INFO("Corruption mapping: {} input fields -> {} output fields",
                     metadata->number_of_fields, field_mapping.size());
        
        // Write corrupted fields
        size_t field_size = video_params->field_width * video_params->field_height * sizeof(uint16_t);
        std::vector<uint16_t> field_buffer;
        field_buffer.resize(video_params->field_width * video_params->field_height);
        
        for (size_t i = 0; i < field_mapping.size(); ++i) {
            int source_field = field_mapping[i];
            
            if (source_field < 0) {
                // Gap - write black field
                std::fill(field_buffer.begin(), field_buffer.end(), 0);
                ORC_LOG_DEBUG("Field {}: GAP (black field)", i);
            } else {
                // Read source field
                if (!reader.read_field(source_field, field_buffer.data())) {
                    ORC_LOG_ERROR("Failed to read field {}", source_field);
                    return false;
                }
                if (source_field != static_cast<int>(i)) {
                    ORC_LOG_DEBUG("Field {}: source field {} ({})", 
                                  i, source_field, 
                                  source_field < static_cast<int>(i) ? "REPEAT" : "SKIP");
                }
            }
            
            // Write field
            output.write(reinterpret_cast<const char*>(field_buffer.data()), field_size);
        }
        
        output.close();
        
        // Update and write metadata
        update_metadata(metadata.get(), field_mapping);
        std::string metadata_path = output_path_ + ".json";
        if (!write_metadata(metadata_path, metadata.get())) {
            ORC_LOG_ERROR("Failed to write metadata to {}", metadata_path);
            return false;
        }
        
        ORC_LOG_INFO("Corruption generation complete!");
        print_statistics(field_mapping);
        
        return true;
    }

private:
    std::vector<int> generate_field_mapping(int total_fields) {
        std::vector<int> mapping;
        mapping.reserve(total_fields);
        
        std::uniform_real_distribution<double> dist(0.0, 1.0);
        
        int i = 0;
        while (i < total_fields) {
            // Check if we should corrupt this region
            bool should_corrupt = false;
            
            if (!pattern_.corruption_ranges.empty()) {
                // Check if current field is in a corruption range
                for (const auto& range : pattern_.corruption_ranges) {
                    if (i >= range.first && i <= range.second) {
                        should_corrupt = true;
                        break;
                    }
                }
            } else {
                // Random corruption based on rate
                should_corrupt = (dist(rng_) < pattern_.corruption_rate);
            }
            
            if (should_corrupt) {
                // Apply corruption
                if (pattern_.skip_fields > 0) {
                    // Skip fields (create gap)
                    ORC_LOG_DEBUG("Skipping {} fields at position {}", pattern_.skip_fields, i);
                    i += pattern_.skip_fields;
                    
                    // Add gap if specified
                    if (pattern_.gap_size > 0) {
                        for (int g = 0; g < pattern_.gap_size; ++g) {
                            mapping.push_back(-1); // -1 = gap
                        }
                    }
                } else if (pattern_.repeat_fields > 0) {
                    // Repeat fields
                    ORC_LOG_DEBUG("Repeating field {} x{} times", i, pattern_.repeat_fields);
                    for (int r = 0; r < pattern_.repeat_fields; ++r) {
                        mapping.push_back(i);
                    }
                    i++;
                } else if (pattern_.gap_size > 0) {
                    // Just create a gap
                    ORC_LOG_DEBUG("Creating gap of {} fields at position {}", pattern_.gap_size, i);
                    for (int g = 0; g < pattern_.gap_size; ++g) {
                        mapping.push_back(-1);
                    }
                    i++;
                }
            } else {
                // Normal field
                mapping.push_back(i);
                i++;
            }
        }
        
        return mapping;
    }
    
    void update_metadata(TBCMetadata* metadata, const std::vector<int>& field_mapping) {
        // Update field count
        metadata->number_of_fields = field_mapping.size();
        
        // Update field metadata entries
        // TODO: Adjust VBI frame numbers based on mapping
    }
    
    bool write_metadata(const std::string& path, const TBCMetadata* metadata) {
        // For now, just copy the input metadata file and update field count
        // A full implementation would parse and rewrite the JSON
        std::string input_metadata = input_path_ + ".json";
        
        ORC_LOG_INFO("Note: Metadata file needs manual update for field count");
        ORC_LOG_INFO("  Input metadata:  {}", input_metadata);
        ORC_LOG_INFO("  Output metadata: {}", path);
        ORC_LOG_INFO("  New field count: {}", metadata->number_of_fields);
        
        // For now, just copy the file
        std::ifstream src(input_metadata, std::ios::binary);
        std::ofstream dst(path, std::ios::binary);
        
        if (!src || !dst) {
            return false;
        }
        
        dst << src.rdbuf();
        return true;
    }
    
    void print_statistics(const std::vector<int>& field_mapping) {
        int repeats = 0;
        int skips = 0;
        int gaps = 0;
        int normal = 0;
        
        int last_source = -1;
        for (size_t i = 0; i < field_mapping.size(); ++i) {
            int source = field_mapping[i];
            
            if (source < 0) {
                gaps++;
            } else if (last_source >= 0 && source == last_source) {
                repeats++;
            } else if (last_source >= 0 && source > last_source + 1) {
                skips += (source - last_source - 1);
            } else {
                normal++;
            }
            
            if (source >= 0) {
                last_source = source;
            }
        }
        
        ORC_LOG_INFO("=== Statistics ===");
        ORC_LOG_INFO("  Normal fields:   {}", normal);
        ORC_LOG_INFO("  Repeated fields: {}", repeats);
        ORC_LOG_INFO("  Skipped fields:  {}", skips);
        ORC_LOG_INFO("  Gap fields:      {}", gaps);
        ORC_LOG_INFO("  Total output:    {}", field_mapping.size());
    }

private:
    std::string input_path_;
    std::string output_path_;
    CorruptionPattern pattern_;
    std::mt19937 rng_;
};

} // namespace orc

// Predefined corruption patterns
static std::vector<orc::CorruptionPattern> get_patterns() {
    return {
        {
            "simple-skip",
            "Skip 5 fields every 100 fields",
            .skip_fields = 5,
            .repeat_fields = 0,
            .gap_size = 0,
            .corruption_rate = 0.01
        },
        {
            "simple-repeat",
            "Repeat 3 fields every 50 fields",
            .skip_fields = 0,
            .repeat_fields = 3,
            .gap_size = 0,
            .corruption_rate = 0.02
        },
        {
            "skip-with-gap",
            "Skip 10 fields and insert 5 black fields every 200 fields",
            .skip_fields = 10,
            .repeat_fields = 0,
            .gap_size = 5,
            .corruption_rate = 0.005
        },
        {
            "mixed-corruption",
            "Random skips (5%), repeats (3%), and gaps (2%)",
            .skip_fields = 0,
            .repeat_fields = 0,
            .gap_size = 0,
            .corruption_rate = 0.1
        },
        {
            "severe-damage",
            "Heavy corruption simulating badly damaged disc",
            .skip_fields = 20,
            .repeat_fields = 0,
            .gap_size = 10,
            .corruption_rate = 0.05
        }
    };
}

void print_usage(const char* program_name) {
    std::cout << "TBC Corruption Generator v" << ORC_VERSION << "\n\n";
    std::cout << "Usage: " << program_name << " <input.tbc> <output.tbc> <pattern>\n\n";
    std::cout << "Available patterns:\n";
    
    auto patterns = get_patterns();
    for (const auto& p : patterns) {
        std::cout << "  " << p.name << "\n";
        std::cout << "    " << p.description << "\n";
    }
    
    std::cout << "\nExample:\n";
    std::cout << "  " << program_name << " test.tbc broken_test.tbc simple-skip\n";
}

int main(int argc, char* argv[]) {
    orc::initialize_logging("tbc-corruption-generator");
    
    if (argc != 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    std::string input_tbc = argv[1];
    std::string output_tbc = argv[2];
    std::string pattern_name = argv[3];
    
    // Find pattern
    auto patterns = get_patterns();
    auto it = std::find_if(patterns.begin(), patterns.end(),
                          [&](const orc::CorruptionPattern& p) {
                              return p.name == pattern_name;
                          });
    
    if (it == patterns.end()) {
        std::cerr << "Error: Unknown pattern '" << pattern_name << "'\n";
        print_usage(argv[0]);
        return 1;
    }
    
    // Generate corrupted TBC
    orc::TBCCorruptionGenerator generator(input_tbc, output_tbc, *it);
    
    if (!generator.generate()) {
        std::cerr << "Error: Failed to generate corrupted TBC\n";
        return 1;
    }
    
    return 0;
}
