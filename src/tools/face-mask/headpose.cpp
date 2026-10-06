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
#include "headpose.hpp"

#include <util/base.h>
#include <plugin-support.h>

#include <array>
#include <filesystem>

namespace FaceMask {

namespace {
constexpr int kSize = 224;
const float kMean[3] = {0.485f, 0.456f, 0.406f};
const float kStd[3] = {0.229f, 0.224f, 0.225f};
} // namespace

HeadPoseNet::HeadPoseNet()
	: env_(ORT_LOGGING_LEVEL_WARNING, "meketreve-face-mask"),
	  mem_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
{
	opts_.SetIntraOpNumThreads(1);
	opts_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
}

bool HeadPoseNet::load(const std::string &model_path)
{
	try {
		// ORT's Session takes const ORTCHAR_T* — wchar_t* on Windows,
		// char* on POSIX. filesystem::path::c_str() yields exactly that
		// native type, so this stays portable without #ifdef.
		const std::filesystem::path mp = std::filesystem::u8path(model_path);
		session_ = std::make_unique<Ort::Session>(env_, mp.c_str(), opts_);

		Ort::AllocatorWithDefaultOptions alloc;
		in_name_ = session_->GetInputNameAllocated(0, alloc).get();
		out_name_ = session_->GetOutputNameAllocated(0, alloc).get();
		blob_.resize((size_t)3 * kSize * kSize);
		obs_log(LOG_INFO, "[face-mask] head-pose net loaded (in=%s out=%s)", in_name_.c_str(),
			out_name_.c_str());
		return true;
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "[face-mask] head-pose load failed: %s", e.what());
		session_.reset();
		return false;
	}
}

bool HeadPoseNet::infer(const Image &frame, const RectI &face, Matx33d &R)
{
	if (!session_ || frame.empty() || face.width <= 0 || face.height <= 0)
		return false;
	try {
		/* RGB, 0..1, then ImageNet's mean and deviation per channel. */
		std::array<float, 3> mul, add;
		for (int c = 0; c < 3; ++c) {
			mul[c] = 1.f / (255.f * kStd[c]);
			add[c] = -kMean[c] / kStd[c];
		}
		sampleToPlanes(frame, face, kSize, kSize, blob_.data(), kSize, static_cast<size_t>(kSize) * kSize, true,
			       mul, add);

		std::array<int64_t, 4> shape{1, 3, kSize, kSize};
		Ort::Value input =
			Ort::Value::CreateTensor<float>(mem_, blob_.data(), blob_.size(), shape.data(), shape.size());

		const char *in_names[] = {in_name_.c_str()};
		const char *out_names[] = {out_name_.c_str()};
		auto outputs = session_->Run(Ort::RunOptions{nullptr}, in_names, &input, 1, out_names, 1);

		const float *o = outputs[0].GetTensorData<float>();
		size_t n = outputs[0].GetTensorTypeAndShapeInfo().GetElementCount();
		if (n < 9)
			return false;
		R = Matx33d(o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8]);
		return true;
	} catch (const std::exception &e) {
		obs_log(LOG_WARNING, "[face-mask] head-pose infer failed: %s", e.what());
		return false;
	}
}

} // namespace FaceMask
