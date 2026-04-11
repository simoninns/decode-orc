// Copyright 2025-2026 Staffan Ulfberg
// This file is licensed under the provisions of the Gnu General Public License v3 (see gpl-3.0.txt)

#ifndef MUSECPP_BYTEWITHERASUREFLAG_H
#define MUSECPP_BYTEWITHERASUREFLAG_H

#include <cinttypes>
#include "GFValue.h"

class ByteWithErasureFlag {
public:
    ByteWithErasureFlag() : m_value(0), m_erased(false) {}

    ByteWithErasureFlag(int v, bool e) : m_value(v), m_erased(e) {}

    explicit ByteWithErasureFlag(int v) : m_value(v), m_erased(false) {}

    template<int irreducible_poly, int alpha> explicit ByteWithErasureFlag(GFValue<8, irreducible_poly, alpha> v) : m_value(v.getInt()), m_erased(false) {}

    template<int irreducible_poly, int alpha> explicit ByteWithErasureFlag(GFValue<8, irreducible_poly, alpha> v, bool e) : m_value(v.getInt()), m_erased(e) {}

    template<int irreducible_poly, int alpha> GFValue<8, irreducible_poly, alpha> gfValue() const {
        return GFValue<8, irreducible_poly, alpha>(m_value);
    }
    [[nodiscard]] uint8_t byteValue() const { return m_value; }
    [[nodiscard]] bool isErased() const { return m_erased; }
    void setErased(bool e) { m_erased = e; }
private:
    uint8_t m_value;
    bool m_erased;
};

#endif //MUSECPP_BYTEWITHERASUREFLAG_H
