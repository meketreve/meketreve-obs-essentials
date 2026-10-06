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
#include "face-image.hpp"

#include <algorithm>
#include <cmath>

namespace FaceMask {

namespace {

/* Where each output pixel reads from, along one axis, as cv::resize does:
 * centres aligned, clamped at the edges. */
struct Tap {
	int i0 = 0, i1 = 0;
	float w1 = 0.f;
};

std::vector<Tap> taps(int from, int count, int out)
{
	std::vector<Tap> list(static_cast<size_t>(out));
	const double scale = static_cast<double>(count) / static_cast<double>(out);
	for (int o = 0; o < out; ++o) {
		const double f = (o + 0.5) * scale - 0.5;
		int i = static_cast<int>(std::floor(f));
		float w = static_cast<float>(f - i);
		if (i < 0) {
			i = 0;
			w = 0.f;
		}
		if (i >= count - 1) {
			i = count - 1;
			w = 0.f;
		}
		list[static_cast<size_t>(o)] = {from + i, from + std::min(i + 1, count - 1), w};
	}
	return list;
}

} // namespace

void sampleToPlanes(const Image &src, const RectI &region, int outW, int outH, float *out, int rowStride,
		    size_t planeSize, bool rgb, const std::array<float, 3> &mul, const std::array<float, 3> &add)
{
	/* Only the part of the region inside the picture. */
	const int x0 = std::clamp(region.x, 0, std::max(0, src.width - 1));
	const int y0 = std::clamp(region.y, 0, std::max(0, src.height - 1));
	const int w = std::clamp(region.x + region.width, x0 + 1, src.width) - x0;
	const int h = std::clamp(region.y + region.height, y0 + 1, src.height) - y0;
	if (src.empty() || outW <= 0 || outH <= 0 || w <= 0 || h <= 0)
		return;
	const std::vector<Tap> xs = taps(x0, w, outW);
	const std::vector<Tap> ys = taps(y0, h, outH);
	const size_t stride = static_cast<size_t>(src.width) * 3;
	for (int oy = 0; oy < outH; ++oy) {
		const Tap &ty = ys[static_cast<size_t>(oy)];
		const uint8_t *r0 = src.bgr.data() + static_cast<size_t>(ty.i0) * stride;
		const uint8_t *r1 = src.bgr.data() + static_cast<size_t>(ty.i1) * stride;
		for (int ox = 0; ox < outW; ++ox) {
			const Tap &tx = xs[static_cast<size_t>(ox)];
			const size_t a = static_cast<size_t>(tx.i0) * 3;
			const size_t b = static_cast<size_t>(tx.i1) * 3;
			const size_t at =
				static_cast<size_t>(oy) * static_cast<size_t>(rowStride) + static_cast<size_t>(ox);
			for (int c = 0; c < 3; ++c) {
				const float top = r0[a + c] + (r0[b + c] - r0[a + c]) * tx.w1;
				const float bottom = r1[a + c] + (r1[b + c] - r1[a + c]) * tx.w1;
				const float v = top + (bottom - top) * ty.w1;
				/* Byte c is B, G, R: in RGB order it goes to plane 2 - c. */
				const int plane = rgb ? 2 - c : c;
				out[static_cast<size_t>(plane) * planeSize + at] = v * mul[plane] + add[plane];
			}
		}
	}
}

Image imageFromRgba(const uint8_t *rgba, int width, int height, uint32_t linesize, bool flip)
{
	Image img;
	if (!rgba || width <= 0 || height <= 0)
		return img;
	img.width = width;
	img.height = height;
	img.bgr.resize(static_cast<size_t>(width) * static_cast<size_t>(height) * 3);
	for (int y = 0; y < height; ++y) {
		const uint8_t *in = rgba + static_cast<size_t>(flip ? height - 1 - y : y) * linesize;
		uint8_t *row = img.bgr.data() + static_cast<size_t>(y) * static_cast<size_t>(width) * 3;
		for (int x = 0; x < width; ++x) {
			row[x * 3] = in[x * 4 + 2];
			row[x * 3 + 1] = in[x * 4 + 1];
			row[x * 3 + 2] = in[x * 4];
		}
	}
	return img;
}

} // namespace FaceMask
