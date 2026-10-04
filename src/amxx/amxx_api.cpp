#include "amxx_api.h"

BSPFile g_bsp;
NavMesh g_nav;
std::unordered_map<int, NavPath> g_activePaths;
int g_nextPathId = 1;

void OnAmxxAttach() {
    RegisterBSPNatives();
    RegisterNavNatives();
    MF_Log("[%s] v%s initialized successfully.", MODULE_NAME, MODULE_VERSION);
}

void OnAmxxDetach() {
    g_activePaths.clear();
    g_nav.Unload();
    g_bsp.Unload();
}

void OnPluginsLoaded() {
    // Map changes or server restarts
    g_activePaths.clear();
}
