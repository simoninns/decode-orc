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

std::string test_data_dir = "../test-data";

void test_biphase_observer() {
    std::cout << "\n=== Testing BiphaseObserver ===" << std::endl;
    
    // Try PAL CAV file
    fs::path tbc_file = fs::path(test_data_dir) / "pal" / "reference" / "ggv1011" / "1005-1205" / "ggv1011_pal_cav_1005-1205.tbc";
    fs::path db_file = fs::path(test_data_dir) / "pal" / "reference" / "ggv1011" / "1005-1205" / "ggv1011_pal_cav_1005-1205.tbc.db";
    
    if (!fs::exists(tbc_file) || !fs::exists(db_file)) {
        std::cout << "Skipping - test files not found" << std::endl;
        return;
    }
    
    auto representation = create_tbc_representation(tbc_file.string(), db_file.string());
    assert(representation != nullptr);
    
    BiphaseObserver observer;
    auto field_range = representation->field_range();
    
    std::cout << "Testing on first 10 PAL CAV fields:\n";
    
    int fields_tested = 0;
    int fields_with_data = 0;
    
    for (size_t i = 0; i < std::min(static_cast<size_t>(10), static_cast<size_t>(field_range.size())); ++i) {
        FieldID field_id = field_range.start + i;
        auto observations = observer.process_field(*representation, field_id);
        
        assert(observations.size() == 1);
        auto biphase_obs = std::dynamic_pointer_cast<BiphaseObservation>(observations[0]);
        assert(biphase_obs != nullptr);
        
        fields_tested++;
        
        if (biphase_obs->confidence != ConfidenceLevel::NONE) {
            fields_with_data++;
            std::cout << "  Field " << field_id.value() << ": Found VBI data\n";
        }
    }
    
    std::cout << "Result: " << fields_with_data << "/" << fields_tested << " fields had VBI data\n";
    std::cout << "[PASS] BiphaseObserver\n";
}

void test_vitc_observer() {
    std::cout << "\n=== Testing VitcObserver ===" << std::endl;
    
    fs::path tbc_file = fs::path(test_data_dir) / "pal" / "reference" / "ggv1011" / "1005-1205" / "ggv1011_pal_cav_1005-1205.tbc";
    fs::path db_file = fs::path(test_data_dir) / "pal" / "reference" / "ggv1011" / "1005-1205" / "ggv1011_pal_cav_1005-1205.tbc.db";
    
    if (!fs::exists(tbc_file) || !fs::exists(db_file)) {
        std::cout << "Skipping - test files not found" << std::endl;
        return;
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
        std::cout << "  NOTE: Test data may not contain VITC timecode\n";
    }
    std::cout << "[PASS] VitcObserver\n";
}

void test_closed_caption_observer() {
    std::cout << "\n=== Testing ClosedCaptionObserver ===" << std::endl;
    
    // Try NTSC file
    fs::path tbc_file = fs::path(test_data_dir) / "ntsc" / "reference" / "ggv1069" / "716-914" / "ggv1069_ntsc_cav_716-914.tbc";
    fs::path db_file = fs::path(test_data_dir) / "ntsc" / "reference" / "ggv1069" / "716-914" / "ggv1069_ntsc_cav_716-914.tbc.db";
    
    if (!fs::exists(tbc_file) || !fs::exists(db_file)) {
        std::cout << "Skipping - NTSC test files not found" << std::endl;
        return;
    }
    
    auto representation = create_tbc_representation(tbc_file.string(), db_file.string());
    assert(representation != nullptr);
    
    ClosedCaptionObserver observer;
    auto field_range = representation->field_range();
    
    std::cout << "Testing on first 20 NTSC fields:\n";
    
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
    
    std::cout << "Result: " << fields_with_cc << "/" << fields_tested << " fields had closed captions\n";
    if (fields_with_cc == 0) {
        std::cout << "  NOTE: Test data may not contain closed captions\n";
    }
    std::cout << "[PASS] ClosedCaptionObserver\n";
}

void test_ntsc_observers() {
    std::cout << "\n=== Testing NTSC-only Observers ===" << std::endl;
    
    fs::path tbc_file = fs::path(test_data_dir) / "ntsc" / "reference" / "ggv1069" / "716-914" / "ggv1069_ntsc_cav_716-914.tbc";
    fs::path db_file = fs::path(test_data_dir) / "ntsc" / "reference" / "ggv1069" / "716-914" / "ggv1069_ntsc_cav_716-914.tbc.db";
    
    if (!fs::exists(tbc_file) || !fs::exists(db_file)) {
        std::cout << "Skipping - NTSC test files not found" << std::endl;
        return;
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
        std::cout << "VideoIdObserver: confidence=" << static_cast<int>(obs->confidence) << "\n";
    }
    
    // Test FmCodeObserver
    {
        FmCodeObserver observer;
        auto observations = observer.process_field(*representation, field_id);
        assert(observations.size() == 1);
        auto obs = std::dynamic_pointer_cast<FmCodeObservation>(observations[0]);
        assert(obs != nullptr);
        std::cout << "FmCodeObserver: confidence=" << static_cast<int>(obs->confidence) << "\n";
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
        std::cout << "Skipping - PAL test files not found" << std::endl;
        return;
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
