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

#include <memory>
#include <string>
#include <vector>

namespace FaceMask {

/* Wraps the yakhyo head-pose ONNX (mobilenetv2): a face crop -> 3x3 rotation
 * matrix. Robust pitch/yaw/roll, unlike sparse landmarks. Runs via onnxruntime
 * (OpenCV's dnn importer chokes on these models). Use from a single thread. */
class HeadPoseNet {
public:
	HeadPoseNet();
	bool load(const std::string &model_path);
	bool loaded() const { return session_ != nullptr; }

	/* bgr_crop: face crop (any size). Returns the raw network rotation
	 * matrix. Returns false on failure. */
	bool infer(const cv::Mat &bgr_crop, cv::Matx33d &R);

private:
	Ort::Env env_;
	Ort::SessionOptions opts_;
	Ort::MemoryInfo mem_;
	std::unique_ptr<Ort::Session> session_;
	std::string in_name_;
	std::string out_name_;
	std::vector<float> blob_; // reused input buffer (1x3x224x224)
};

} // namespace FaceMask
