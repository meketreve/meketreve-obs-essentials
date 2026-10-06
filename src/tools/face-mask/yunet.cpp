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

#include <plugin-support.h>
#include <util/base.h>

#include <opencv2/imgproc.hpp>

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

void YuNet::decode(int stride, const float *cls, const float *obj, const float *bbox, const float *kps,
		   float score_threshold, std::vector<FaceBox> &out)
{
	/* As OpenCV 4.8's FaceDetectorYN: score = sqrt(cls * obj), box centre
	 * and landmarks are offsets from the cell, size is exp() of the cell. */
	const int cols = kSize / stride;
	const int rows = kSize / stride;
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

std::vector<FaceBox> YuNet::suppress(std::vector<FaceBox> boxes, float iou_threshold, int top_k)
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

std::vector<FaceBox> YuNet::detect(const cv::Mat &bgr, float score_threshold, float nms_threshold, int top_k)
{
	if (!session_ || bgr.empty())
		return {};
	try {
		/* Fit into 640x640, top-left; the rest stays black. */
		const float scale = static_cast<float>(kSize) / static_cast<float>(std::max(bgr.cols, bgr.rows));
		const int w = std::clamp(static_cast<int>(std::lround(bgr.cols * scale)), 1, kSize);
		const int h = std::clamp(static_cast<int>(std::lround(bgr.rows * scale)), 1, kSize);
		cv::Mat fitted;
		cv::resize(bgr, fitted, cv::Size(w, h));
		std::fill(blob_.begin(), blob_.end(), 0.f);
		const size_t plane = static_cast<size_t>(kSize) * kSize;
		for (int y = 0; y < h; ++y) {
			const uint8_t *row = fitted.ptr<uint8_t>(y);
			for (int x = 0; x < w; ++x) {
				const size_t at = static_cast<size_t>(y) * kSize + x;
				for (int ch = 0; ch < 3; ++ch)
					blob_[ch * plane + at] = static_cast<float>(row[x * 3 + ch]);
			}
		}

		const int64_t shape[4] = {1, 3, kSize, kSize};
		Ort::Value input = Ort::Value::CreateTensor<float>(mem_, blob_.data(), blob_.size(), shape, 4);
		std::vector<const char *> names;
		for (const std::string &n : out_names_)
			names.push_back(n.c_str());
		const char *in_names[] = {in_name_.c_str()};
		auto outputs = session_->Run(Ort::RunOptions{nullptr}, in_names, &input, 1, names.data(), names.size());

		std::vector<FaceBox> found;
		for (int i = 0; i < 3; ++i)
			decode(kStrides[i], outputs[i].GetTensorData<float>(), outputs[3 + i].GetTensorData<float>(),
			       outputs[6 + i].GetTensorData<float>(), outputs[9 + i].GetTensorData<float>(),
			       score_threshold, found);
		std::vector<FaceBox> faces = suppress(std::move(found), nms_threshold, top_k);
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
