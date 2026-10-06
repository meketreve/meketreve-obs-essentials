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
#include "yunet.hpp"

#include <util/base.h>
#include <plugin-support.h>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace FaceMask {

YuNet::YuNet()
	: env_(ORT_LOGGING_LEVEL_WARNING, "meketreve-face-mask-yunet"),
	  mem_(Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault))
{
	opts_.SetIntraOpNumThreads(1);
	opts_.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
}

bool YuNet::load(const std::string &model_path)
{
	try {
		const std::filesystem::path path = std::filesystem::u8path(model_path);
		session_ = std::make_unique<Ort::Session>(env_, path.c_str(), opts_);
		Ort::AllocatorWithDefaultOptions alloc;
		in_name_ = session_->GetInputNameAllocated(0, alloc).get();
		out_names_.clear();
		for (const char *kind : {"cls", "obj", "bbox", "kps"}) {
			for (int stride : kStrides)
				out_names_.push_back(std::string(kind) + "_" + std::to_string(stride));
		}
		blob_.assign(static_cast<size_t>(3) * kSize * kSize, 0.f);
		obs_log(LOG_INFO, "[face-mask] YuNet loaded");
		return true;
	} catch (const std::exception &e) {
		obs_log(LOG_ERROR, "[face-mask] YuNet load failed: %s", e.what());
		session_.reset();
		return false;
	}
}

std::vector<FaceBox> YuNet::detect(const Image &bgr, float score_threshold, float nms_threshold, int top_k)
{
	if (!session_ || bgr.empty())
		return {};
	try {
		/* Fit into 640x640, top-left; the rest stays black. */
		const float scale = static_cast<float>(kSize) / static_cast<float>(std::max(bgr.width, bgr.height));
		const int w =
			std::clamp(static_cast<int>(std::lround(static_cast<float>(bgr.width) * scale)), 1, kSize);
		const int h =
			std::clamp(static_cast<int>(std::lround(static_cast<float>(bgr.height) * scale)), 1, kSize);
		std::fill(blob_.begin(), blob_.end(), 0.f);
		sampleToPlanes(bgr, {0, 0, bgr.width, bgr.height}, w, h, blob_.data(), kSize,
			       static_cast<size_t>(kSize) * kSize, false, {1.f, 1.f, 1.f}, {0.f, 0.f, 0.f});

		const int64_t shape[4] = {1, 3, kSize, kSize};
		Ort::Value input = Ort::Value::CreateTensor<float>(mem_, blob_.data(), blob_.size(), shape, 4);
		std::vector<const char *> names;
		for (const std::string &n : out_names_)
			names.push_back(n.c_str());
		const char *in_names[] = {in_name_.c_str()};
		auto outputs = session_->Run(Ort::RunOptions{nullptr}, in_names, &input, 1, names.data(), names.size());

		std::vector<FaceBox> found;
		for (int i = 0; i < 3; ++i)
			decodeYuNet(kStrides[i], kSize, outputs[i].GetTensorData<float>(),
				    outputs[3 + i].GetTensorData<float>(), outputs[6 + i].GetTensorData<float>(),
				    outputs[9 + i].GetTensorData<float>(), score_threshold, found);
		std::vector<FaceBox> faces = suppressBoxes(std::move(found), nms_threshold, top_k);
		for (FaceBox &f : faces) {
			f.x /= scale;
			f.y /= scale;
			f.w /= scale;
			f.h /= scale;
			for (float &k : f.kps)
				k /= scale;
		}
		return faces;
	} catch (const std::exception &e) {
		obs_log(LOG_WARNING, "[face-mask] YuNet detect failed: %s", e.what());
		return {};
	}
}

} // namespace FaceMask
