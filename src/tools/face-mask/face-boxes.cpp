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
#include "face-boxes.hpp"

#include <algorithm>
#include <cmath>

namespace FaceMask {

void decodeYuNet(int stride, int size, const float *cls, const float *obj, const float *bbox, const float *kps,
		 float score_threshold, std::vector<FaceBox> &out)
{
	/* As OpenCV 4.8's FaceDetectorYN: score = sqrt(cls * obj), box centre
	 * and landmarks are offsets from the cell, size is exp() of the cell. */
	const int cols = size / stride;
	const int rows = size / stride;
	const auto s = static_cast<float>(stride);
	for (int r = 0; r < rows; ++r) {
		for (int c = 0; c < cols; ++c) {
			const size_t i = static_cast<size_t>(r) * cols + c;
			const float score = std::sqrt(std::clamp(cls[i], 0.f, 1.f) * std::clamp(obj[i], 0.f, 1.f));
			if (score < score_threshold)
				continue;
			const float *b = bbox + i * 4;
			const float cx = (static_cast<float>(c) + b[0]) * s;
			const float cy = (static_cast<float>(r) + b[1]) * s;
			const float w = std::exp(b[2]) * s;
			const float h = std::exp(b[3]) * s;
			FaceBox f;
			f.x = cx - w * 0.5f;
			f.y = cy - h * 0.5f;
			f.w = w;
			f.h = h;
			f.score = score;
			const float *k = kps + i * 10;
			for (int n = 0; n < 5; ++n) {
				f.kps[n * 2] = (k[n * 2] + static_cast<float>(c)) * s;
				f.kps[n * 2 + 1] = (k[n * 2 + 1] + static_cast<float>(r)) * s;
			}
			out.push_back(f);
		}
	}
}

std::vector<FaceBox> suppressBoxes(std::vector<FaceBox> boxes, float iou_threshold, int top_k)
{
	std::stable_sort(boxes.begin(), boxes.end(),
			 [](const FaceBox &a, const FaceBox &b) { return a.score > b.score; });
	std::vector<FaceBox> kept;
	for (const FaceBox &b : boxes) {
		if (static_cast<int>(kept.size()) >= top_k)
			break;
		bool overlaps = false;
		for (const FaceBox &k : kept) {
			const float ix = std::max(0.f, std::min(b.x + b.w, k.x + k.w) - std::max(b.x, k.x));
			const float iy = std::max(0.f, std::min(b.y + b.h, k.y + k.h) - std::max(b.y, k.y));
			const float inter = ix * iy;
			const float uni = b.w * b.h + k.w * k.h - inter;
			if (uni > 0.f && inter / uni > iou_threshold) {
				overlaps = true;
				break;
			}
		}
		if (!overlaps)
			kept.push_back(b);
	}
	return kept;
}

} // namespace FaceMask
