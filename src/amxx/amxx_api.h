#pragma once

#include "amxxmodule.h"
#include "../bsp/bsp_file.h"
#include "../nav/nav_file.h"
#include "../nav/nav_path.h"
#include "../nav/async_pathfinder.h"
#include <unordered_map>
#include <mutex>
#include <atomic>

extern BSPFile g_bsp;
extern NavMesh g_nav;
extern std::unordered_map<int, NavPath> g_activePaths;
extern std::mutex g_activePathsMutex;
extern std::atomic<int> g_nextPathId;

extern int g_fwdMapLoaded;
extern int g_fwdPathComputed;

void RegisterBSPNatives();
void RegisterNavNatives();
void DispatchAsyncPathResult(const AsyncPathResult& result);
void FireMapLoadedForward(bool bspLoaded, bool navLoaded, int areaCount);

// Metamod Hooks
#ifdef USE_METAMOD
void ServerActivate_Post(edict_t *pEdictList, int edictCount, int clientMax);
void ServerDeactivate();
void StartFrame_Post();
#endif
