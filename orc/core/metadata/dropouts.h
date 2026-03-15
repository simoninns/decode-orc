/************************************************************************

    dropouts.h

    ld-decode-tools TBC library
    Copyright (C) 2018-2020 Simon Inns

    This file is part of ld-decode-tools.

    ld-decode-tools is free software: you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation, either version 3 of the
    License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

************************************************************************/

// Note: Copied from the TBC library so the JSON handling code is local to the application

#ifndef DROPOUTS_H
#define DROPOUTS_H

#include <cstdint>
#include <vector>

class JsonReader;
class JsonWriter;

class DropOuts
{
public:
    DropOuts() = default;
    DropOuts(int reserve);
    ~DropOuts() = default;
    DropOuts(const DropOuts &) = default;

    DropOuts(const std::vector<int32_t> &startx, const std::vector<int32_t> &endx, const std::vector<int32_t> &fieldLine);
    DropOuts &operator=(const DropOuts &);

    void append(const int32_t startx, const int32_t endx, const int32_t fieldLine);
    void reserve(int size);
    void resize(int32_t size);
    void clear();
    void concatenate(const bool verbose=true);

    // Return the number of dropouts
    int32_t size() const {
        return static_cast<int32_t>(m_startx.size());
    }

    // Return true if there are no dropouts
    bool empty() const {
        return m_startx.empty();
    }

    // Get position of a dropout
    int32_t startx(int32_t index) const {
        return m_startx[static_cast<size_t>(index)];
    }
    int32_t endx(int32_t index) const {
        return m_endx[static_cast<size_t>(index)];
    }
    int32_t fieldLine(int32_t index) const {
        return m_fieldLine[static_cast<size_t>(index)];
    }

    void read(JsonReader &reader);
    void write(JsonWriter &writer) const;

private:
    std::vector<int32_t> m_startx;
    std::vector<int32_t> m_endx;
    std::vector<int32_t> m_fieldLine;

    void readArray(JsonReader &reader, std::vector<int32_t> &array);
    void writeArray(JsonWriter &writer, const std::vector<int32_t> &array) const;
};

#endif // DROPOUTS_H
