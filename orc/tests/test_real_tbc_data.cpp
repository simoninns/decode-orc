/******************************************************************************
 * test_real_tbc_data.cpp
 *
 * Integration tests with real TBC files and metadata
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "tbc_reader.h"
#include "tbc_metadata.h"
#include "tbc_video_field_representation.h"
#include <cassert>
#include <iostream>
#include <iomanip>
#include <filesystem>

using namespace orc;
namespace fs = std::filesystem;

// Test data paths
const std::string TEST_DATA_ROOT = "/home/sdi/Coding/github/decode-orc/test-data";

struct TestFile {
    std::string name;
    std::string tbc_path;
    std::string db_path;
    VideoSystem expected_system;
    std::string format_type; // CAV or CLV
};

void print_separator() {
    std::cout << std::string(70, '=') << "\n";
}

void print_header(const std::string& title) {
    print_separator();
    std::cout << "  " << title << "\n";
    print_separator();
}

void test_pal_cav_file(const TestFile& test_file) {
    print_header("Testing PAL CAV: " + test_file.name);
    
    // Check files exist
    if (!fs::exists(test_file.tbc_path)) {
        std::cout << "⚠ TBC file not found: " << test_file.tbc_path << "\n";
        return;
    }
    if (!fs::exists(test_file.db_path)) {
        std::cout << "⚠ DB file not found: " << test_file.db_path << "\n";
        return;
    }
    
    std::cout << "✓ Files exist\n";
    
    // Test metadata reading
    TBCMetadataReader metadata_reader;
    assert(metadata_reader.open(test_file.db_path));
    std::cout << "✓ Opened metadata database\n";
    
    auto video_params = metadata_reader.read_video_parameters();
    assert(video_params.has_value());
    std::cout << "✓ Read video parameters\n";
    
    const auto& params = *video_params;
    std::cout << "\nVideo Parameters:\n";
    std::cout << "  System: " << video_system_to_string(params.system) << "\n";
    std::cout << "  Field dimensions: " << params.field_width << " x " << params.field_height << "\n";
    std::cout << "  Sample rate: " << std::fixed << std::setprecision(4) 
              << (params.sample_rate / 1e6) << " MHz\n";
    std::cout << "  Number of fields: " << params.number_of_sequential_fields << "\n";
    std::cout << "  Active video: " << params.active_video_start << " - " << params.active_video_end << "\n";
    std::cout << "  Colour burst: " << params.colour_burst_start << " - " << params.colour_burst_end << "\n";
    std::cout << "  Subcarrier locked: " << (params.is_subcarrier_locked ? "Yes" : "No") << "\n";
    std::cout << "  Widescreen: " << (params.is_widescreen ? "Yes" : "No") << "\n";
    
    assert(params.system == test_file.expected_system);
    assert(params.field_width > 0);
    assert(params.field_height > 0);
    assert(params.sample_rate > 0);
    
    // Test TBC reading
    TBCReader tbc_reader;
    size_t field_length = params.field_width * params.field_height;
    assert(tbc_reader.open(test_file.tbc_path, field_length, params.field_width));
    std::cout << "✓ Opened TBC file\n";
    
    size_t field_count = tbc_reader.get_field_count();
    std::cout << "  TBC contains " << field_count << " fields\n";
    assert(field_count > 0);
    
    // Read first field
    auto field0 = tbc_reader.read_field(FieldID(0));
    assert(field0.size() == field_length);
    std::cout << "✓ Read first field (" << field0.size() << " samples)\n";
    
    // Show sample values from first line
    std::cout << "  First 10 samples: ";
    for (size_t i = 0; i < std::min(size_t(10), field0.size()); ++i) {
        std::cout << field0[i] << " ";
    }
    std::cout << "\n";
    
    // Read a middle field
    FieldID middle_field(field_count / 2);
    auto field_mid = tbc_reader.read_field(middle_field);
    assert(field_mid.size() == field_length);
    std::cout << "✓ Read middle field (" << middle_field.value() << ")\n";
    
    // Test field metadata
    auto field0_metadata = metadata_reader.read_field_metadata(FieldID(0));
    if (field0_metadata) {
        std::cout << "\nField 0 Metadata:\n";
        std::cout << "  Sequence: " << field0_metadata->seq_no << "\n";
        std::cout << "  First field: " << (field0_metadata->is_first_field ? "Yes" : "No") << "\n";
        std::cout << "  Sync confidence: " << field0_metadata->sync_confidence << "\n";
        std::cout << "  Median burst IRE: " << field0_metadata->median_burst_ire << "\n";
        
        // Check for dropouts
        auto dropouts = metadata_reader.read_dropouts(FieldID(0));
        std::cout << "  Dropouts: " << dropouts.size() << "\n";
        if (!dropouts.empty()) {
            std::cout << "    First dropout: line " << dropouts[0].line
                      << ", x=" << dropouts[0].start_sample << "-" << dropouts[0].end_sample << "\n";
        }
    }
    
    // Test complete representation
    auto representation = create_tbc_representation(test_file.tbc_path, test_file.db_path);
    assert(representation != nullptr);
    std::cout << "✓ Created TBCVideoFieldRepresentation\n";
    
    assert(representation->field_count() == field_count);
    assert(representation->has_field(FieldID(0)));
    
    auto descriptor = representation->get_descriptor(FieldID(0));
    assert(descriptor.has_value());
    std::cout << "✓ Got field descriptor\n";
    std::cout << "  Format: " << (descriptor->format == VideoFormat::PAL ? "PAL" : 
                                   descriptor->format == VideoFormat::NTSC ? "NTSC" : "Unknown") << "\n";
    std::cout << "  Parity: " << (descriptor->parity == FieldParity::Top ? "Top" : "Bottom") << "\n";
    
    std::cout << "\n✅ All tests passed for " << test_file.name << "\n\n";
}

void test_ntsc_cav_file(const TestFile& test_file) {
    print_header("Testing NTSC CAV: " + test_file.name);
    
    // Check files exist
    if (!fs::exists(test_file.tbc_path)) {
        std::cout << "⚠ TBC file not found: " << test_file.tbc_path << "\n";
        return;
    }
    if (!fs::exists(test_file.db_path)) {
        std::cout << "⚠ DB file not found: " << test_file.db_path << "\n";
        return;
    }
    
    std::cout << "✓ Files exist\n";
    
    // Test metadata reading
    TBCMetadataReader metadata_reader;
    assert(metadata_reader.open(test_file.db_path));
    std::cout << "✓ Opened metadata database\n";
    
    auto video_params = metadata_reader.read_video_parameters();
    assert(video_params.has_value());
    std::cout << "✓ Read video parameters\n";
    
    const auto& params = *video_params;
    std::cout << "\nVideo Parameters:\n";
    std::cout << "  System: " << video_system_to_string(params.system) << "\n";
    std::cout << "  Field dimensions: " << params.field_width << " x " << params.field_height << "\n";
    std::cout << "  Sample rate: " << std::fixed << std::setprecision(4) 
              << (params.sample_rate / 1e6) << " MHz\n";
    std::cout << "  Number of fields: " << params.number_of_sequential_fields << "\n";
    std::cout << "  Active video: " << params.active_video_start << " - " << params.active_video_end << "\n";
    
    assert(params.system == test_file.expected_system);
    assert(params.field_width > 0);
    assert(params.field_height > 0);
    
    // Test TBC reading
    TBCReader tbc_reader;
    size_t field_length = params.field_width * params.field_height;
    assert(tbc_reader.open(test_file.tbc_path, field_length, params.field_width));
    std::cout << "✓ Opened TBC file\n";
    
    size_t field_count = tbc_reader.get_field_count();
    std::cout << "  TBC contains " << field_count << " fields\n";
    assert(field_count > 0);
    
    // Read first few fields
    for (size_t i = 0; i < std::min(size_t(3), field_count); ++i) {
        auto field = tbc_reader.read_field(FieldID(i));
        assert(field.size() == field_length);
    }
    std::cout << "✓ Read first 3 fields\n";
    
    // Test PCM audio parameters
    auto pcm_params = metadata_reader.read_pcm_audio_parameters();
    if (pcm_params) {
        std::cout << "\nPCM Audio Parameters:\n";
        std::cout << "  Sample rate: " << std::fixed << std::setprecision(1) 
                  << (pcm_params->sample_rate / 1000.0) << " kHz\n";
        std::cout << "  Bits: " << pcm_params->bits << "\n";
        std::cout << "  Signed: " << (pcm_params->is_signed ? "Yes" : "No") << "\n";
        std::cout << "  Little endian: " << (pcm_params->is_little_endian ? "Yes" : "No") << "\n";
    }
    
    // Test complete representation
    auto representation = create_tbc_representation(test_file.tbc_path, test_file.db_path);
    assert(representation != nullptr);
    std::cout << "✓ Created TBCVideoFieldRepresentation\n";
    
    // Test line access
    const auto* line0 = representation->get_line(FieldID(0), 0);
    assert(line0 != nullptr);
    std::cout << "✓ Got line data via get_line()\n";
    std::cout << "  First line samples: ";
    for (size_t i = 0; i < std::min(size_t(10), size_t(params.field_width)); ++i) {
        std::cout << line0[i] << " ";
    }
    std::cout << "\n";
    
    std::cout << "\n✅ All tests passed for " << test_file.name << "\n\n";
}

void test_clv_file(const TestFile& test_file) {
    print_header("Testing " + std::string(test_file.expected_system == VideoSystem::PAL ? "PAL" : "NTSC") 
                 + " CLV: " + test_file.name);
    
    if (!fs::exists(test_file.tbc_path) || !fs::exists(test_file.db_path)) {
        std::cout << "⚠ Files not found, skipping\n";
        return;
    }
    
    std::cout << "✓ Files exist\n";
    
    auto representation = create_tbc_representation(test_file.tbc_path, test_file.db_path);
    assert(representation != nullptr);
    std::cout << "✓ Created representation\n";
    
    const auto& params = representation->video_parameters();
    std::cout << "  System: " << video_system_to_string(params.system) << "\n";
    std::cout << "  Fields: " << representation->field_count() << "\n";
    std::cout << "  Dimensions: " << params.field_width << " x " << params.field_height << "\n";
    
    // CLV-specific: fields should be sequential and consistent
    size_t sample_count = std::min(representation->field_count(), size_t(10));
    for (size_t i = 0; i < sample_count; ++i) {
        assert(representation->has_field(FieldID(i)));
    }
    std::cout << "✓ Verified field sequence\n";
    
    std::cout << "\n✅ All tests passed for " << test_file.name << "\n\n";
}

int main() {
    std::cout << "\n";
    print_separator();
    std::cout << "  TESTING decode-orc TBC I/O WITH REAL TEST DATA\n";
    print_separator();
    std::cout << "\n";
    
    // Define test files
    std::vector<TestFile> test_files = {
        // PAL CAV
        {
            "GGV1011 PAL CAV (frames 1005-1205)",
            TEST_DATA_ROOT + "/laserdisc/pal/ggv1011/1005-1205/ggv1011_pal_cav_1005-1205.tbc",
            TEST_DATA_ROOT + "/laserdisc/pal/ggv1011/1005-1205/ggv1011_pal_cav_1005-1205.tbc.db",
            VideoSystem::PAL,
            "CAV"
        },
        {
            "GGV1011 PAL CAV (frames 16770-16973)",
            TEST_DATA_ROOT + "/laserdisc/pal/ggv1011/16770-16973/ggv1011_pal_cav_16770-16973.tbc",
            TEST_DATA_ROOT + "/laserdisc/pal/ggv1011/16770-16973/ggv1011_pal_cav_16770-16973.tbc.db",
            VideoSystem::PAL,
            "CAV"
        },
        // PAL CLV
        {
            "AMAWAAB PAL CLV (frames 6001-6205)",
            TEST_DATA_ROOT + "/laserdisc/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc",
            TEST_DATA_ROOT + "/laserdisc/pal/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc.db",
            VideoSystem::PAL,
            "CLV"
        },
        {
            "GPBlank PAL CLV (frames 14005-14206)",
            TEST_DATA_ROOT + "/laserdisc/pal/gpblank/14005-14206/gpb_pal_clv_14005-14206.tbc",
            TEST_DATA_ROOT + "/laserdisc/pal/gpblank/14005-14206/gpb_pal_clv_14005-14206.tbc.db",
            VideoSystem::PAL,
            "CLV"
        },
        // NTSC CAV
        {
            "GGV1069 NTSC CAV (frames 716-914)",
            TEST_DATA_ROOT + "/laserdisc/ntsc/ggv1069/716-914/ggv1069_ntsc_cav_716-914.tbc",
            TEST_DATA_ROOT + "/laserdisc/ntsc/ggv1069/716-914/ggv1069_ntsc_cav_716-914.tbc.db",
            VideoSystem::NTSC,
            "CAV"
        },
        {
            "GGV1069 NTSC CAV (frames 7946-8158)",
            TEST_DATA_ROOT + "/laserdisc/ntsc/ggv1069/7946-8158/ggv1069_ntsc_cav_7946-8158.tbc",
            TEST_DATA_ROOT + "/laserdisc/ntsc/ggv1069/7946-8158/ggv1069_ntsc_cav_7946-8158.tbc.db",
            VideoSystem::NTSC,
            "CAV"
        }
    };
    
    int tests_run = 0;
    int tests_passed = 0;
    
    for (const auto& test_file : test_files) {
        try {
            tests_run++;
            
            if (test_file.format_type == "CAV") {
                if (test_file.expected_system == VideoSystem::PAL) {
                    test_pal_cav_file(test_file);
                } else {
                    test_ntsc_cav_file(test_file);
                }
            } else {
                test_clv_file(test_file);
            }
            
            tests_passed++;
        } catch (const std::exception& e) {
            std::cout << "❌ Test failed with exception: " << e.what() << "\n\n";
        }
    }
    
    print_separator();
    std::cout << "  SUMMARY: " << tests_passed << "/" << tests_run << " test files passed\n";
    print_separator();
    std::cout << "\n";
    
    return (tests_passed == tests_run) ? 0 : 1;
}
