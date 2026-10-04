#ifndef _INCLUDE_MODULECONFIG_H
#define _INCLUDE_MODULECONFIG_H

#ifndef HAVE_STDINT_H
#define HAVE_STDINT_H
#endif

#ifndef HAVE_STRING_H
#define HAVE_STRING_H
#endif

#ifndef NO_ALLOC_OVERRIDES
#define NO_ALLOC_OVERRIDES
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#ifndef NO_MSVC8_AUTO_COMPAT
#define NO_MSVC8_AUTO_COMPAT
#endif

#define MODULE_NAME "NavMesh Core"
#define MODULE_VERSION "1.0.0"
#define MODULE_AUTHOR "Ziyad"
#define MODULE_LOGTAG "NAVMESH"
#define MODULE_LIBRARY "navmesh"
#define MODULE_LIBCLASS ""

#define FN_AMXX_ATTACH OnAmxxAttach
#define FN_AMXX_DETACH OnAmxxDetach
#define FN_AMXX_PLUGINSLOADED OnPluginsLoaded
#define FN_AMXX_PLUGINSUNLOADED OnPluginsUnloaded

// Forward declarations for amxxmodule.cpp
void OnAmxxAttach();
void OnAmxxDetach();
void OnPluginsLoaded();
void OnPluginsUnloaded();

#endif
