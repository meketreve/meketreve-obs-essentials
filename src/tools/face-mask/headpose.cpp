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

#include <plugin-support.h>
#include <util/base.h>

#include <opencv2/imgproc.hpp>

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

bool HeadPoseNet::infer(const cv::Mat &bgr_crop, cv::Matx33d &R)
{
	if (!session_ || bgr_crop.empty())
		return false;
	try {
		cv::Mat rgb;
		cv::cvtColor(bgr_crop, rgb, cv::COLOR_BGR2RGB);
		cv::resize(rgb, rgb, cv::Size(kSize, kSize));
		rgb.convertTo(rgb, CV_32F, 1.0 / 255.0);

		// HWC float -> NCHW normalized.
		const int plane = kSize * kSize;
		for (int y = 0; y < kSize; ++y) {
			const cv::Vec3f *row = rgb.ptr<cv::Vec3f>(y);
			for (int x = 0; x < kSize; ++x) {
				const cv::Vec3f &px = row[x];
				int idx = y * kSize + x;
				for (int c = 0; c < 3; ++c)
					blob_[(size_t)c * plane + idx] = (px[c] - kMean[c]) / kStd[c];
			}
		}

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
		R = cv::Matx33d(o[0], o[1], o[2], o[3], o[4], o[5], o[6], o[7], o[8]);
		return true;
	} catch (const std::exception &e) {
		obs_log(LOG_WARNING, "[face-mask] head-pose infer failed: %s", e.what());
		return false;
	}
}

} // namespace FaceMask
