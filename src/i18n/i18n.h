/*
Meketreve OBS Essentials - Languages
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

#ifdef __cplusplus
extern "C" {
#endif

/* Reads language.json and both locale files once, and returns the locale
 * the module's text lookup should load ("pt-BR" or "en-US") for the OBS
 * locale it was given. */
const char *meketreve_i18n_start(const char *obs_locale);

#ifdef __cplusplus
}
#endif
