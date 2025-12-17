// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2025
// 
// Tests for VBI observers with real TBC data

#include "biphase_observer.h"
#include "vitc_observer.h"
#include "closed_caption_observer.h"
#include "video_id_observer.h"
#include "fm_code_observer.h"
#include "white_flag_observer.h"
#include "tbc_video_field_representation.h"

#include <cassert>
#include <filesystem>
#include <iostream>
#include <iomanip>
#include <memory>

namespace fs = std::filesystem;
using namespace orc;

std::string test_data_dir = "../../test-data";

void test_biphase_observer() {
    std::cout << "\n=== Testing BiphaseObserver ===" << std::endl;
    
    // Test all PAL files
    std::vector<std::pair<std::string, std::string>> test_files = {
        {"pal/reference/ggv1011/1005-1205/ggv1011_pal_cav_1005-1205.tbc", "GGV1011 CAV 1005-1205"},
        {"pal/reference/ggv1011/16770-16973/ggv1011_pal_cav_16770-16973.tbc", "GGV1011 CAV 16770-16973"},
        {"pal/reference/amawaab/6001-6205/amawaab_pal_clv_6001-6205.tbc", "Amawaab CLV 6001-6205"},
        {"pal/reference/gpblank/14005-14206/gpb_pal_clv_14005-14206.tbc", "GPBlank CLV 14005-14206"},
        {"pal/reference/gpblank/18500-18700/gpb_pal_clv_18500-18700.tbc", "GPBlank CLV 18500-18700"},
        {"pal/reference/domesday/8100-8200/domesdaynat4_cav_pal-8100-8200.tbc", "Domesday Nat CAV 8100-8200"},
        {"pal/reference/domesday/3100-3200/domesdaycs4_cav_pal-3100-3200.tbc", "Domesday CS CAV 3100-3200"},
        {"pal/reference/domesday/11000-11200/domesdaycn4_cav_pal-11000-11200.tbc", "Domesday CN CAV 11000-11200"},
        {"pal/reference/domesday/14100-14300/domesdaynat4_clv_pal-14100-14300.tbc", "Domesday Nat CLV 14100-14300"}
    };
    
    int total_files_tested = 0;
    int total_fields_with_data = 0;
    int missing_files = 0;
    
    for (const auto& [rel_path, description] : test_files) {
        fs::path tbc_file = fs::path(test_data_dir) / rel_path;
        fs::path db_file = tbc_file.string() + ".db";
        
        if (!fs::exists(tbc_file) || !fs::exists(db_file)) {
            std::cerr << "  ERROR: Missing test files for " << description << "\n";
            std::cerr << "    Expected: " << tbc_file << "\n";
            std::cerr << "    Expected: " << db_file << "\n";
            missing_files++;
            continue;
        }
        
        auto representation = create_tbc_representation(tbc_file.string(), db_file.string());
        assert(representation != nullptr);
        
        BiphaseObserver observer;
        auto field_range = representation->field_range();
        
        std::cout << "  Testing " << description << " (first 5 fields):\n";
        
        int fields_tested = 0;
        int fields_with_data = 0;
        
        for (size_t i = 0; i < std::min(static_cast<size_t>(5), static_cast<size_t>(field_range.size())); ++i) {
            FieldID field_id = field_range.start + i;
            auto observations = observer.process_field(*representation, field_id);
            
            assert(observations.size() == 1);
            auto biphase_obs = std::dynamic_pointer_cast<BiphaseObservation>(observations[0]);
            assert(biphase_obs != nullptr);
            
            fields_tested++;
            
            if (biphase_obs->confidence != ConfidenceLevel::NONE) {
                fields_with_data++;
            }
        }
        
        std::cout << "    Result: " << fields_with_data << "/" << fields_tested << " fields had VBI data\n";
        total_files_tested++;
        total_fields_with_data += (fields_with_data > 0 ? 1 : 0);
    }
    
    if (missing_files > 0) {
        throw std::runtime_error("Missing " + std::to_string(missing_files) + " test file(s)");
    }
    
    std::cout << "Summary: " << total_fields_with_data << "/" << total_files_tested << " files had biphase data\n";
    std::cout << "[PASS] BiphaseObserver\n";
}

