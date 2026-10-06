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
#include "landmarks.hpp"

#include <util/base.h>
#include <plugin-support.h>

#include <filesystem>

namespace FaceMask {

namespace {
constexpr int kSize = 192; // FaceMesh input is 192x192
} // namespace

LandmarkNet::LandmarkNet()
	: env_(ORT_LOGGING_LEVEL_WARNING, "meketreve-face-mask-landmarks"),
	  mem_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
{
	opts_.SetIntraOpNumThreads(1);
	opts_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
}

bool LandmarkNet::load(const std::string &model_path)
{
	try {
		// filesystem::path::c_str() is the native ORTCHAR_T type
		// (wchar_t* on Windows, char* on POSIX) — portable, no #ifdef.
		const std::filesystem::path mp = std::filesystem::u8path(model_path);
		session_ = std::make_unique<Ort::Session>(env_, mp.c_str(), opts_);

		Ort::AllocatorWithDefaultOptions alloc;
		in_name_ = session_->GetInputNameAllocated(0, alloc).get();
		// Output 0 = 1404 landmark coords; output 1 = presence logit.
		out_pts_name_ = session_->GetOutputNameAllocated(0, alloc).get();
		out_score_name_ = session_->GetOutputNameAllocated(1, alloc).get();
		blob_.resize((size_t)3 * kSize * kSize);
		obs_log(LOG_INFO, "[face-mask] FaceMesh net loaded (in=%s out=%s,%s)", in_name_.c_str(),
			out_pts_name_.c_str(), out_score_name_.c_str());
		return true;
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "[face-mask] FaceMesh load failed: %s", e.what());
		session_.reset();
		return false;
	}
}

bool LandmarkNet::infer(const Image &frame, const RectI &face, std::vector<Point2f> &pts, float &presence)
{
	if (!session_ || frame.empty() || face.width <= 0 || face.height <= 0)
		return false;
	try {
		/* RGB, 0..1. */
		const float k = 1.f / 255.f;
		sampleToPlanes(frame, face, kSize, kSize, blob_.data(), kSize, static_cast<size_t>(kSize) * kSize, true,
			       {k, k, k}, {0.f, 0.f, 0.f});

		const int64_t in_shape[4] = {1, 3, kSize, kSize};
		Ort::Value in = Ort::Value::CreateTensor<float>(mem_, blob_.data(), blob_.size(), in_shape, 4);

		const char *in_names[] = {in_name_.c_str()};
		const char *out_names[] = {out_pts_name_.c_str(), out_score_name_.c_str()};
		auto out = session_->Run(Ort::RunOptions{nullptr}, in_names, &in, 1, out_names, 2);

		const float *lm = out[0].GetTensorData<float>(); // 1404
		const float *sc = out[1].GetTensorData<float>(); // 1
		presence = sc[0];

		const float sx = (float)face.width / (float)kSize;
		const float sy = (float)face.height / (float)kSize;
		pts.resize(kNumPoints);
		for (int i = 0; i < kNumPoints; ++i) {
			pts[i].x = lm[i * 3 + 0] * sx;
			pts[i].y = lm[i * 3 + 1] * sy;
		}
		return true;
	} catch (const std::exception &e) {
		obs_log(LOG_WARNING, "[face-mask] FaceMesh infer threw: %s", e.what());
		return false;
	}
}

} // namespace FaceMask
