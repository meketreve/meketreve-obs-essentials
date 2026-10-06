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

/* MediaPipe FaceMesh (468 3D landmarks) via onnxruntime (keijiro's Barracuda
 * ONNX conversion of Google's face_landmark model, Apache-2.0). A face crop ->
 * 468 (x,y,z) points in 192x192 input space + a presence logit. Denser and
 * steadier than YuNet's 5 keypoints; single-threaded use (tracker worker). */
class LandmarkNet {
public:
	static constexpr int kNumPoints = 468;

	// MediaPipe canonical indices used to derive stable eye centres.
	// Eye corners: outer/inner of each eye (subject's own left/right).
	static constexpr int kRightEyeOuter = 33;
	static constexpr int kRightEyeInner = 133;
	static constexpr int kLeftEyeInner = 362;
	static constexpr int kLeftEyeOuter = 263;

	LandmarkNet();
	bool load(const std::string &model_path);
	bool loaded() const { return session_ != nullptr; }

	/* bgr_crop: a (roughly square) face crop. Fills pts (kNumPoints) in CROP
	 * pixel coordinates and the presence logit. Returns false on failure. */
	bool infer(const cv::Mat &bgr_crop, std::vector<cv::Point2f> &pts, float &presence);

private:
	Ort::Env env_;
	Ort::SessionOptions opts_;
	Ort::MemoryInfo mem_;
	std::unique_ptr<Ort::Session> session_;
	std::string in_name_;
	std::string out_pts_name_;
	std::string out_score_name_;
	std::vector<float> blob_; // reused input buffer (1x3x192x192)
};

} // namespace FaceMask
