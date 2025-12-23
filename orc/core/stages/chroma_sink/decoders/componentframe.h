/************************************************************************

    componentframe.h

    ld-chroma-decoder - Colourisation filter for ld-decode
    Copyright (C) 2021 Adam Sampson

    This file is part of ld-decode-tools.

    ld-chroma-decoder is free software: you can redistribute it and/or
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

#ifndef COMPONENTFRAME_H
#define COMPONENTFRAME_H

#include <vector>
#include <cstdint>
#include <cassert>

#include "lddecodemetadata.h"

// Two complete, interlaced fields' worth of decoded luma and chroma information.
//
// The luma and chroma samples have the same scaling as in the original
// composite signal (i.e. they're not in Y'CbCr form yet). You can recover the
// chroma signal by subtracting Y from the composite signal.
class ComponentFrame
{
public:
    ComponentFrame();

    // Set the frame's size and clear it to black
    // If mono is true, only Y set to black, while U and V are cleared.
    void init(const LdDecodeMetaData::VideoParameters &videoParameters, bool mono=false);

    // Get a pointer to a line of samples. Line numbers are 0-based within the frame.
    // Lines are stored in a contiguous array, so it's safe to get a pointer to
    // line 0 and use it to refer to later lines.
    double *y(int32_t line) {
        return yData.data() + getLineOffset(line);
    }
    double *u(int32_t line) {
        return uData.data() + getLineOffsetUV(line);
    }
    double *v(int32_t line) {
        return vData.data() + getLineOffsetUV(line);
    }
    const double *y(int32_t line) const {
        return yData.data() + getLineOffset(line);
    }
    const double *u(int32_t line) const {
        return uData.data() + getLineOffsetUV(line);
    }
    const double *v(int32_t line) const {
        return vData.data() + getLineOffsetUV(line);
    }
	
	std::vector<double>* getY(){
		return &yData;
	}
	
	std::vector<double>* getU(){
		return &uData;
	}
	
	std::vector<double>* getV(){
		return &vData;
	}
	
	void setY(std::vector<double>& _yData){
		yData = _yData;
	}
	
	void setU(std::vector<double>& _uData){
		uData = _uData;
	}
	
	void setV(std::vector<double>& _vData){
		vData = _vData;
	}

    int32_t getWidth() const {
        return width;
    }
    int32_t getHeight() const {
        return height;
    }

private:
    int32_t getLineOffset(int32_t line) const {
        assert(line >= 0);
        assert(line < static_cast<int32_t>(yData.size()));
        return line * width;
    }

    int32_t getLineOffsetUV(int32_t line) const {
        assert(line >= 0);
        assert(line < static_cast<int32_t>(uData.size()));
        return line * width;
    }

    // Size of the frame
    int32_t width;
    int32_t height;

    // Samples for Y, U and V
    std::vector<double> yData;
    std::vector<double> uData;
    std::vector<double> vData;
};

#endif // COMPONENTFRAME_H
