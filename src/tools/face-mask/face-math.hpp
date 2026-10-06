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

#include <cmath>

/* The little geometry the face mask needs, in place of OpenCV's types:
 * points, rectangles, 3-vectors, 3x3 matrices and unit quaternions. Same
 * names and layout as cv::Point2f / Rect2f / Vec3d / Matx33d / Quatd, so the
 * code ported from the Eye Mask Tracker reads the same. Pure, for tests. */
namespace FaceMask {

constexpr double kPi = 3.14159265358979323846;

struct Point2f {
	float x = 0.f, y = 0.f;
};

struct Point2d {
	double x = 0.0, y = 0.0;
};

struct Rect2f {
	float x = 0.f, y = 0.f, width = 0.f, height = 0.f;
};

struct Vec3d {
	double v[3] = {0.0, 0.0, 0.0};

	Vec3d() = default;
	Vec3d(double x, double y, double z) : v{x, y, z} {}
	double &operator[](int i) { return v[i]; }
	double operator[](int i) const { return v[i]; }
	Vec3d operator+(const Vec3d &o) const { return {v[0] + o[0], v[1] + o[1], v[2] + o[2]}; }
	Vec3d operator-(const Vec3d &o) const { return {v[0] - o[0], v[1] - o[1], v[2] - o[2]}; }
	Vec3d operator*(double s) const { return {v[0] * s, v[1] * s, v[2] * s}; }
	Vec3d operator/(double s) const { return {v[0] / s, v[1] / s, v[2] / s}; }
	Vec3d &operator+=(const Vec3d &o)
	{
		v[0] += o[0];
		v[1] += o[1];
		v[2] += o[2];
		return *this;
	}
	double dot(const Vec3d &o) const { return v[0] * o[0] + v[1] * o[1] + v[2] * o[2]; }
	Vec3d cross(const Vec3d &o) const
	{
		return {v[1] * o[2] - v[2] * o[1], v[2] * o[0] - v[0] * o[2], v[0] * o[1] - v[1] * o[0]};
	}
	double norm() const { return std::sqrt(dot(*this)); }
};

inline Vec3d operator*(double s, const Vec3d &a)
{
	return a * s;
}

/* Row-major 3x3. */
struct Matx33d {
	double m[9] = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0};

	Matx33d() = default;
	Matx33d(double a, double b, double c, double d, double e, double f, double g, double h, double i)
		: m{a, b, c, d, e, f, g, h, i}
	{
	}
	static Matx33d eye() { return {}; }
	double &operator()(int r, int c) { return m[r * 3 + c]; }
	double operator()(int r, int c) const { return m[r * 3 + c]; }
	Matx33d operator*(const Matx33d &o) const
	{
		Matx33d out;
		for (int r = 0; r < 3; ++r) {
			for (int c = 0; c < 3; ++c)
				out(r, c) = (*this)(r, 0) * o(0, c) + (*this)(r, 1) * o(1, c) + (*this)(r, 2) * o(2, c);
		}
		return out;
	}
	Vec3d operator*(const Vec3d &a) const
	{
		return {m[0] * a[0] + m[1] * a[1] + m[2] * a[2], m[3] * a[0] + m[4] * a[1] + m[5] * a[2],
			m[6] * a[0] + m[7] * a[1] + m[8] * a[2]};
	}
};

/* w + xi + yj + zk. */
struct Quatd {
	double w = 1.0, x = 0.0, y = 0.0, z = 0.0;

	Quatd() = default;
	Quatd(double w_, double x_, double y_, double z_) : w(w_), x(x_), y(y_), z(z_) {}

	double dot(const Quatd &o) const { return w * o.w + x * o.x + y * o.y + z * o.z; }
	Quatd operator-() const { return {-w, -x, -y, -z}; }
	Quatd operator+(const Quatd &o) const { return {w + o.w, x + o.x, y + o.y, z + o.z}; }
	Quatd operator*(double s) const { return {w * s, x * s, y * s, z * s}; }
	Quatd normalize() const
	{
		const double n = std::sqrt(dot(*this));
		return n > 1e-12 ? *this * (1.0 / n) : Quatd();
	}

	/* From a rotation matrix (Shepperd's method: stable for any angle). */
	static Quatd createFromRotMat(const Matx33d &R)
	{
		const double tr = R(0, 0) + R(1, 1) + R(2, 2);
		Quatd q;
		if (tr > 0.0) {
			const double s = std::sqrt(tr + 1.0) * 2.0;
			q = {0.25 * s, (R(2, 1) - R(1, 2)) / s, (R(0, 2) - R(2, 0)) / s, (R(1, 0) - R(0, 1)) / s};
		} else if (R(0, 0) > R(1, 1) && R(0, 0) > R(2, 2)) {
			const double s = std::sqrt(1.0 + R(0, 0) - R(1, 1) - R(2, 2)) * 2.0;
			q = {(R(2, 1) - R(1, 2)) / s, 0.25 * s, (R(0, 1) + R(1, 0)) / s, (R(0, 2) + R(2, 0)) / s};
		} else if (R(1, 1) > R(2, 2)) {
			const double s = std::sqrt(1.0 + R(1, 1) - R(0, 0) - R(2, 2)) * 2.0;
			q = {(R(0, 2) - R(2, 0)) / s, (R(0, 1) + R(1, 0)) / s, 0.25 * s, (R(1, 2) + R(2, 1)) / s};
		} else {
			const double s = std::sqrt(1.0 + R(2, 2) - R(0, 0) - R(1, 1)) * 2.0;
			q = {(R(1, 0) - R(0, 1)) / s, (R(0, 2) + R(2, 0)) / s, (R(1, 2) + R(2, 1)) / s, 0.25 * s};
		}
		return q.normalize();
	}

	Matx33d toRotMat3x3() const
	{
		const Quatd q = normalize();
		const double xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
		const double xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
		const double wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
		return {1.0 - 2.0 * (yy + zz), 2.0 * (xy - wz),       2.0 * (xz + wy),
			2.0 * (xy + wz),       1.0 - 2.0 * (xx + zz), 2.0 * (yz - wx),
			2.0 * (xz - wy),       2.0 * (yz + wx),       1.0 - 2.0 * (xx + yy)};
	}

	/* Spherical interpolation from a (t = 0) to b (t = 1), the short way. */
	static Quatd slerp(const Quatd &a, const Quatd &b, double t)
	{
		const Quatd qa = a.normalize();
		Quatd qb = b.normalize();
		double d = qa.dot(qb);
		if (d < 0.0) {
			qb = -qb;
			d = -d;
		}
		if (d > 0.9995) /* nearly the same: a straight blend is exact enough */
			return (qa * (1.0 - t) + qb * t).normalize();
		const double theta = std::acos(d);
		const double s = std::sin(theta);
		return qa * (std::sin((1.0 - t) * theta) / s) + qb * (std::sin(t * theta) / s);
	}
};

} // namespace FaceMask