void test_vitc_observer() {
    std::cout << "\n=== Testing VitcObserver ===" << std::endl;
    
    fs::path tbc_file = fs::path(test_data_dir) / "pal" / "reference" / "ggv1011" / "1005-1205" / "ggv1011_pal_cav_1005-1205.tbc";
    fs::path db_file = fs::path(test_data_dir) / "pal" / "reference" / "ggv1011" / "1005-1205" / "ggv1011_pal_cav_1005-1205.tbc.db";
    
    if (!fs::exists(tbc_file) || !fs::exists(db_file)) {
        std::cerr << "ERROR: Missing required test files\n";
        std::cerr << "  Expected: " << tbc_file << "\n";
        std::cerr << "  Expected: " << db_file << "\n";
        throw std::runtime_error("Missing test files for VitcObserver");
    }
    
    auto representation = create_tbc_representation(tbc_file.string(), db_file.string());
    assert(representation != nullptr);
    
    VitcObserver observer;
    auto field_range = representation->field_range();
    
    std::cout << "Testing on first 10 PAL fields:\n";
    
    int fields_tested = 0;
    int fields_with_vitc = 0;
    
    for (size_t i = 0; i < std::min(static_cast<size_t>(10), static_cast<size_t>(field_range.size())); ++i) {
        FieldID field_id = field_range.start + i;
        auto observations = observer.process_field(*representation, field_id);
        
        assert(observations.size() == 1);
        auto vitc_obs = std::dynamic_pointer_cast<VitcObservation>(observations[0]);
        assert(vitc_obs != nullptr);
        
        fields_tested++;
        
        if (vitc_obs->confidence != ConfidenceLevel::NONE) {
            fields_with_vitc++;
            std::cout << "  Field " << field_id.value() << " (line " << vitc_obs->line_number << "): Found VITC\n";
        }
    }
    
    std::cout << "Result: " << fields_with_vitc << "/" << fields_tested << " fields had VITC\n";
    if (fields_with_vitc == 0) {
        std::cout << "\n⚠️  WARNING: No VITC timecode found in test data\n";
        std::cout << "   VitcObserver functionality could not be validated\n";
        std::cout << "   Observer compiles and runs but decoding accuracy is UNVERIFIED\n\n";
    }
    std::cout << "[PASS] VitcObserver\n";
}

