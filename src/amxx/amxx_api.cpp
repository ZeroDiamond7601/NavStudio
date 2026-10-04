#include "amxx_api.h"

BSPFile g_bsp;
NavMesh g_nav;
std::unordered_map<int, NavPath> g_activePaths;
std::mutex g_activePathsMutex;
std::atomic<int> g_nextPathId(1);

int g_fwdMapLoaded = -1;
int g_fwdPathComputed = -1;

void OnAmxxAttach() {
    RegisterBSPNatives();
    RegisterNavNatives();

    // Register custom event forwards
    g_fwdMapLoaded = MF_RegisterForward("nav_on_map_loaded", ET_IGNORE, FP_CELL, FP_CELL, FP_CELL, FP_DONE);
    g_fwdPathComputed = MF_RegisterForward("nav_on_path_computed", ET_IGNORE, FP_CELL, FP_CELL, FP_FLOAT, FP_CELL, FP_DONE);

    // Initialize async worker pool
    AsyncPathManager::Get().Initialize();

    MF_Log("[%s] v%s initialized successfully.", MODULE_NAME, MODULE_VERSION);
}

void OnAmxxDetach() {
    // Shutdown async thread pool
    AsyncPathManager::Get().Shutdown();

    {
        std::lock_guard<std::mutex> lock(g_activePathsMutex);
        g_activePaths.clear();
    }

    g_nav.Unload();
    g_bsp.Unload();

    g_fwdMapLoaded = -1;
    g_fwdPathComputed = -1;
}

void OnPluginsLoaded() {
    std::lock_guard<std::mutex> lock(g_activePathsMutex);
    g_activePaths.clear();
}

void OnPluginsUnloaded() {
    AsyncPathManager::Get().ClearQueue();

    std::lock_guard<std::mutex> lock(g_activePathsMutex);
    g_activePaths.clear();
}

void FireMapLoadedForward(bool bspLoaded, bool navLoaded, int areaCount) {
    if (g_fwdMapLoaded > 0) {
        MF_ExecuteForward(
            g_fwdMapLoaded,
            static_cast<cell>(bspLoaded ? 1 : 0),
            static_cast<cell>(navLoaded ? 1 : 0),
            static_cast<cell>(areaCount)
        );
    }
}

void DispatchAsyncPathResult(const AsyncPathResult& result) {
    if (g_fwdPathComputed > 0) {
        MF_ExecuteForward(
            g_fwdPathComputed,
            static_cast<cell>(result.taskId),
            static_cast<cell>(result.pathId),
            amx_ftoc(result.length),
            static_cast<cell>(result.success ? 1 : 0)
        );
    }
}

#ifdef USE_METAMOD
void ServerActivate_Post(edict_t *pEdictList, int edictCount, int clientMax) {
    if (!gpGlobals || !gpGlobals->mapname) return;

    const char* mapname = STRING(gpGlobals->mapname);
    if (!mapname || mapname[0] == '\0') return;

    char bspPath[256];
    char navPath[256];
    snprintf(bspPath, sizeof(bspPath), "maps/%s.bsp", mapname);
    snprintf(navPath, sizeof(navPath), "maps/%s.nav", mapname);

    bool bspOk = g_bsp.Load(bspPath);
    bool navOk = g_nav.Load(navPath);

    if (bspOk && navOk) {
        g_nav.BuildLadders(&g_bsp);
    }

    FireMapLoadedForward(bspOk, navOk, static_cast<int>(g_nav.GetAreaCount()));
}

void ServerDeactivate() {
    AsyncPathManager::Get().ClearQueue();

    {
        std::lock_guard<std::mutex> lock(g_activePathsMutex);
        g_activePaths.clear();
    }

    g_nav.Unload();
    g_bsp.Unload();
}

void StartFrame_Post() {
    // Poll and dispatch completed background worker jobs on the main server thread
    AsyncPathManager::Get().ProcessCompleted();
}
#endif
