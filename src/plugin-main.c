/*
Plugin Name
Copyright (C) <Year> <Developer> <Email Address>

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

#include <obs-module.h>
#include <plugin-support.h>

#include "tools/bass-shake.h"
#include "tools/voice-fx.h"
#include "tools/unified-chat.h"

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")

bool obs_module_load(void)
{
	/* Register every tool in the toolkit here. */
	bass_shake_register();
	voice_fx_register();
	unified_chat_register();

	obs_log(LOG_INFO, "Meketreve OBS Essentials loaded (version %s)", PLUGIN_VERSION);
	return true;
}

void obs_module_unload(void)
{
	unified_chat_unregister();
	obs_log(LOG_INFO, "plugin unloaded");
}
