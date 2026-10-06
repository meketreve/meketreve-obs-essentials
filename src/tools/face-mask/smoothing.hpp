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

#include "pose.hpp"

#include "face-math.hpp"

#include <cstdint>
#include <deque>

namespace FaceMask {

/* Scalar 1€ (One-Euro) filter: low jitter at rest, low lag on motion. */
class OneEuro {
public:
	OneEuro() = default;
	OneEuro(double mincutoff, double beta, double dcutoff = 1.0)
		: mincutoff_(mincutoff),
		  beta_(beta),
		  dcutoff_(dcutoff)
	{
	}
	void configure(double mincutoff, double beta, double dcutoff = 1.0);
	double filter(double x, double dt);
	void reset() { init_ = false; }

	static double alpha(double dt, double cutoff);

private:
	double mincutoff_ = 1.0;
	double beta_ = 0.0;
	double dcutoff_ = 1.0;
	double x_prev_ = 0.0;
	double dx_prev_ = 0.0;
	bool init_ = false;
};

/* Smooths a HeadPose over time: 1€ on translation, speed-adaptive quaternion
 * slerp on rotation. rotation_follow in [0,1] blends from a camera-facing
 * billboard (0) to the full head rotation (1). */
class PoseSmoother {
public:
	// smoothing in [0,1]: 0 = most responsive, 1 = most smooth.
	void set_smoothing(double smoothing);

	// Moving-average window (frames) applied after the 1€/slerp stage.
	// 1 = disabled; higher = smoother but adds ~N-frame latency.
	void set_avg_window(int frames);

	HeadPose smooth(const HeadPose &in, uint64_t now_ns, double rotation_follow);
	void reset();

private:
	Matx33d billboard_rotation(const Vec3d &center) const;

	OneEuro tx_{4.0, 0.02};
	OneEuro ty_{4.0, 0.02};
	OneEuro tz_{4.0, 0.02};
	double rot_mincut_ = 3.0;
	double rot_beta_ = 0.2;

	Quatd q_s_;
	bool q_init_ = false;
	uint64_t last_ns_ = 0;
	bool init_ = false;

	// Moving-average post-filter state.
	int avg_n_ = 1;
	std::deque<Vec3d> t_hist_;
	std::deque<Quatd> q_hist_;
};

} // namespace FaceMask
