/*
 * File:        sha256_hash_test.cpp
 * Module:      orc-core unit tests
 * Purpose:     Unit tests for the SHA-256 digest used in plugin integrity
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "../../../orc/core/include/sha256_hash.h"

#include <gtest/gtest.h>

#include <string>

namespace orc_unit_test {

// FIPS 180-4 / NIST test vectors.

TEST(Sha256HashTest, Sha256Hex_MatchesNistVectorForEmptyInput) {
  EXPECT_EQ(orc::sha256_hex(""),
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(Sha256HashTest, Sha256Hex_MatchesNistVectorForAbc) {
  EXPECT_EQ(orc::sha256_hex("abc"),
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(Sha256HashTest, Sha256Hex_MatchesNistVectorForTwoBlockMessage) {
  EXPECT_EQ(orc::sha256_hex(
                "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(Sha256HashTest, Sha256Hex_MatchesNistVectorForMillionAs) {
  const std::string input(1000000, 'a');
  EXPECT_EQ(orc::sha256_hex(input),
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256HashTest, Sha256Hex_HandlesInputSpanningBlockBoundary) {
  // 64 bytes fills exactly one block; padding must spill into a second one.
  const std::string input(64, 'x');
  const auto digest = orc::sha256_hex(input);
  EXPECT_EQ(digest.size(), 64U);
  EXPECT_NE(digest, orc::sha256_hex(std::string(63, 'x')));
  EXPECT_NE(digest, orc::sha256_hex(std::string(65, 'x')));
}

}  // namespace orc_unit_test