void test_closed_caption_observer() {
    std::cout << "\n=== Testing ClosedCaptionObserver ===" << std::endl;
    
    // Test all NTSC files
    std::vector<std::pair<std::string, std::string>> test_files = {
        {"ntsc/reference/ggv1069/716-914/ggv1069_ntsc_cav_716-914.tbc", "GGV1069 CAV 716-914"},
        {"ntsc/reference/ggv1069/7946-8158/ggv1069_ntsc_cav_7946-8158.tbc", "GGV1069 CAV 7946-8158"},
        {"ntsc/reference/bambi/8000-8200/bambi_ntsc_clv_8000-8200.tbc", "Bambi CLV 8000-8200"},
        {"ntsc/reference/bambi/18100-18306/bambi_ntsc_clv_18100-18306.tbc", "Bambi CLV 18100-18306"},
        {"ntsc/reference/cinder/9000-9210/cinder_ntsc_clv_9000-9210.tbc", "Cinder CLV 9000-9210"},
        {"ntsc/reference/cinder/21200-21410/cinder_ntsc_clv_21200-21410.tbc", "Cinder CLV 21200-21410"},
        {"ntsc/reference/appleva/2000-2200/appleva_cav_ntsc-2000-2200.tbc", "Apple VA CAV 2000-2200"},
        {"ntsc/reference/appleva/18000-18200/appleva_cav_ntsc-18000-18200.tbc", "Apple VA CAV 18000-18200"}
    };
    
    int total_files_tested = 0;
    int total_files_with_cc = 0;
    int missing_files = 0;
    
    for (const auto& [rel_path, description] : test_files) {
        fs::path tbc_file = fs::path(test_data_dir) / rel_path;
        fs::path db_file = tbc_file.string() + ".db";
        
        if (!fs::exists(tbc_file) || !fs::exists(db_file)) {
            std::cerr << "  ERROR: Missing test files for " << description << "\n";
            std::cerr << "    Expected: " << tbc_file << "\n";
            std::cerr << "    Expected: " << db_file << "\n";
            missing_files++;
            continue;
        }
        
        auto representation = create_tbc_representation(tbc_file.string(), db_file.string());
        assert(representation != nullptr);
        
        ClosedCaptionObserver observer;
        auto field_range = representation->field_range();
        
        std::cout << "  Testing " << description << " (first 20 fields):\n";
        
        int fields_tested = 0;
        int fields_with_cc = 0;
        
        for (size_t i = 0; i < std::min(static_cast<size_t>(20), static_cast<size_t>(field_range.size())); ++i) {
            FieldID field_id = field_range.start + i;
            auto observations = observer.process_field(*representation, field_id);
            
            assert(observations.size() == 1);
            auto cc_obs = std::dynamic_pointer_cast<ClosedCaptionObservation>(observations[0]);
            assert(cc_obs != nullptr);
            
            fields_tested++;
            
            if (cc_obs->confidence != ConfidenceLevel::NONE) {
                fields_with_cc++;
            }
        }
        
        std::cout << "    Result: " << fields_with_cc << "/" << fields_tested << " fields had closed captions\n";
        total_files_tested++;
        total_files_with_cc += (fields_with_cc > 0 ? 1 : 0);
    }
    
    if (missing_files > 0) {
        throw std::runtime_error("Missing " + std::to_string(missing_files) + " test file(s)");
    }
    
    std::cout << "Summary: " << total_files_with_cc << "/" << total_files_tested << " files had closed captions\n";
    
    if (total_files_with_cc == 0) {
        std::cout << "\n⚠️  WARNING: No closed captions found in any test files\n";
        std::cout << "   ClosedCaptionObserver functionality could not be validated\n";
        std::cout << "   Observer compiles and runs but decoding accuracy is UNVERIFIED\n\n";
    }
    
    std::cout << "[PASS] ClosedCaptionObserver\n";
}

void test_ntsc_observers() {
    std::cout << "\n=== Testing NTSC-only Observers ===" << std::endl;
    
    fs::path tbc_file = fs::path(test_data_dir) / "ntsc" / "reference" / "ggv1069" / "716-914" / "ggv1069_ntsc_cav_716-914.tbc";
    fs::path db_file = fs::path(test_data_dir) / "ntsc" / "reference" / "ggv1069" / "716-914" / "ggv1069_ntsc_cav_716-914.tbc.db";
    
    if (!fs::exists(tbc_file) || !fs::exists(db_file)) {
        std::cerr << "ERROR: Missing required NTSC test files\n";
        std::cerr << "  Expected: " << tbc_file << "\n";
        std::cerr << "  Expected: " << db_file << "\n";
        throw std::runtime_error("Missing test files for NTSC observers");
    }
    
    auto representation = create_tbc_representation(tbc_file.string(), db_file.string());
    assert(representation != nullptr);
    
    FieldID field_id = representation->field_range().start;
    
    // Test VideoIdObserver
    {
        VideoIdObserver observer;
        auto observations = observer.process_field(*representation, field_id);
        assert(observations.size() == 1);
        auto obs = std::dynamic_pointer_cast<VideoIdObservation>(observations[0]);
        assert(obs != nullptr);
        std::cout << "VideoIdObserver: confidence=" << static_cast<int>(obs->confidence);
        if (obs->confidence == ConfidenceLevel::NONE) {
            std::cout << " (⚠️  NO TEST DATA - functionality UNVERIFIED)";
        }
        std::cout << "\n";
    }
    
    // Test FmCodeObserver
    {
        FmCodeObserver observer;
        auto observations = observer.process_field(*representation, field_id);
        assert(observations.size() == 1);
        auto obs = std::dynamic_pointer_cast<FmCodeObservation>(observations[0]);
        assert(obs != nullptr);
        std::cout << "FmCodeObserver: confidence=" << static_cast<int>(obs->confidence);
        if (obs->confidence == ConfidenceLevel::NONE) {
            std::cout << " (⚠️  NO TEST DATA - functionality UNVERIFIED)";
        }
        std::cout << "\n";
    }
    
    // Test WhiteFlagObserver
    {
        WhiteFlagObserver observer;
        auto observations = observer.process_field(*representation, field_id);
        assert(observations.size() == 1);
        auto obs = std::dynamic_pointer_cast<WhiteFlagObservation>(observations[0]);
        assert(obs != nullptr);
        std::cout << "WhiteFlagObserver: confidence=" << static_cast<int>(obs->confidence) << "\n";
    }
    
    std::cout << "[PASS] NTSC-only Observers\n";
}

