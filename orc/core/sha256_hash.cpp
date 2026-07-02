/*
 * File:        sha256_hash.cpp
 * Module:      orc-core
 * Purpose:     SHA-256 digest computation for plugin artifact integrity
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 decode-orc contributors
 */

#include "include/sha256_hash.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>

namespace orc {
namespace {

// FIPS 180-4 Section 4.2.2: first 32 bits of the fractional parts of the cube
// roots of the first 64 prime numbers.
constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

inline uint32_t rotr(uint32_t value, uint32_t bits) {
  return (value >> bits) | (value << (32 - bits));
}

// Incremental SHA-256 state (FIPS 180-4 Sections 5.3.3, 6.2).
class Sha256Context {
 public:
  void update(const unsigned char* data, size_t length) {
    total_length_ += length;

    while (length > 0) {
      const size_t space = kBlockSize - buffer_length_;
      const size_t take = length < space ? length : space;
      std::memcpy(buffer_.data() + buffer_length_, data, take);
      buffer_length_ += take;
      data += take;
      length -= take;

      if (buffer_length_ == kBlockSize) {
        process_block(buffer_.data());
        buffer_length_ = 0;
      }
    }
  }

  std::string finish_hex() {
    // FIPS 180-4 Section 5.1.1: append 0x80, pad with zeros, then the
    // 64-bit big-endian bit length.
    const uint64_t bit_length = total_length_ * 8;
    const unsigned char one_bit = 0x80;
    update(&one_bit, 1);

    const unsigned char zero = 0x00;
    while (buffer_length_ != kBlockSize - 8) {
      update(&zero, 1);
    }

    std::array<unsigned char, 8> length_bytes{};
    for (int i = 0; i < 8; ++i) {
      length_bytes[static_cast<size_t>(i)] =
          static_cast<unsigned char>(bit_length >> (56 - 8 * i));
    }
    update(length_bytes.data(), length_bytes.size());

    static const char* kHexDigits = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (uint32_t word : state_) {
      for (int shift = 28; shift >= 0; shift -= 4) {
        hex.push_back(kHexDigits[(word >> shift) & 0xF]);
      }
    }
    return hex;
  }

 private:
  static constexpr size_t kBlockSize = 64;

  void process_block(const unsigned char* block) {
    std::array<uint32_t, 64> schedule{};
    for (size_t t = 0; t < 16; ++t) {
      schedule[t] = (static_cast<uint32_t>(block[t * 4]) << 24) |
                    (static_cast<uint32_t>(block[t * 4 + 1]) << 16) |
                    (static_cast<uint32_t>(block[t * 4 + 2]) << 8) |
                    static_cast<uint32_t>(block[t * 4 + 3]);
    }
    for (size_t t = 16; t < 64; ++t) {
      const uint32_t s0 = rotr(schedule[t - 15], 7) ^
                          rotr(schedule[t - 15], 18) ^ (schedule[t - 15] >> 3);
      const uint32_t s1 = rotr(schedule[t - 2], 17) ^
                          rotr(schedule[t - 2], 19) ^ (schedule[t - 2] >> 10);
      schedule[t] = schedule[t - 16] + s0 + schedule[t - 7] + s1;
    }

    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    for (size_t t = 0; t < 64; ++t) {
      const uint32_t big_sigma1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
      const uint32_t choose = (e & f) ^ (~e & g);
      const uint32_t temp1 =
          h + big_sigma1 + choose + kRoundConstants[t] + schedule[t];
      const uint32_t big_sigma0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
      const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
      const uint32_t temp2 = big_sigma0 + majority;

      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  // FIPS 180-4 Section 5.3.3 initial hash value.
  std::array<uint32_t, 8> state_ = {0x6a09e667, 0xbb67ae85, 0x3c6ef372,
                                    0xa54ff53a, 0x510e527f, 0x9b05688c,
                                    0x1f83d9ab, 0x5be0cd19};
  std::array<unsigned char, kBlockSize> buffer_{};
  size_t buffer_length_ = 0;
  uint64_t total_length_ = 0;
};

}  // namespace

std::string sha256_hex(std::string_view data) {
  Sha256Context context;
  context.update(reinterpret_cast<const unsigned char*>(data.data()),
                 data.size());
  return context.finish_hex();
}

std::string sha256_hex_of_file(const std::string& path, std::string* error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    if (error) {
      *error = "Failed to open file for hashing: " + path;
    }
    return "";
  }

  Sha256Context context;
  constexpr size_t kFileChunkSize = size_t{64} * 1024;
  std::array<char, kFileChunkSize> buffer;
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize read_count = file.gcount();
    if (read_count > 0) {
      context.update(reinterpret_cast<const unsigned char*>(buffer.data()),
                     static_cast<size_t>(read_count));
    }
  }

  if (file.bad()) {
    if (error) {
      *error = "I/O error while hashing file: " + path;
    }
    return "";
  }

  return context.finish_hex();
}

}  // namespace orc
