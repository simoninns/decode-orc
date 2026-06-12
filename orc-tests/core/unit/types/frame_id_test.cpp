/*
 * File:        frame_id_test.cpp
 * Module:      orc-core-tests
 * Purpose:     Unit tests for FrameID and FrameIDRange types
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Simon Inns
 */

#include <frame_id.h>

#include <gtest/gtest.h>

#include <unordered_map>
#include <unordered_set>

using namespace orc;

// ---------------------------------------------------------------------------
// FrameID arithmetic
// ---------------------------------------------------------------------------

TEST(FrameID, DefaultConstructsToZero) {
  FrameID id = 0;
  EXPECT_EQ(id, 0u);
}

TEST(FrameID, AssignAndCompare) {
  FrameID a = 10;
  FrameID b = 20;
  EXPECT_LT(a, b);
  EXPECT_LE(a, b);
  EXPECT_GT(b, a);
  EXPECT_GE(b, a);
  EXPECT_NE(a, b);
  EXPECT_EQ(a, 10u);
}

TEST(FrameID, Addition) {
  FrameID a = 5;
  EXPECT_EQ(a + 3, 8u);
}

TEST(FrameID, Subtraction) {
  FrameID a = 10;
  EXPECT_EQ(a - 3u, 7u);
}

TEST(FrameID, DifferenceBetweenTwoFrameIDs) {
  FrameID a = 100;
  FrameID b = 42;
  EXPECT_EQ(a - b, 58u);
}

TEST(FrameID, MaxValue) {
  FrameID max = std::numeric_limits<uint64_t>::max();
  EXPECT_EQ(max, std::numeric_limits<uint64_t>::max());
}

// ---------------------------------------------------------------------------
// FrameIDRange
// ---------------------------------------------------------------------------

TEST(FrameIDRange, DefaultConstructsEmpty) {
  FrameIDRange r;
  // Default: first == last == 0, count == 1 (single frame)
  // But empty() is false; this is by design — a single-frame range is valid.
  EXPECT_EQ(r.count(), 1u);
  EXPECT_FALSE(r.empty());
}

TEST(FrameIDRange, ConstructWithBounds) {
  FrameIDRange r{10, 19};  // frames 10..19 inclusive
  EXPECT_EQ(r.count(), 10u);
  EXPECT_FALSE(r.empty());
}

TEST(FrameIDRange, EmptyWhenLastBeforeFirst) {
  FrameIDRange r{100, 50};
  EXPECT_TRUE(r.empty());
  EXPECT_EQ(r.count(), 0u);
}

TEST(FrameIDRange, ContainsInclusiveBounds) {
  FrameIDRange r{5, 10};
  EXPECT_TRUE(r.contains(5));
  EXPECT_TRUE(r.contains(7));
  EXPECT_TRUE(r.contains(10));
  EXPECT_FALSE(r.contains(4));
  EXPECT_FALSE(r.contains(11));
}

TEST(FrameIDRange, SingleFrameRange) {
  FrameIDRange r{42, 42};
  EXPECT_EQ(r.count(), 1u);
  EXPECT_TRUE(r.contains(42));
  EXPECT_FALSE(r.contains(41));
  EXPECT_FALSE(r.contains(43));
}

// ---------------------------------------------------------------------------
// std::hash<FrameID> — FrameID is uint64_t; std::hash<uint64_t> is available
// without any additional specialisation.
// ---------------------------------------------------------------------------

TEST(FrameID, UnorderedMapCompilesAndWorks) {
  std::unordered_map<FrameID, int> m;
  m[0] = 100;
  m[1] = 200;
  m[999] = 300;
  EXPECT_EQ(m[0], 100);
  EXPECT_EQ(m[1], 200);
  EXPECT_EQ(m[999], 300);
}

TEST(FrameID, HashInequalityViaStdHashUint64) {
  // FrameID is uint64_t; std::hash<uint64_t> is the identity hash on most
  // platforms, guaranteeing inequality for distinct small values.
  std::hash<FrameID> h;
  EXPECT_NE(h(0), h(1));
  EXPECT_NE(h(100), h(200));
}

TEST(FrameID, UnorderedSetCompilesAndWorks) {
  std::unordered_set<FrameID> s{1, 2, 3, 4, 5};
  EXPECT_TRUE(s.count(3));
  EXPECT_FALSE(s.count(6));
}
