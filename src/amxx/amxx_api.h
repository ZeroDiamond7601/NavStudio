#pragma once

#include "amxxmodule.h"
#include "../bsp/bsp_file.h"
#include "../nav/nav_file.h"
#include "../nav/nav_path.h"
#include <unordered_map>

extern BSPFile g_bsp;
extern NavMesh g_nav;
extern std::unordered_map<int, NavPath> g_activePaths;
extern int g_nextPathId;

void RegisterBSPNatives();
void RegisterNavNatives();
