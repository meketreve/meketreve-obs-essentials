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

/* What the face mask needs besides the plugin: onnxruntime and the face
 * models, downloaded once into the plugin's config folder (face-mask/). */
namespace FaceMask::Setup {

/* Checks every file (size and SHA-256) and opens onnxruntime. True when all
 * is in place; cheap once it is. Any thread. */
bool check();
bool ready();
/* Goes up each time the components become ready: filters made before that
 * start tracking when they see it change. */
int generation();

/* The path of a model, or empty when it is not there. */
std::string modelPath(const char *file);
/* What the download would take, in MB (rounded up). */
int downloadMegabytes();

/* Downloads what is missing, with a progress window on the OBS window, and
 * checks it all again at the end. UI thread. */
void download();
bool downloading();

/* MEKETREVE_SELFTEST_FACEMASK=download: downloads once OBS has loaded. */
void registerSelfTest();

} // namespace FaceMask::Setup
