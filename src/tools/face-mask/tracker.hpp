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

#include <opencv2/core.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace FaceMask {

/* One detected face. Keypoint/bbox coordinates are in FULL-RESOLUTION source
 * pixels (the tracker maps them back from the downscaled detection frame). */
struct FaceResult {
	bool valid = false;
	float score = 0.f;
	cv::Point2f right_eye; // subject's right eye (image left)
	cv::Point2f left_eye;  // subject's left eye  (image right)
	cv::Point2f nose;
	cv::Point2f mouth_right;
	cv::Point2f mouth_left;
	cv::Rect2f bbox;
	cv::Matx33d head_R = cv::Matx33d::eye(); // raw rotation from head-pose net
	bool has_R = false;
	// Dense FaceMesh landmarks (full-res pixels) when the landmark net ran.
	std::vector<cv::Point2f> mesh;
	bool has_mesh = false;
	uint64_t frame_id = 0;
};

/* Runs YuNet (onnxruntime) on a background thread so detection never
 * blocks the OBS graphics thread. The graphics thread submits downscaled BGR
 * frames; the worker detects and publishes the latest FaceResult. */
class FaceTracker {
public:
	FaceTracker() = default;
	~FaceTracker();

	/* yunet_path: YuNet .onnx (detection). headpose_path: head-pose .onnx
	 * (rotation). landmark_path: FaceMesh .onnx (dense landmarks, optional —
	 * pass "" to disable). All resolved via obs_module_file. Spawns the
	 * worker. */
	void start(const std::string &yunet_path, const std::string &headpose_path, const std::string &landmark_path,
		   float score_thresh = 0.6f);
	void stop();
	bool running() const { return running_.load(); }

	// Enable/disable dense FaceMesh landmarks at runtime (no effect if the
	// model never loaded). When on, eye centres come from the mesh.
	void set_landmarks_enabled(bool on) { mesh_enabled_.store(on); }

	/* Called from the graphics thread. Non-blocking: keeps only the most
	 * recent frame. scale_to_full multiplies detected coords back to the
	 * full-resolution source. */
	void submit(const cv::Mat &bgr, float scale_to_full, uint64_t frame_id);

	/* Thread-safe snapshot of the latest detection. */
	FaceResult latest();

	void set_score_threshold(float t) { score_thresh_.store(t); }

private:
	void worker();

	std::string yunet_path_;
	std::string headpose_path_;
	std::string landmark_path_;
	std::atomic<float> score_thresh_{0.6f};
	std::atomic<bool> mesh_enabled_{true};

	std::thread thread_;
	std::atomic<bool> running_{false};

	// input handoff (graphics -> worker)
	std::mutex in_mtx_;
	std::condition_variable in_cv_;
	cv::Mat pending_;
	float pending_scale_ = 1.f;
	uint64_t pending_id_ = 0;
	bool has_pending_ = false;

	// output (worker -> graphics)
	std::mutex out_mtx_;
	FaceResult result_;
};

} // namespace FaceMask
