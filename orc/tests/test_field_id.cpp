/******************************************************************************
 * test_field_id.cpp
 *
 * Unit tests for FieldID implementation
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2025 Simon Inns
 ******************************************************************************/

#include "field_id.h"
#include <cassert>
#include <iostream>

using namespace orc;

void test_field_id_construction() {
    FieldID id1;
    assert(!id1.is_valid());
    
    FieldID id2(42);
    assert(id2.is_valid());
    assert(id2.value() == 42);
    
    std::cout << "test_field_id_construction: PASSED\n";
}

void test_field_id_comparison() {
    FieldID id1(10);
    FieldID id2(20);
    FieldID id3(10);
    
    assert(id1 < id2);
    assert(id1 <= id2);
    assert(id2 > id1);
    assert(id2 >= id1);
    assert(id1 == id3);
    assert(id1 != id2);
    
    std::cout << "test_field_id_comparison: PASSED\n";
}

void test_field_id_arithmetic() {
    FieldID id1(100);
    FieldID id2 = id1 + 50;
    
    assert(id2.value() == 150);
    assert((id2 - id1) == 50);
    
    FieldID id3 = id2 - 25;
    assert(id3.value() == 125);
    
    std::cout << "test_field_id_arithmetic: PASSED\n";
}

void test_field_id_range() {
    FieldID start(100);
    FieldID end(200);
    FieldIDRange range(start, end);
    
    assert(range.is_valid());
    assert(range.contains(FieldID(150)));
    assert(!range.contains(FieldID(50)));
    assert(!range.contains(FieldID(200)));  // exclusive end
    assert(range.size() == 100);
    
    std::cout << "test_field_id_range: PASSED\n";
}

int main() {
    std::cout << "Running FieldID tests...\n";
    
    test_field_id_construction();
    test_field_id_comparison();
    test_field_id_arithmetic();
    test_field_id_range();
    
    std::cout << "All FieldID tests passed!\n";
    return 0;
}
