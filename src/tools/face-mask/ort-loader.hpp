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

#include <string>

namespace FaceMask {

/* onnxruntime is not linked: the plugin opens the library the first time a
 * face mask needs it, so OBS starts the same with or without it. */

/* The onnxruntime build the filter is made for (headers in onnxruntime/). */
constexpr const char *kOnnxRuntimeVersion = "1.20.1";

/* The library's file name on this system (libonnxruntime.so.1.20.1,
 * onnxruntime.dll, libonnxruntime.1.20.1.dylib). */
std::string onnxRuntimeFileName();

/* Opens the library at path and sets up the C++ API. True when it is ready
 * (also on later calls once it worked); else false with the reason in
 * *error. Thread-safe. Never unloaded. */
bool loadOnnxRuntime(const std::string &path, std::string *error = nullptr);
bool onnxRuntimeReady();

} // namespace FaceMask
