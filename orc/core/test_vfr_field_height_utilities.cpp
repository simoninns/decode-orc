/*
 * File:        test_vfr_field_height_utilities.cpp
 * Module:      orc-core
 * Purpose:     Unit tests for VFR field height calculation utilities
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025-2026 Simon Inns
 */

#include "include/video_field_representation.h"
#include <cassert>
#include <iostream>

/**
 * Simple test runner with basic assertions
 * 
 * This test file validates the Phase 1 implementation of VFR field height
 * calculation utilities. It uses simple assert() macros for validation.
 * 
 * Compile with: g++ -std=c++17 -I. -Icommon/include test_vfr_field_height_utilities.cpp
 * Run: ./test_vfr_field_height_utilities
 */

namespace orc::test {

// =============================================================================
// Test Suite: calculate_standard_field_height()
// =============================================================================

void test_ntsc_standard_field_heights() {
    // NTSC first field (even): 262 lines
    size_t first_field = calculate_standard_field_height(VideoSystem::NTSC, true);
    assert(first_field == 262);
    std::cout << "✓ NTSC first field height: " << first_field << " lines\n";
    
    // NTSC second field (odd): 263 lines
    size_t second_field = calculate_standard_field_height(VideoSystem::NTSC, false);
    assert(second_field == 263);
    std::cout << "✓ NTSC second field height: " << second_field << " lines\n";
    
    // Verify total frame size
    assert(first_field + second_field == 525);
    std::cout << "✓ NTSC total frame height: " << (first_field + second_field) << " lines\n";
}

void test_pal_standard_field_heights() {
    // PAL first field (odd): 312 lines
    size_t first_field = calculate_standard_field_height(VideoSystem::PAL, true);
    assert(first_field == 312);
    std::cout << "✓ PAL first field height: " << first_field << " lines\n";
    
    // PAL second field (even): 313 lines
    size_t second_field = calculate_standard_field_height(VideoSystem::PAL, false);
    assert(second_field == 313);
    std::cout << "✓ PAL second field height: " << second_field << " lines\n";
    
    // Verify total frame size
    assert(first_field + second_field == 625);
    std::cout << "✓ PAL total frame height: " << (first_field + second_field) << " lines\n";
}

void test_pal_m_standard_field_heights() {
    // PAL-M first field (even): 262 lines (same as NTSC)
    size_t first_field = calculate_standard_field_height(VideoSystem::PAL_M, true);
    assert(first_field == 262);
    std::cout << "✓ PAL-M first field height: " << first_field << " lines\n";
    
    // PAL-M second field (odd): 263 lines (same as NTSC)
    size_t second_field = calculate_standard_field_height(VideoSystem::PAL_M, false);
    assert(second_field == 263);
    std::cout << "✓ PAL-M second field height: " << second_field << " lines\n";
    
    // Verify total frame size
    assert(first_field + second_field == 525);
    std::cout << "✓ PAL-M total frame height: " << (first_field + second_field) << " lines\n";
}

void test_unknown_system() {
    // Unknown system should return 0
    size_t result = calculate_standard_field_height(VideoSystem::Unknown, true);
    assert(result == 0);
    std::cout << "✓ Unknown system returns 0 lines\n";
}

void test_field_height_asymmetry() {
    // NTSC: fields have different heights (asymmetric)
    size_t ntsc_first = calculate_standard_field_height(VideoSystem::NTSC, true);
    size_t ntsc_second = calculate_standard_field_height(VideoSystem::NTSC, false);
    assert(ntsc_first != ntsc_second);
    assert(ntsc_first < ntsc_second);
    std::cout << "✓ NTSC fields are asymmetric: " << ntsc_first << " < " << ntsc_second << "\n";
    
    // PAL: fields have different heights (asymmetric)
    size_t pal_first = calculate_standard_field_height(VideoSystem::PAL, true);
    size_t pal_second = calculate_standard_field_height(VideoSystem::PAL, false);
    assert(pal_first != pal_second);
    assert(pal_first < pal_second);
    std::cout << "✓ PAL fields are asymmetric: " << pal_first << " < " << pal_second << "\n";
}

// =============================================================================
// Test Suite: calculate_padded_field_height()
// =============================================================================

void test_ntsc_padded_field_heights() {
    // NTSC TBC files: both fields stored as 263 lines
    size_t padded_height = calculate_padded_field_height(VideoSystem::NTSC);
    assert(padded_height == 263);
    std::cout << "✓ NTSC padded field height: " << padded_height << " lines\n";
    
    // Verify padding needed for first field
    size_t ntsc_first = calculate_standard_field_height(VideoSystem::NTSC, true);
    size_t ntsc_second = calculate_standard_field_height(VideoSystem::NTSC, false);
    assert(ntsc_first < padded_height);  // First field needs padding
    assert(ntsc_second == padded_height);  // Second field needs no padding
    std::cout << "✓ NTSC: first field (" << ntsc_first << ") needs " 
              << (padded_height - ntsc_first) << " line(s) padding\n";
}

void test_pal_padded_field_heights() {
    // PAL TBC files: both fields stored as 313 lines
    size_t padded_height = calculate_padded_field_height(VideoSystem::PAL);
    assert(padded_height == 313);
    std::cout << "✓ PAL padded field height: " << padded_height << " lines\n";
    
    // Verify padding needed for first field
    size_t pal_first = calculate_standard_field_height(VideoSystem::PAL, true);
    size_t pal_second = calculate_standard_field_height(VideoSystem::PAL, false);
    assert(pal_first < padded_height);  // First field needs padding
    assert(pal_second == padded_height);  // Second field needs no padding
    std::cout << "✓ PAL: first field (" << pal_first << ") needs " 
              << (padded_height - pal_first) << " line(s) padding\n";
}

void test_pal_m_padded_field_heights() {
    // PAL-M TBC files: same as NTSC (263 lines)
    size_t padded_height = calculate_padded_field_height(VideoSystem::PAL_M);
    assert(padded_height == 263);
    std::cout << "✓ PAL-M padded field height: " << padded_height << " lines\n";
}

void test_padded_equals_second_field() {
    // Padded height should equal the second field height (no padding needed for it)
    
    // NTSC
    size_t ntsc_second = calculate_standard_field_height(VideoSystem::NTSC, false);
    size_t ntsc_padded = calculate_padded_field_height(VideoSystem::NTSC);
    assert(ntsc_second == ntsc_padded);
    std::cout << "✓ NTSC: padded height equals second field height\n";
    
    // PAL
    size_t pal_second = calculate_standard_field_height(VideoSystem::PAL, false);
    size_t pal_padded = calculate_padded_field_height(VideoSystem::PAL);
    assert(pal_second == pal_padded);
    std::cout << "✓ PAL: padded height equals second field height\n";
    
    // PAL-M
    size_t pal_m_second = calculate_standard_field_height(VideoSystem::PAL_M, false);
    size_t pal_m_padded = calculate_padded_field_height(VideoSystem::PAL_M);
    assert(pal_m_second == pal_m_padded);
    std::cout << "✓ PAL-M: padded height equals second field height\n";
}

void test_unknown_padded_system() {
    // Unknown system should return 0
    size_t result = calculate_padded_field_height(VideoSystem::Unknown);
    assert(result == 0);
    std::cout << "✓ Unknown system padded returns 0 lines\n";
}

// =============================================================================
// Integration Tests
// =============================================================================

void test_padding_calculation_ntsc() {
    // For NTSC, padding = padded_height - first_field_height
    size_t padded = calculate_padded_field_height(VideoSystem::NTSC);
    size_t first = calculate_standard_field_height(VideoSystem::NTSC, true);
    size_t padding = padded - first;
    
    assert(padding == 1);
    std::cout << "✓ NTSC padding calculation: " << padding << " line(s)\n";
}

void test_padding_calculation_pal() {
    // For PAL, padding = padded_height - first_field_height
    size_t padded = calculate_padded_field_height(VideoSystem::PAL);
    size_t first = calculate_standard_field_height(VideoSystem::PAL, true);
    size_t padding = padded - first;
    
    assert(padding == 1);
    std::cout << "✓ PAL padding calculation: " << padding << " line(s)\n";
}

void test_frame_assembly_ntsc() {
    // VFR frame assembly: first_field + second_field = total
    size_t first = calculate_standard_field_height(VideoSystem::NTSC, true);
    size_t second = calculate_standard_field_height(VideoSystem::NTSC, false);
    size_t total = first + second;
    
    assert(total == 525);
    std::cout << "✓ NTSC frame assembly: " << first << " + " << second << " = " << total << " lines\n";
}

void test_frame_assembly_pal() {
    // VFR frame assembly: first_field + second_field = total
    size_t first = calculate_standard_field_height(VideoSystem::PAL, true);
    size_t second = calculate_standard_field_height(VideoSystem::PAL, false);
    size_t total = first + second;
    
    assert(total == 625);
    std::cout << "✓ PAL frame assembly: " << first << " + " << second << " = " << total << " lines\n";
}

// =============================================================================
// Main Test Runner
// =============================================================================

int run_all_tests() {
    int test_count = 0;
    
    std::cout << "\n=============================================================================\n";
    std::cout << "VFR Field Height Calculation Utilities - Unit Tests\n";
    std::cout << "=============================================================================\n\n";
    
    try {
        // Test Suite 1: calculate_standard_field_height()
        std::cout << "Test Suite 1: calculate_standard_field_height()\n";
        std::cout << "----------\n";
        test_ntsc_standard_field_heights();
        test_count++;
        
        test_pal_standard_field_heights();
        test_count++;
        
        test_pal_m_standard_field_heights();
        test_count++;
        
        test_unknown_system();
        test_count++;
        
        test_field_height_asymmetry();
        test_count++;
        
        // Test Suite 2: calculate_padded_field_height()
        std::cout << "\nTest Suite 2: calculate_padded_field_height()\n";
        std::cout << "----------\n";
        test_ntsc_padded_field_heights();
        test_count++;
        
        test_pal_padded_field_heights();
        test_count++;
        
        test_pal_m_padded_field_heights();
        test_count++;
        
        test_padded_equals_second_field();
        test_count++;
        
        test_unknown_padded_system();
        test_count++;
        
        // Integration Tests
        std::cout << "\nTest Suite 3: Integration Tests\n";
        std::cout << "----------\n";
        test_padding_calculation_ntsc();
        test_count++;
        
        test_padding_calculation_pal();
        test_count++;
        
        test_frame_assembly_ntsc();
        test_count++;
        
        test_frame_assembly_pal();
        test_count++;
        
        std::cout << "\n=============================================================================\n";
        std::cout << "All " << test_count << " test groups passed ✓\n";
        std::cout << "=============================================================================\n\n";
        
        return 0;  // Success
        
    } catch (const std::exception& e) {
        std::cerr << "\nTest failed with exception: " << e.what() << "\n";
        return 1;  // Failure
    } catch (...) {
        std::cerr << "\nTest failed with unknown exception\n";
        return 1;  // Failure
    }
}

} // namespace orc::test

// Entry point for standalone test execution
int main() {
    return orc::test::run_all_tests();
}
