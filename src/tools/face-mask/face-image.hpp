/*
Meketreve OBS Essentials - Face mask
Copyright (C) 2026 meketreve

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace FaceMask {

/* An 8-bit BGR picture, rows packed with no padding. */
struct Image {
	int width = 0;
	int height = 0;
	std::vector<uint8_t> bgr;

	bool empty() const { return width <= 0 || height <= 0; }
};

struct RectI {
	int x = 0, y = 0, width = 0, height = 0;
};

/* Turns a picture into a network input: bilinear resampling of `region` of
 * src (as cv::resize with INTER_LINEAR) to outW x outH, written as three
 * float planes, rows rowStride floats apart and planes planeSize floats
 * apart. Each value is byte * mul[c] + add[c], channels in RGB order when
 * rgb, else BGR. Pure, for tests. */
void sampleToPlanes(const Image &src, const RectI &region, int outW, int outH, float *out, int rowStride,
		    size_t planeSize, bool rgb, const std::array<float, 3> &mul, const std::array<float, 3> &add);

/* An RGBA copy (rows linesize bytes apart) as BGR, upside down if flip. */
Image imageFromRgba(const uint8_t *rgba, int width, int height, uint32_t linesize, bool flip);

} // namespace FaceMask
