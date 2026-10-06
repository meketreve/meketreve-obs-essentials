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
#include "smoothing.hpp"

#include <algorithm>
#include <cmath>

namespace FaceMask {

/* ---------------- OneEuro ---------------- */

void OneEuro::configure(double mincutoff, double beta, double dcutoff)
{
	mincutoff_ = mincutoff;
	beta_ = beta;
	dcutoff_ = dcutoff;
}

double OneEuro::alpha(double dt, double cutoff)
{
	double tau = 1.0 / (2.0 * kPi * cutoff);
	return 1.0 / (1.0 + tau / dt);
}

double OneEuro::filter(double x, double dt)
{
	if (!init_) {
		init_ = true;
		x_prev_ = x;
		dx_prev_ = 0.0;
		return x;
	}
	if (dt <= 0.0)
		dt = 1e-3;

	double dx = (x - x_prev_) / dt;
	double a_d = alpha(dt, dcutoff_);
	double dx_hat = a_d * dx + (1.0 - a_d) * dx_prev_;

	double cutoff = mincutoff_ + beta_ * std::abs(dx_hat);
	double a = alpha(dt, cutoff);
	double x_hat = a * x + (1.0 - a) * x_prev_;

	x_prev_ = x_hat;
	dx_prev_ = dx_hat;
	return x_hat;
}

/* ---------------- PoseSmoother ---------------- */

void PoseSmoother::set_smoothing(double s)
{
	s = std::clamp(s, 0.0, 1.0);
	// Map smoothing -> mincutoff: high cutoff = responsive, low = smooth.
	double trans_mincut = 8.0 * (1.0 - s) + 0.5 * s;
	double rot_mincut = 6.0 * (1.0 - s) + 0.4 * s;
	tx_.configure(trans_mincut, 0.02);
	ty_.configure(trans_mincut, 0.02);
	tz_.configure(trans_mincut, 0.02);
	rot_mincut_ = rot_mincut;
	rot_beta_ = 0.2;
}

void PoseSmoother::set_avg_window(int frames)
{
	avg_n_ = std::max(1, frames);
	while ((int)t_hist_.size() > avg_n_)
		t_hist_.pop_front();
	while ((int)q_hist_.size() > avg_n_)
		q_hist_.pop_front();
}

void PoseSmoother::reset()
{
	tx_.reset();
	ty_.reset();
	tz_.reset();
	q_init_ = false;
	init_ = false;
	t_hist_.clear();
	q_hist_.clear();
}

Matx33d PoseSmoother::billboard_rotation(const Vec3d &center) const
{
	// Orientation (object->camera) that makes the face plane square to the
	// camera: object +Z maps along the view ray (away from camera), object
	// +Y to camera-up (-Y in OpenCV image coords).
	Vec3d f = center;
	double n = f.norm();
	f = (n > 1e-9) ? f / n : Vec3d(0, 0, 1);

	Vec3d up(0, -1, 0);
	Vec3d r = up.cross(f);
	double rn = r.norm();
	r = (rn > 1e-9) ? r / rn : Vec3d(1, 0, 0);
	Vec3d u = f.cross(r);

	// columns = [r, u, f]
	return Matx33d(r[0], u[0], f[0], r[1], u[1], f[1], r[2], u[2], f[2]);
}

HeadPose PoseSmoother::smooth(const HeadPose &in, uint64_t now_ns, double rotation_follow)
{
	if (!in.valid)
		return in;

	double dt = init_ ? (double)(now_ns - last_ns_) * 1e-9 : (1.0 / 60.0);
	if (dt <= 0.0)
		dt = 1e-3;
	last_ns_ = now_ns;
	init_ = true;

	HeadPose out = in;

	// Translation.
	out.t[0] = tx_.filter(in.t[0], dt);
	out.t[1] = ty_.filter(in.t[1], dt);
	out.t[2] = tz_.filter(in.t[2], dt);

	// Rotation target (optionally blended toward billboard).
	Quatd q = Quatd::createFromRotMat(in.R);
	Quatd q_target = q;
	if (rotation_follow < 0.999) {
		Matx33d Rbb = billboard_rotation(out.t);
		Quatd qbb = Quatd::createFromRotMat(Rbb);
		if (q.dot(qbb) < 0)
			qbb = -qbb;
		q_target = Quatd::slerp(qbb, q, std::clamp(rotation_follow, 0.0, 1.0));
	}

	if (!q_init_) {
		q_s_ = q_target.normalize();
		q_init_ = true;
	} else {
		if (q_s_.dot(q_target) < 0)
			q_target = -q_target;
		double d = std::clamp(q_s_.dot(q_target), -1.0, 1.0);
		double ang = 2.0 * std::acos(std::abs(d)); // radians between
		double cutoff = rot_mincut_ + rot_beta_ * (ang / dt);
		double a = OneEuro::alpha(dt, cutoff);
		q_s_ = Quatd::slerp(q_s_, q_target, a).normalize();
	}

	out.R = q_s_.toRotMat3x3();

	// Moving-average post-filter: boxcar mean of the last N smoothed poses.
	// Extra jitter rejection on top of 1€/slerp, at the cost of ~N-frame lag.
	t_hist_.push_back(out.t);
	q_hist_.push_back(q_s_);
	while ((int)t_hist_.size() > avg_n_)
		t_hist_.pop_front();
	while ((int)q_hist_.size() > avg_n_)
		q_hist_.pop_front();

	if (avg_n_ > 1 && t_hist_.size() > 1) {
		Vec3d tsum(0, 0, 0);
		for (const auto &v : t_hist_)
			tsum += v;
		out.t = tsum * (1.0 / (double)t_hist_.size());

		// Average quaternions: sign-align to a reference, sum, normalize.
		// Valid for the small angular spread typical of jitter.
		const Quatd ref = q_hist_.back();
		Quatd qsum(0, 0, 0, 0);
		for (const auto &qq : q_hist_) {
			const Quatd q2 = (qq.dot(ref) < 0) ? -qq : qq;
			qsum = qsum + q2;
		}
		out.R = qsum.normalize().toRotMat3x3();
	}

	return out;
}

} // namespace FaceMask
