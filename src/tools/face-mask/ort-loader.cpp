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
#include "ort-loader.hpp"

#include <onnxruntime_cxx_api.h>

#include <atomic>
#include <filesystem>
#include <mutex>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace FaceMask {

namespace {

std::mutex g_mutex;
std::atomic<bool> g_ready{false};

using GetApiBase = const OrtApiBase *(ORT_API_CALL *)();

/* The library's OrtGetApiBase, or null with the reason. */
GetApiBase openLibrary(const std::string &path, std::string &why)
{
#ifdef _WIN32
	/* By full path, its own folder first for anything it needs: another
	 * plugin may have loaded a different onnxruntime.dll by name. */
	const std::wstring wide = std::filesystem::u8path(path).wstring();
	HMODULE lib = LoadLibraryExW(wide.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
	if (!lib) {
		why = "LoadLibrary error " + std::to_string(GetLastError());
		return nullptr;
	}
	auto *fn = reinterpret_cast<GetApiBase>(reinterpret_cast<void *>(GetProcAddress(lib, "OrtGetApiBase")));
#else
	void *lib = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
	if (!lib) {
		const char *e = dlerror();
		why = e ? e : "dlopen failed";
		return nullptr;
	}
	auto *fn = reinterpret_cast<GetApiBase>(dlsym(lib, "OrtGetApiBase"));
#endif
	if (!fn)
		why = "no OrtGetApiBase in the library";
	return fn;
}

} // namespace

std::string onnxRuntimeFileName()
{
#if defined(_WIN32)
	return "onnxruntime.dll";
#elif defined(__APPLE__)
	return std::string("libonnxruntime.") + kOnnxRuntimeVersion + ".dylib";
#else
	return std::string("libonnxruntime.so.") + kOnnxRuntimeVersion;
#endif
}

bool onnxRuntimeReady()
{
	return g_ready.load();
}

bool loadOnnxRuntime(const std::string &path, std::string *error)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	if (g_ready.load())
		return true;
	std::string why;
	if (std::error_code ec; !std::filesystem::is_regular_file(std::filesystem::u8path(path), ec)) {
		why = "not found: " + path;
	} else if (const GetApiBase fn = openLibrary(path, why)) {
		const OrtApiBase *base = fn();
		/* An older library has no API of our version. */
		const OrtApi *api = base ? base->GetApi(ORT_API_VERSION) : nullptr;
		if (api) {
			Ort::InitApi(api);
			g_ready.store(true);
			return true;
		}
		why = std::string("onnxruntime ") + (base ? base->GetVersionString() : "?") + " is too old, need " +
		      kOnnxRuntimeVersion;
	}
	if (error)
		*error = why;
	return false;
}

} // namespace FaceMask
