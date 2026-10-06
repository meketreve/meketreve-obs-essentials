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
#include "pose.hpp"

#include <cmath>
#include <vector>

namespace FaceMask {

namespace facemodel {
cv::Vec3d eye_anchor()
{
	return {0.0, 170.0, -135.0};
}
double interocular()
{
	return 330.0; // eye-to-eye distance of the model (eyes at x = ±165)
}
cv::Vec3d axis_right()
{
	return {1.0, 0.0, 0.0};
}
cv::Vec3d axis_up()
{
	return {0.0, 1.0, 0.0};
}
cv::Vec3d toward_camera()
{
	return {0.0, 0.0, -1.0};
}
} // namespace facemodel

namespace {
cv::Matx33d rotX(double a)
{
	double c = std::cos(a), s = std::sin(a);
	return cv::Matx33d(1, 0, 0, 0, c, -s, 0, s, c);
}
cv::Matx33d rotY(double a)
{
	double c = std::cos(a), s = std::sin(a);
	return cv::Matx33d(c, 0, s, 0, 1, 0, -s, 0, c);
}
cv::Matx33d rotZ(double a)
{
	double c = std::cos(a), s = std::sin(a);
	return cv::Matx33d(c, -s, 0, s, c, 0, 0, 0, 1);
}
} // namespace

bool build_pose_from_net(const FaceResult &f, int w, int h, double fov_deg, bool invert_pitch, bool invert_yaw,
			 bool invert_roll, HeadPose &out)
{
	if (!f.valid || !f.has_R || w <= 0 || h <= 0)
		return false;

	double fov = fov_deg * CV_PI / 180.0;
	if (fov < 0.05)
		fov = 0.05;
	double fx = (w * 0.5) / std::tan(fov * 0.5);
	double fy = fx;
	double cx = w * 0.5, cy = h * 0.5;
	cv::Matx33d K(fx, 0, cx, 0, fy, cy, 0, 0, 1);

	// Position/scale from the eye keypoints.
	cv::Point2f er = f.right_eye, el = f.left_eye;
	double io_px = cv::norm(cv::Vec2f(el.x - er.x, el.y - er.y));
	if (io_px < 1.0)
		return false;
	cv::Point2f mid((er.x + el.x) * 0.5f, (er.y + el.y) * 0.5f);

	const double obj_io = facemodel::interocular(); // 330 model units
	double z = obj_io * fx / io_px;                 // depth so size matches
	cv::Vec3d anchor_cam((mid.x - cx) * z / fx, (mid.y - cy) * z / fy, z);

	// Rotation from the net (decompose -> rebuild in render frame).
	// The net's axes are mirrored vs our render frame, so the baseline
	// negates all three; the invert toggles flip back per axis if needed.
	const cv::Matx33d &Rn = f.head_R;
	double sy = std::sqrt(Rn(0, 0) * Rn(0, 0) + Rn(1, 0) * Rn(1, 0));
	double pitch = -std::atan2(Rn(2, 1), Rn(2, 2));
	double yaw = -std::atan2(-Rn(2, 0), sy);
	double roll = -std::atan2(Rn(1, 0), Rn(0, 0));
	if (invert_pitch)
		pitch = -pitch;
	if (invert_yaw)
		yaw = -yaw;
	if (invert_roll)
		roll = -roll;

	// Rebuild in the SAME order the decomposition assumes (Rz*Ry*Rx) so that
	// with all inverts off this reconstructs the net rotation exactly; then
	// flip object (Y-up) -> camera frame.
	cv::Matx33d R_obj = rotZ(roll) * rotY(yaw) * rotX(pitch);
	cv::Matx33d base(1, 0, 0, 0, -1, 0, 0, 0, -1);
	cv::Matx33d R = base * R_obj;

	// Translation so the model eye-anchor projects onto the measured eyes.
	cv::Vec3d t = anchor_cam - R * facemodel::eye_anchor();

	out.valid = true;
	out.R = R;
	out.t = t;
	out.K = K;
	return true;
}

void project_point(const HeadPose &p, const cv::Vec3d &obj, cv::Point2d &screen, double &z_cam)
{
	cv::Vec3d cam = p.R * obj + p.t;
	z_cam = cam[2];
	double inv = (std::abs(z_cam) > 1e-9) ? (1.0 / z_cam) : 0.0;
	double x = p.K(0, 0) * cam[0] + p.K(0, 2) * cam[2];
	double y = p.K(1, 1) * cam[1] + p.K(1, 2) * cam[2];
	screen.x = x * inv;
	screen.y = y * inv;
}

} // namespace FaceMask
