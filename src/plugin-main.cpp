#include <obs-module.h>

#include "nowplaylisting-source.hpp"

#ifndef NOWPLAYLISTING_VERSION
#define NOWPLAYLISTING_VERSION "unknown"
#endif

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("nowplaylisting", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Playlist-driven media source for OBS with metadata overlays.";
}

bool obs_module_load(void)
{
	register_nowplaylisting_source();
	blog(LOG_INFO, "[nowplaylisting] module loaded (version=%s, build=%s %s)", NOWPLAYLISTING_VERSION,
	     __DATE__, __TIME__);
	return true;
}

void obs_module_unload(void)
{
	nowplaylist_shutdown();
	blog(LOG_INFO, "[nowplaylisting] module unloaded");
}
