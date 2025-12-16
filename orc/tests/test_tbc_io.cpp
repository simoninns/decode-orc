/******************************************************************************
 * test_tbc_io.cpp
 *
 * Unit tests for TBC file I/O
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "tbc_reader.h"
#include "tbc_metadata.h"
#include "tbc_video_field_representation.h"
#include <cassert>
#include <iostream>
#include <fstream>

using namespace orc;

void test_tbc_reader_construction() {
    TBCReader reader;
    assert(!reader.is_open());
    assert(reader.get_field_count() == 0);
    
    std::cout << "test_tbc_reader_construction: PASSED\n";
}

void test_metadata_construction() {
    TBCMetadataReader reader;
    assert(!reader.is_open());
    
    std::cout << "test_metadata_construction: PASSED\n";
}

void test_video_system_conversion() {
    assert(video_system_to_string(VideoSystem::PAL) == "PAL");
    assert(video_system_to_string(VideoSystem::NTSC) == "NTSC");
    assert(video_system_to_string(VideoSystem::PAL_M) == "PAL-M");
    
    assert(video_system_from_string("PAL") == VideoSystem::PAL);
    assert(video_system_from_string("NTSC") == VideoSystem::NTSC);
    assert(video_system_from_string("PAL-M") == VideoSystem::PAL_M);
    assert(video_system_from_string("invalid") == VideoSystem::Unknown);
    
    std::cout << "test_video_system_conversion: PASSED\n";
}

void test_synthetic_tbc_file() {
    // Create a tiny synthetic TBC file for testing
    const std::string test_file = "/tmp/test_orc_tbc.tbc";
    const size_t field_width = 100;
    const size_t field_height = 50;
    const size_t field_length = field_width * field_height;
    const size_t num_fields = 10;
    
    // Write test data
    {
        std::ofstream out(test_file, std::ios::binary);
        for (size_t field = 0; field < num_fields; ++field) {
            std::vector<uint16_t> field_data(field_length);
            
            // Fill with a pattern that varies per field
            for (size_t i = 0; i < field_length; ++i) {
                field_data[i] = static_cast<uint16_t>((field * 1000) + (i % 256));
            }
            
            out.write(reinterpret_cast<const char*>(field_data.data()), 
                     field_length * sizeof(uint16_t));
        }
    }
    
    // Test reading
    TBCReader reader;
    assert(reader.open(test_file, field_length, field_width));
    assert(reader.is_open());
    assert(reader.get_field_count() == num_fields);
    assert(reader.get_field_length() == field_length);
    
    // Read first field
    auto field0 = reader.read_field(FieldID(0));
    assert(field0.size() == field_length);
    assert(field0[0] == 0);
    assert(field0[1] == 1);
    
    // Read another field
    auto field5 = reader.read_field(FieldID(5));
    assert(field5.size() == field_length);
    assert(field5[0] == 5000);
    assert(field5[1] == 5001);
    
    reader.close();
    assert(!reader.is_open());
    
    // Clean up
    std::remove(test_file.c_str());
    
    std::cout << "test_synthetic_tbc_file: PASSED\n";
}

int main() {
    std::cout << "Running TBC I/O tests...\n";
    
    test_tbc_reader_construction();
    test_metadata_construction();
    test_video_system_conversion();
    test_synthetic_tbc_file();
    
    std::cout << "All TBC I/O tests passed!\n";
    return 0;
}
