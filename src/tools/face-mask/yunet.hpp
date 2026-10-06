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

#include "face-boxes.hpp"
#include "face-image.hpp"

#include <memory>
#include <string>
#include <vector>

namespace FaceMask {

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
	std::vector<FaceBox> detect(const Image &bgr, float score_threshold, float nms_threshold = 0.3f,
				    int top_k = 50);

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
