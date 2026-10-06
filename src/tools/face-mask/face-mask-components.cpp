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
#include "face-mask-components.hpp"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>

namespace FaceMask {

const std::vector<Component> &components()
{
	static const std::vector<Component> list{
#if defined(_WIN32)
		{"onnxruntime.dll", false, 11569696,
		 "4cb41e89b8bf30578e1dd95e9c40292d61974a4bfcd666409302c4f0c5aa8ce0"},
#elif defined(__APPLE__)
		{"libonnxruntime.1.20.1.dylib", false, 54114312,
		 "b7b762db4fe82fbc681e7c738bc9dfb686f61f7da5d1c903f0bdba1a62641c5c"},
#else
		{"libonnxruntime.so.1.20.1", false, 16559416,
		 "a5faaf78a37590d3fe640f887620e74f6022d34550172b91ad2131bf0ad77d64"},
#endif
		{"face_detection_yunet_2023mar.onnx", true, 232589,
		 "8f2383e4dd3cfbb4553ea8718107fc0423210dc964f9f4280604804ed2552fa4"},
		{"headpose_mobilenetv2.onnx", true, 8905103,
		 "1e902872868e483bd0e4f8f4a8ff2a4d61c2ccbca9dadf748e5479b5cc86a9e9"},
		{"face_landmark_468.onnx", true, 2440358,
		 "ed487104519b0a88cb2cb2ec3678e183f447fb9dd63560998e960ffbaa8fb335"},
	};
	return list;
}

QString componentUrl(const Component &component)
{
	return QString::fromLatin1(kComponentsRelease) + QString::fromLatin1(component.file);
}

qint64 componentsBytes()
{
	qint64 total = 0;
	for (const Component &c : components())
		total += c.size;
	return total;
}

bool fileMatches(const QString &path, qint64 size, const QString &sha256)
{
	/* The size first: a missing or partial file costs no hashing. */
	if (QFileInfo(path).size() != size)
		return false;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
		return false;
	QCryptographicHash hash(QCryptographicHash::Sha256);
	if (!hash.addData(&file))
		return false;
	return QString::fromLatin1(hash.result().toHex()) == sha256.toLower();
}

} // namespace FaceMask