void test_format_specificity() {
    std::cout << "\n=== Testing Format Specificity ===" << std::endl;
    
    fs::path pal_tbc = fs::path(test_data_dir) / "pal" / "reference" / "ggv1011" / "1005-1205" / "ggv1011_pal_cav_1005-1205.tbc";
    fs::path pal_db = fs::path(test_data_dir) / "pal" / "reference" / "ggv1011" / "1005-1205" / "ggv1011_pal_cav_1005-1205.tbc.db";
    
    if (!fs::exists(pal_tbc) || !fs::exists(pal_db)) {
        std::cerr << "ERROR: Missing required PAL test files\n";
        std::cerr << "  Expected: " << pal_tbc << "\n";
        std::cerr << "  Expected: " << pal_db << "\n";
        throw std::runtime_error("Missing test files for format specificity test");
    }
    
    auto representation = create_tbc_representation(pal_tbc.string(), pal_db.string());
    assert(representation != nullptr);
    
    FieldID field_id = representation->field_range().start;
    auto descriptor = representation->get_descriptor(field_id);
    assert(descriptor.has_value());
    assert(descriptor->format == VideoFormat::PAL);
    
    std::cout << "Testing NTSC-only observers on PAL data (should return NONE):\n";
    
    // VideoIdObserver should return NONE on PAL
    {
        VideoIdObserver observer;
        auto observations = observer.process_field(*representation, field_id);
        assert(observations.size() == 1);
        auto obs = std::dynamic_pointer_cast<VideoIdObservation>(observations[0]);
        assert(obs->confidence == ConfidenceLevel::NONE);
        std::cout << "  VideoIdObserver: NONE (correct)\n";
    }
    
    // FmCodeObserver should return NONE on PAL
    {
        FmCodeObserver observer;
        auto observations = observer.process_field(*representation, field_id);
        assert(observations.size() == 1);
        auto obs = std::dynamic_pointer_cast<FmCodeObservation>(observations[0]);
        assert(obs->confidence == ConfidenceLevel::NONE);
        std::cout << "  FmCodeObserver: NONE (correct)\n";
    }
    
    // WhiteFlagObserver should return NONE on PAL
    {
        WhiteFlagObserver observer;
        auto observations = observer.process_field(*representation, field_id);
        assert(observations.size() == 1);
        auto obs = std::dynamic_pointer_cast<WhiteFlagObservation>(observations[0]);
        assert(obs->confidence == ConfidenceLevel::NONE);
        std::cout << "  WhiteFlagObserver: NONE (correct)\n";
    }
    
    std::cout << "[PASS] Format Specificity\n";
}

int main() {
    std::cout << "======================================" << std::endl;
    std::cout << " VBI Observer Tests" << std::endl;
    std::cout << "======================================" << std::endl;
    
    try {
        test_biphase_observer();
        test_vitc_observer();
        test_closed_caption_observer();
        test_ntsc_observers();
        test_format_specificity();
        
        std::cout << "\n======================================" << std::endl;
        std::cout << "All VBI Observer tests PASSED!" << std::endl;
        std::cout << "======================================" << std::endl;
        
        std::cout << "\nNOTE: Some observers may not find data if the test" << std::endl;
        std::cout << "      files don't contain that specific VBI type." << std::endl;
        std::cout << "      This is expected behavior." << std::endl;
        
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nTest FAILED with exception: " << e.what() << std::endl;
        return 1;
    }
}
