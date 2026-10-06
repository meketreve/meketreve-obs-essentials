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

#include <onnxruntime_cxx_api.h>

#include <opencv2/core.hpp>

#include <array>
#include <memory>
#include <string>
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

/* YuNet 2023mar face detector run by onnxruntime (OpenCV's FaceDetectorYN
 * needs OpenCV 4.8+ for this model). The network takes a fixed 640x640
 * BGR image: the frame is scaled to fit and padded at the right and bottom.
 * Use from a single thread. */
class YuNet {
public:
	static constexpr int kSize = 640;
	static constexpr int kStrides[3] = {8, 16, 32};

	YuNet();
	bool load(const std::string &model_path);
	bool loaded() const { return session_ != nullptr; }

	/* Faces above the threshold, best first, after non-maximum suppression. */
	std::vector<FaceBox> detect(const cv::Mat &bgr, float score_threshold, float nms_threshold = 0.3f,
				    int top_k = 50);

	/* The network outputs of one stride -> faces in network pixels (no
	 * threshold on anything but the score). Pure, for tests. */
	static void decode(int stride, const float *cls, const float *obj, const float *bbox, const float *kps,
			   float score_threshold, std::vector<FaceBox> &out);
	/* Highest scores first; drops boxes overlapping a kept one by more than
	 * iou_threshold. Pure, for tests. */
	static std::vector<FaceBox> suppress(std::vector<FaceBox> boxes, float iou_threshold, int top_k);

private:
	Ort::Env env_;
	Ort::SessionOptions opts_;
	Ort::MemoryInfo mem_;
	std::unique_ptr<Ort::Session> session_;
	std::string in_name_;
	/* cls, obj, bbox, kps per stride, in that order. */
	std::vector<std::string> out_names_;
	std::vector<float> blob_; /* 1x3x640x640 */
};

} // namespace FaceMask
