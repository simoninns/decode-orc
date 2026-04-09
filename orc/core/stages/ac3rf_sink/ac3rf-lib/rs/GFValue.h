// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#ifndef MUSECPP_GFVALUE_H
#define MUSECPP_GFVALUE_H

#include <cassert>
#include <array>
#include <vector>
#include <stdexcept>

namespace GfValueHelper {
    template<int bits, int irreducible_poly> int multiply_impl(int a, int b) {
        int aa = a;
        int bb = b;
        int res = 0;
        while (bb != 0) {
            if ((bb & 1) != 0)
                res ^= aa;
            if ((aa & (1 << (bits - 1))) != 0)
                aa = (aa << 1) ^ irreducible_poly;
            else aa <<= 1;
            bb >>= 1;
        }
        return res;
    }

    template<int bits, int irreducible_poly>
    std::array<int, 1 << bits> make_inverse_table() {
        std::array<int, 1 << bits> table;
        table[0] = -1; // error
        for (int i = 1; i < (1 << bits); i++) {
            for (int j = 1; j < (1 << bits); j++)
                if (multiply_impl<bits, irreducible_poly>(i, j) == 1)
                    table[i] = j;
        }
        return table;
    }

    template<int bits, int irreducible_poly>
    std::array<int, 1 << bits> make_log_table(int alpha) {
        std::array<int, 1 << bits> table;
        table[0] = -1;
        int v = 1;
        for (int i = 1; i <= ( 1 << bits) - 2; i++) {
            v = multiply_impl<bits, irreducible_poly>(v, alpha);
            table[v] = i;
        }
        return table;
    }

    template<int bits, int irreducible_poly>
    std::array<int, 1 << bits> make_pow_table(int alpha) {
        std::array<int, 1 << bits> table;
        int v = 1;
        for (int i = 0; i < ( 1 << bits); i++) {
            table[i] = v;
            v = multiply_impl<bits, irreducible_poly>(v, alpha);
        }
        return table;
    }
}

using namespace GfValueHelper;

// An element with operations in GF[2^bits] mod the given irreducible polynomial
// and a primitive element (the primitive element is used to compute logs and powers
// for thw pow() function.
template <int bits, int irreducible_poly, int alpha> class GFValue {
public:
    GFValue() : m_value(0) {};
    explicit GFValue(int v) : m_value(v) {};

    [[nodiscard]] bool isZero() const {
        return m_value == 0;
    }

    [[nodiscard]] bool nonZero() const {
        return m_value != 0;
    }

    [[nodiscard]] int getInt() const {
        return m_value;
    }

    bool operator==(GFValue b) const {
        return m_value == b.m_value;
    }

    bool operator!=(GFValue b) const {
        return m_value != b.m_value;
    }

    GFValue operator+(GFValue b) const {
        return GFValue(m_value ^ b.m_value);
    }

    void operator+=(GFValue b) {
        m_value = m_value ^ b.m_value;
    }

    GFValue operator*(GFValue b) const {
        return GFValue(multiply_impl<bits, irreducible_poly>(m_value, b.m_value));
    }

    void operator*=(GFValue b) {
        m_value = multiply_impl<bits, irreducible_poly>(m_value, b.m_value);
    }

    GFValue inverse() const {
        assert(this->m_value != 0 && m_value < (1 << bits));
        return GFValue(c_inverse_table[m_value]);
    }

    GFValue pow(int a) const {
        return m_value == 0 ? GFValue(0) : alpha_pow(a * log(*this));
    }

    [[nodiscard]] int log() const {
        assert(this->m_value != 0 && m_value < (1 << bits));
        return c_log_table[m_value];
    }

    static int log(GFValue a) {
        return a.log();
    }

    static GFValue alpha_pow(int i) {
        return GFValue(c_alpha_pow_table[i % ((1 << bits) - 1)]);
    }

private:
    static const std::array<int, 1 << bits> c_inverse_table;
    static const std::array<int, 1 << bits> c_log_table;
    static const std::array<int, 1 << bits> c_alpha_pow_table;

    int m_value;
};

template <int bits, int irreducible_poly, int alpha>
const std::array<int, 1 << bits> GFValue<bits, irreducible_poly, alpha>::c_inverse_table = make_inverse_table<bits, irreducible_poly>();

template <int bits, int irreducible_poly, int alpha>
const std::array<int, 1 << bits> GFValue<bits, irreducible_poly, alpha>::c_log_table = make_log_table<bits, irreducible_poly>(alpha);

template <int bits, int irreducible_poly, int alpha>
const std::array<int, 1 << bits> GFValue<bits, irreducible_poly, alpha>::c_alpha_pow_table = make_pow_table<bits, irreducible_poly>(alpha);

#endif //MUSECPP_GFVALUE_H
