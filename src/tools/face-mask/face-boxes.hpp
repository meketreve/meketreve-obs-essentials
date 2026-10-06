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
#include <vector>

namespace FaceMask {

/* One face from YuNet, in the pixels of the image given to detect(). */
struct FaceBox {
	float x = 0.f, y = 0.f, w = 0.f, h = 0.f;
	/* right eye, left eye, nose tip, right and left mouth corners (x, y):
	 * "right" is the subject's own, on the image's left. */
	std::array<float, 10> kps{};
	float score = 0.f;
};

/* YuNet's outputs for one stride (cls, obj, bbox and kps of each cell of a
 * size x size input) -> faces in input pixels with at least that score. As
 * OpenCV 4.8's FaceDetectorYN decodes them. */
void decodeYuNet(int stride, int size, const float *cls, const float *obj, const float *bbox, const float *kps,
		 float score_threshold, std::vector<FaceBox> &out);

/* Highest scores first; drops a box overlapping a kept one by more than
 * iou_threshold (intersection over union); at most top_k. */
std::vector<FaceBox> suppressBoxes(std::vector<FaceBox> boxes, float iou_threshold, int top_k);

} // namespace FaceMask
