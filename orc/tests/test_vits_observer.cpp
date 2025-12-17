/******************************************************************************
 * test_vits_observer.cpp
 *
 * Unit tests for VITS quality observer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "vits_observer.h"
#include "tbc_video_field_representation.h"
#include <cassert>
#include <iostream>
#include <iomanip>
#include <filesystem>

using namespace orc;
namespace fs = std::filesystem;

void test_vits_observer_with_real_data() {
    std::cout << "=== Testing VITS Observer with Real TBC Files ===" << std::endl;
    
    // Test with PAL CAV file
    std::string pal_tbc = "../test-data/pal/reference/ggv1011/1005-1205/ggv1011_pal_cav_1005-1205.tbc";
    std::string pal_db = pal_tbc + ".db";
    
    if (!fs::exists(pal_tbc) || !fs::exists(pal_db)) {
        std::cout << "Skipping real data test - test files not found" << std::endl;
        return;
    }
    
    std::cout << "\nTesting PAL CAV file: " << pal_tbc << std::endl;
    
    auto representation = create_tbc_representation(pal_tbc, pal_db);
    assert(representation != nullptr);
    
    FieldIDRange range = representation->field_range();
    std::cout << "Field range: " << range.start.to_string() << " to " 
              << range.end.to_string() << " (" << range.size() << " fields)" << std::endl;
    
    // Create VITS observer
    VITSQualityObserver observer;
    
    // Process first 10 fields
    int fields_with_vits = 0;
    int fields_with_white = 0;
    int fields_with_black = 0;
    
    size_t test_count = std::min(static_cast<size_t>(10), static_cast<size_t>(range.size()));
    
    for (size_t i = 0; i < test_count; ++i) {
        FieldID field_id = range.start + i;
        
        auto observations = observer.process_field(*representation, field_id);
        assert(observations.size() == 1);
        
        auto vits_obs = std::dynamic_pointer_cast<VITSQualityObservation>(observations[0]);
        assert(vits_obs != nullptr);
        
        if (vits_obs->confidence != ConfidenceLevel::NONE) {
            fields_with_vits++;
            
            std::cout << "  Field " << field_id.to_string() << ": ";
            
            if (vits_obs->white_snr.has_value()) {
                std::cout << "White SNR: " << std::fixed << std::setprecision(1) 
                         << vits_obs->white_snr.value() << " dB ";
                fields_with_white++;
            }
            
            if (vits_obs->black_psnr.has_value()) {
                std::cout << "Black PSNR: " << std::fixed << std::setprecision(1) 
                         << vits_obs->black_psnr.value() << " dB";
                fields_with_black++;
            }
            
            std::cout << " (confidence: ";
            switch (vits_obs->confidence) {
                case ConfidenceLevel::HIGH: std::cout << "HIGH"; break;
                case ConfidenceLevel::MEDIUM: std::cout << "MEDIUM"; break;
                case ConfidenceLevel::LOW: std::cout << "LOW"; break;
                case ConfidenceLevel::NONE: std::cout << "NONE"; break;
            }
            std::cout << ")" << std::endl;
        }
    }
    
    std::cout << "\nSummary for " << test_count << " fields:" << std::endl;
    std::cout << "  Fields with VITS data: " << fields_with_vits << std::endl;
    std::cout << "  Fields with White SNR: " << fields_with_white << std::endl;
    std::cout << "  Fields with Black PSNR: " << fields_with_black << std::endl;
    
    // Test with NTSC file if available
    std::string ntsc_tbc = "../test-data/ntsc/reference/ggv1069/5m/ggv1069_ntsc_cav_5m.tbc";
    std::string ntsc_db = ntsc_tbc + ".db";
    
    if (fs::exists(ntsc_tbc) && fs::exists(ntsc_db)) {
        std::cout << "\n\nTesting NTSC CAV file: " << ntsc_tbc << std::endl;
        
        auto ntsc_rep = create_tbc_representation(ntsc_tbc, ntsc_db);
        assert(ntsc_rep != nullptr);
        
        FieldIDRange ntsc_range = ntsc_rep->field_range();
        std::cout << "Field range: " << ntsc_range.start.to_string() << " to " 
                  << ntsc_range.end.to_string() << " (" << ntsc_range.size() << " fields)" << std::endl;
        
        VITSQualityObserver ntsc_observer;
        
        // Process first 10 NTSC fields
        fields_with_vits = 0;
        fields_with_white = 0;
        fields_with_black = 0;
        
        test_count = std::min(static_cast<size_t>(10), static_cast<size_t>(ntsc_range.size()));
        
        for (size_t i = 0; i < test_count; ++i) {
            FieldID field_id = ntsc_range.start + i;
            
            auto observations = ntsc_observer.process_field(*ntsc_rep, field_id);
            assert(observations.size() == 1);
            
            auto vits_obs = std::dynamic_pointer_cast<VITSQualityObservation>(observations[0]);
            assert(vits_obs != nullptr);
            
            if (vits_obs->confidence != ConfidenceLevel::NONE) {
                fields_with_vits++;
                
                std::cout << "  Field " << field_id.to_string() << ": ";
                
                if (vits_obs->white_snr.has_value()) {
                    std::cout << "White SNR: " << std::fixed << std::setprecision(1) 
                             << vits_obs->white_snr.value() << " dB ";
                    fields_with_white++;
                }
                
                if (vits_obs->black_psnr.has_value()) {
                    std::cout << "Black PSNR: " << std::fixed << std::setprecision(1) 
                             << vits_obs->black_psnr.value() << " dB";
                    fields_with_black++;
                }
                
                std::cout << " (confidence: ";
                switch (vits_obs->confidence) {
                    case ConfidenceLevel::HIGH: std::cout << "HIGH"; break;
                    case ConfidenceLevel::MEDIUM: std::cout << "MEDIUM"; break;
                    case ConfidenceLevel::LOW: std::cout << "LOW"; break;
                    case ConfidenceLevel::NONE: std::cout << "NONE"; break;
                }
                std::cout << ")" << std::endl;
            }
        }
        
        std::cout << "\nSummary for " << test_count << " NTSC fields:" << std::endl;
        std::cout << "  Fields with VITS data: " << fields_with_vits << std::endl;
        std::cout << "  Fields with White SNR: " << fields_with_white << std::endl;
        std::cout << "  Fields with Black PSNR: " << fields_with_black << std::endl;
    }
}

void test_vits_observer_parameters() {
    std::cout << "\n=== Testing VITS Observer Parameters ===" << std::endl;
    
    VITSQualityObserver observer;
    
    // Test parameter setting
    std::map<std::string, std::string> params = {
        {"white_ire_min", "85.0"},
        {"white_ire_max", "115.0"}
    };
    
    observer.set_parameters(params);
    
    std::cout << "  Observer name: " << observer.observer_name() << std::endl;
    std::cout << "  Observer version: " << observer.observer_version() << std::endl;
    std::cout << "  Parameters set successfully" << std::endl;
}

void test_vits_observer_metadata() {
    std::cout << "\n=== Testing VITS Observer Observation Metadata ===" << std::endl;
    
    VITSQualityObserver observer;
    
    auto obs = std::make_shared<VITSQualityObservation>();
    obs->field_id = FieldID(100);
    obs->detection_basis = DetectionBasis::SAMPLE_DERIVED;
    obs->confidence = ConfidenceLevel::HIGH;
    obs->observer_version = "1.0.0";
    obs->white_snr = 45.2;
    obs->black_psnr = 52.8;
    
    std::cout << "  Observation type: " << obs->observation_type() << std::endl;
    std::cout << "  Field ID: " << obs->field_id.to_string() << std::endl;
    std::cout << "  Detection basis: SAMPLE_DERIVED" << std::endl;
    std::cout << "  Confidence: HIGH" << std::endl;
    std::cout << "  White SNR: " << obs->white_snr.value() << " dB" << std::endl;
    std::cout << "  Black PSNR: " << obs->black_psnr.value() << " dB" << std::endl;
}

int main() {
    std::cout << "Starting VITS Observer Tests..." << std::endl;
    
    try {
        test_vits_observer_parameters();
        test_vits_observer_metadata();
        test_vits_observer_with_real_data();
        
        std::cout << "\n✓ All VITS observer tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n✗ Test failed with exception: " << e.what() << std::endl;
        return 1;
    }
}
