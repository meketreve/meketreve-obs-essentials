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

#include "tracker.hpp"

#include "face-math.hpp"

namespace FaceMask {

/* Head pose: rotation from the head-pose net, position from the eyes.
 * R,t map object (head model) space -> camera space; K is the assumed pinhole
 * intrinsic matrix. */
struct HeadPose {
	bool valid = false;
	Matx33d R;
	Vec3d t;
	Matx33d K;
};

/* Canonical 3D head model (arbitrary mm-ish units), Y up, X = image-right,
 * +Z = toward back of head (camera sits on the -Z side). */
namespace facemodel {
Vec3d eye_anchor();    // midpoint between the eyes
double interocular();  // eye-to-eye distance (model units)
Vec3d axis_right();    // +X
Vec3d axis_up();       // +Y (projects to screen-up)
Vec3d toward_camera(); // unit vector from the face toward the camera (-Z)
} // namespace facemodel

/* Build pose from the head-pose NET rotation (f.head_R) plus geometric
 * position/scale from the YuNet eye keypoints. Robust pitch. invert_* flip the
 * sign of each axis to match the render frame (set empirically). */
bool build_pose_from_net(const FaceResult &f, int w, int h, double fov_deg, bool invert_pitch, bool invert_yaw,
			 bool invert_roll, HeadPose &out);

/* Project an object-space point to screen pixels; also returns camera-space
 * depth (z_cam > 0 in front of camera) for perspective-correct texturing. */
void project_point(const HeadPose &p, const Vec3d &obj, Point2d &screen, double &z_cam);

} // namespace FaceMask
