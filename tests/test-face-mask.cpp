/*
Meketreve OBS Essentials - Face mask tests
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
#include "face-boxes.hpp"
#include "face-image.hpp"
#include "face-math.hpp"
#include "pose.hpp"
#include "smoothing.hpp"

#include <QTest>

#include <cmath>
#include <vector>

using namespace FaceMask;

class TestFaceMask : public QObject {
	Q_OBJECT

private slots:
	void sampleCopiesResizesAndSwapsChannels()
	{
		/* 2x1 picture: pixel 0 is B=10 G=20 R=30, pixel 1 is B=50 G=60 R=70. */
		const Image img{2, 1, {10, 20, 30, 50, 60, 70}};
		std::vector<float> out(6, -1.f);
		sampleToPlanes(img, {0, 0, 2, 1}, 2, 1, out.data(), 2, 2, false, {1, 1, 1}, {0, 0, 0});
		QCOMPARE(out, (std::vector<float>{10, 50, 20, 60, 30, 70}));
		/* RGB order puts R in the first plane; mul/add apply per plane. */
		sampleToPlanes(img, {0, 0, 2, 1}, 2, 1, out.data(), 2, 2, true, {1, 2, 1}, {0, 0, -10});
		QCOMPARE(out, (std::vector<float>{30, 70, 40, 120, 0, 40}));
		/* 2 -> 1 pixel averages them, like cv::resize. */
		std::vector<float> one(3);
		sampleToPlanes(img, {0, 0, 2, 1}, 1, 1, one.data(), 1, 1, false, {1, 1, 1}, {0, 0, 0});
		QCOMPARE(one, (std::vector<float>{30, 40, 50}));
		/* A region reads only from it; rows land rowStride apart. */
		std::vector<float> wide(6, -1.f);
		sampleToPlanes(img, {1, 0, 1, 1}, 1, 1, wide.data(), 3, 2, false, {1, 1, 1}, {0, 0, 0});
		QCOMPARE(wide, (std::vector<float>{50, -1, 60, -1, 70, -1}));
	}

	void rgbaBecomesBgrAndFlips()
	{
		/* 1x2 RGBA with a padded row (linesize 8). */
		const uint8_t rgba[16] = {1, 2, 3, 255, 0, 0, 0, 0, 4, 5, 6, 255, 0, 0, 0, 0};
		const Image up = imageFromRgba(rgba, 1, 2, 8, false);
		QCOMPARE(up.bgr, (std::vector<uint8_t>{3, 2, 1, 6, 5, 4}));
		const Image flipped = imageFromRgba(rgba, 1, 2, 8, true);
		QCOMPARE(flipped.bgr, (std::vector<uint8_t>{6, 5, 4, 3, 2, 1}));
	}

	void yunetDecodesOneCell()
	{
		/* A 64x64 input at stride 32 has 2x2 cells; only cell (row 1,
		 * col 0) has a face. */
		std::vector<float> cls(4, 0.f), obj(4, 0.f), bbox(16, 0.f), kps(40, 0.f);
		cls[2] = 0.81f;
		obj[2] = 0.81f;
		bbox[8] = 0.5f; /* centre offset x, y */
		bbox[9] = 0.5f;
		bbox[10] = std::log(2.f); /* size: exp() of the cell, x2 */
		bbox[11] = 0.f;
		kps[20] = 0.25f;
		kps[21] = 0.75f;
		std::vector<FaceBox> faces;
		decodeYuNet(32, 64, cls.data(), obj.data(), bbox.data(), kps.data(), 0.5f, faces);
		QCOMPARE(faces.size(), size_t(1));
		const FaceBox &f = faces.front();
		QVERIFY(std::abs(f.score - 0.81f) < 1e-5f);
		QVERIFY(std::abs(f.x - (16.f - 32.f)) < 1e-4f);
		QVERIFY(std::abs(f.y - (48.f - 16.f)) < 1e-4f);
		QVERIFY(std::abs(f.w - 64.f) < 1e-4f);
		QVERIFY(std::abs(f.h - 32.f) < 1e-4f);
		QVERIFY(std::abs(f.kps[0] - 8.f) < 1e-4f);
		QVERIFY(std::abs(f.kps[1] - 56.f) < 1e-4f);
		/* Under the threshold: nothing. */
		faces.clear();
		decodeYuNet(32, 64, cls.data(), obj.data(), bbox.data(), kps.data(), 0.9f, faces);
		QVERIFY(faces.empty());
	}

	void suppressionKeepsTheBestOfOverlaps()
	{
		FaceBox a, b, c;
		a = {0, 0, 10, 10, {}, 0.6f};
		b = {1, 1, 10, 10, {}, 0.9f}; /* overlaps a a lot */
		c = {50, 50, 10, 10, {}, 0.7f};
		const std::vector<FaceBox> kept = suppressBoxes({a, b, c}, 0.3f, 10);
		QCOMPARE(kept.size(), size_t(2));
		QCOMPARE(kept[0].score, 0.9f);
		QCOMPARE(kept[1].score, 0.7f);
		QCOMPARE(suppressBoxes({a, b, c}, 0.3f, 1).size(), size_t(1));
	}

	void quaternionsRoundTripAndSlerp()
	{
		const double s = std::sin(0.7), c = std::cos(0.7);
		const Matx33d rz(c, -s, 0, s, c, 0, 0, 0, 1);
		const Matx33d back = Quatd::createFromRotMat(rz).toRotMat3x3();
		for (int i = 0; i < 9; ++i)
			QVERIFY(std::abs(back.m[i] - rz.m[i]) < 1e-12);
		/* Halfway between none and 90 degrees about Z is 45 degrees. */
		const Matx33d r90(0, -1, 0, 1, 0, 0, 0, 0, 1);
		const Matx33d half = Quatd::slerp(Quatd(), Quatd::createFromRotMat(r90), 0.5).toRotMat3x3();
		QVERIFY(std::abs(half(0, 0) - std::cos(kPi / 4)) < 1e-12);
		QVERIFY(std::abs(half(1, 0) - std::sin(kPi / 4)) < 1e-12);
		/* The short way even when the target has the other sign. */
		const Quatd q = Quatd::createFromRotMat(r90);
		const Matx33d same = Quatd::slerp(q, -q, 0.5).toRotMat3x3();
		for (int i = 0; i < 9; ++i)
			QVERIFY(std::abs(same.m[i] - r90.m[i]) < 1e-12);
	}

	void oneEuroHoldsAStillValue()
	{
		OneEuro f(1.0, 0.0);
		QCOMPARE(f.filter(5.0, 0.016), 5.0);
		for (int i = 0; i < 10; ++i)
			QVERIFY(std::abs(f.filter(5.0, 0.016) - 5.0) < 1e-12);
		/* A jump is followed partly, not at once. */
		const double next = f.filter(15.0, 0.016);
		QVERIFY(next > 5.0 && next < 15.0);
	}

	void poseAnchorsOnTheEyes()
	{
		FaceResult face;
		face.valid = true;
		face.has_R = true;
		face.right_eye = {900.f, 500.f};
		face.left_eye = {1000.f, 500.f};
		HeadPose pose;
		QVERIFY(build_pose_from_net(face, 1920, 1080, 60.0, false, false, false, pose));
		Point2d screen;
		double z = 0.0;
		project_point(pose, facemodel::eye_anchor(), screen, z);
		QVERIFY(z > 0.0);
		QVERIFY(std::abs(screen.x - 950.0) < 1e-6);
		QVERIFY(std::abs(screen.y - 500.0) < 1e-6);
		/* The model's eyes land on the measured ones, 100 px apart. */
		Point2d right, left;
		project_point(pose, facemodel::eye_anchor() - facemodel::axis_right() * 165.0, right, z);
		project_point(pose, facemodel::eye_anchor() + facemodel::axis_right() * 165.0, left, z);
		QVERIFY(std::abs(std::abs(left.x - right.x) - 100.0) < 1e-6);
		/* No eye distance, no pose. */
		face.left_eye = face.right_eye;
		QVERIFY(!build_pose_from_net(face, 1920, 1080, 60.0, false, false, false, pose));
	}
};

QTEST_GUILESS_MAIN(TestFaceMask)
#include "test-face-mask.moc"
