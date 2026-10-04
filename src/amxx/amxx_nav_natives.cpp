#include "amxx_api.h"
#include <cstdio>
#include <cstring>
#include <string>

// Helper to resolve nav file path
static std::string FindMapNAV(const char* mapname) {
    char path[512];

    if (mapname && strstr(mapname, ".nav") != nullptr) {
        snprintf(path, sizeof(path), "%s", mapname);
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); return std::string(path); }
    }

    const char* baseName = (mapname && mapname[0] != '\0') ? mapname : g_bsp.GetMapName().c_str();
    if (!baseName || baseName[0] == '\0') {
        return "";
    }

    // Try czero/maps/<mapname>.nav
    snprintf(path, sizeof(path), "czero/maps/%s.nav", baseName);
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Try cstrike/maps/<mapname>.nav
    snprintf(path, sizeof(path), "cstrike/maps/%s.nav", baseName);
    f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Try maps/<mapname>.nav
    snprintf(path, sizeof(path), "maps/%s.nav", baseName);
    f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Fallback
    snprintf(path, sizeof(path), "cstrike/maps/%s.nav", baseName);
    return std::string(path);
}

// native nav_load(const mapname[] = "");
static cell AMX_NATIVE_CALL nav_load(AMX *amx, cell *params) {
    char mapname[128] = {0};
    if (params[0] >= 1) {
        int len = 0;
        char *str = MF_GetAmxString(amx, params[1], 1, &len);
        if (str && len > 0) {
            strncpy(mapname, str, sizeof(mapname) - 1);
        }
    }

    std::string navPath = FindMapNAV(mapname);
    if (navPath.empty() || !g_nav.Load(navPath)) {
        MF_Log("[%s] Failed to load NAV file '%s'.", MODULE_LOGTAG, navPath.c_str());
        return 0;
    }

    // Link ladders if BSP is loaded
    if (g_bsp.IsLoaded()) {
        g_nav.BuildLadders(&g_bsp);
    }

    MF_Log("[%s] NavMesh loaded successfully: %d areas, %d places, %d ladders.",
           MODULE_LOGTAG,
           static_cast<int>(g_nav.GetAreaCount()),
           static_cast<int>(g_nav.GetPlaceNames().size()),
           static_cast<int>(g_nav.GetLadders().size()));

    return 1;
}

// native nav_unload();
static cell AMX_NATIVE_CALL nav_unload(AMX *amx, cell *params) {
    g_activePaths.clear();
    g_nav.Unload();
    return 1;
}

// native nav_is_loaded();
static cell AMX_NATIVE_CALL nav_is_loaded(AMX *amx, cell *params) {
    return g_nav.IsLoaded() ? 1 : 0;
}

// native nav_get_area_count();
static cell AMX_NATIVE_CALL nav_get_area_count(AMX *amx, cell *params) {
    return static_cast<cell>(g_nav.GetAreaCount());
}

// native nav_get_area_by_id(area_id);
static cell AMX_NATIVE_CALL nav_get_area_by_id(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return -1;
    uint32_t id = static_cast<uint32_t>(params[1]);

    for (size_t i = 0; i < g_nav.GetAreaCount(); i++) {
        if (g_nav.GetArea(i)->GetID() == id) {
            return static_cast<cell>(i);
        }
    }
    return -1;
}

// native nav_get_area_id(area_index);
static cell AMX_NATIVE_CALL nav_get_area_id(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    return area ? static_cast<cell>(area->GetID()) : 0;
}

// native nav_get_nearest_area(const Float:pos[3], Float:max_dist = 1000.0);
static cell AMX_NATIVE_CALL nav_get_nearest_area(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return -1;

    cell *c_pos = MF_GetAmxAddr(amx, params[1]);
    float maxDist = (params[0] >= 2) ? amx_ctof(params[2]) : 1000.0f;
    Vector3 pos(amx_ctof(c_pos[0]), amx_ctof(c_pos[1]), amx_ctof(c_pos[2]));

    NavArea* nearest = g_nav.GetNearestArea(pos, maxDist);
    if (!nearest) return -1;

    for (size_t i = 0; i < g_nav.GetAreaCount(); i++) {
        if (g_nav.GetArea(i) == nearest) {
            return static_cast<cell>(i);
        }
    }
    return -1;
}

// native nav_get_area_at_point(const Float:pos[3], Float:max_z_delta = 40.0);
static cell AMX_NATIVE_CALL nav_get_area_at_point(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return -1;

    cell *c_pos = MF_GetAmxAddr(amx, params[1]);
    float maxZ = (params[0] >= 2) ? amx_ctof(params[2]) : 40.0f;
    Vector3 pos(amx_ctof(c_pos[0]), amx_ctof(c_pos[1]), amx_ctof(c_pos[2]));

    NavArea* area = g_nav.GetAreaAtPoint(pos);
    if (!area || !area->Contains(pos, maxZ)) return -1;

    for (size_t i = 0; i < g_nav.GetAreaCount(); i++) {
        if (g_nav.GetArea(i) == area) {
            return static_cast<cell>(i);
        }
    }
    return -1;
}

// native nav_get_area_center(area_index, Float:center[3]);
static cell AMX_NATIVE_CALL nav_get_area_center(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    if (!area) return 0;

    cell *c_center = MF_GetAmxAddr(amx, params[2]);
    const Vector3& center = area->GetCenter();
    c_center[0] = amx_ftoc(center.x);
    c_center[1] = amx_ftoc(center.y);
    c_center[2] = amx_ftoc(center.z);
    return 1;
}

// native nav_get_area_extent(area_index, Float:mins[3], Float:maxs[3]);
static cell AMX_NATIVE_CALL nav_get_area_extent(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    if (!area) return 0;

    cell *c_mins = MF_GetAmxAddr(amx, params[2]);
    cell *c_maxs = MF_GetAmxAddr(amx, params[3]);
    const NavExtent& ext = area->GetExtent();

    c_mins[0] = amx_ftoc(ext.lo.x);
    c_mins[1] = amx_ftoc(ext.lo.y);
    c_mins[2] = amx_ftoc(ext.lo.z);

    c_maxs[0] = amx_ftoc(ext.hi.x);
    c_maxs[1] = amx_ftoc(ext.hi.y);
    c_maxs[2] = amx_ftoc(ext.hi.z);
    return 1;
}

// native Float:nav_get_area_z(area_index, Float:x, Float:y);
static cell AMX_NATIVE_CALL nav_get_area_z(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return amx_ftoc(0.0f);
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    if (!area) return amx_ftoc(0.0f);

    float x = amx_ctof(params[2]);
    float y = amx_ctof(params[3]);
    float z = area->GetZ(x, y);
    return amx_ftoc(z);
}

// native nav_get_area_flags(area_index);
static cell AMX_NATIVE_CALL nav_get_area_flags(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    return area ? static_cast<cell>(area->GetAttributes()) : 0;
}

// native nav_is_point_in_area(area_index, const Float:pos[3], Float:max_z_delta = 40.0);
static cell AMX_NATIVE_CALL nav_is_point_in_area(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    if (!area) return 0;

    cell *c_pos = MF_GetAmxAddr(amx, params[2]);
    float maxZ = (params[0] >= 3) ? amx_ctof(params[3]) : 40.0f;
    Vector3 pos(amx_ctof(c_pos[0]), amx_ctof(c_pos[1]), amx_ctof(c_pos[2]));

    return area->Contains(pos, maxZ) ? 1 : 0;
}

// native nav_get_closest_point(area_index, const Float:pos[3], Float:closest[3]);
static cell AMX_NATIVE_CALL nav_get_closest_point(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    if (!area) return 0;

    cell *c_pos = MF_GetAmxAddr(amx, params[2]);
    cell *c_close = MF_GetAmxAddr(amx, params[3]);
    Vector3 pos(amx_ctof(c_pos[0]), amx_ctof(c_pos[1]), amx_ctof(c_pos[2]));

    Vector3 close = area->GetClosestPoint(pos);
    c_close[0] = amx_ftoc(close.x);
    c_close[1] = amx_ftoc(close.y);
    c_close[2] = amx_ftoc(close.z);
    return 1;
}

// native Float:nav_get_distance_to_area(area_index, const Float:pos[3]);
static cell AMX_NATIVE_CALL nav_get_distance_to_area(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return amx_ftoc(999999.0f);
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    if (!area) return amx_ftoc(999999.0f);

    cell *c_pos = MF_GetAmxAddr(amx, params[2]);
    Vector3 pos(amx_ctof(c_pos[0]), amx_ctof(c_pos[1]), amx_ctof(c_pos[2]));

    float distSq = area->GetDistanceSquaredToPoint(pos);
    return amx_ftoc(std::sqrt(distSq));
}

// native nav_get_adjacent_count(area_index, direction);
static cell AMX_NATIVE_CALL nav_get_adjacent_count(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    int dir = params[2];
    if (!area || dir < 0 || dir >= NUM_NAV_DIRECTIONS) return 0;

    return static_cast<cell>(area->GetAdjacentCount(static_cast<NavDirType>(dir)));
}

// native nav_get_adjacent_area(area_index, direction, adj_index);
static cell AMX_NATIVE_CALL nav_get_adjacent_area(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return -1;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    int dir = params[2];
    size_t adjIdx = static_cast<size_t>(params[3]);
    if (!area || dir < 0 || dir >= NUM_NAV_DIRECTIONS) return -1;

    NavArea* adj = area->GetAdjacentArea(static_cast<NavDirType>(dir), adjIdx);
    if (!adj) return -1;

    for (size_t i = 0; i < g_nav.GetAreaCount(); i++) {
        if (g_nav.GetArea(i) == adj) return static_cast<cell>(i);
    }
    return -1;
}

// native nav_is_connected(area_a, area_b, direction = -1);
static cell AMX_NATIVE_CALL nav_is_connected(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* a = g_nav.GetArea(static_cast<size_t>(params[1]));
    NavArea* b = g_nav.GetArea(static_cast<size_t>(params[2]));
    int dir = (params[0] >= 3) ? params[3] : -1;

    if (!a || !b) return 0;
    return a->IsConnected(b, dir) ? 1 : 0;
}

// native nav_get_place_name(area_index, output[], maxlen);
static cell AMX_NATIVE_CALL nav_get_place_name(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    if (!area) return 0;

    const std::string& name = area->GetPlaceName();
    int maxlen = params[3];
    return static_cast<cell>(MF_SetAmxString(amx, params[2], name.c_str(), maxlen));
}

// native nav_get_hiding_spot_count(area_index);
static cell AMX_NATIVE_CALL nav_get_hiding_spot_count(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    return area ? static_cast<cell>(area->GetHidingSpots().size()) : 0;
}

// native nav_get_hiding_spot(area_index, spot_index, Float:pos[3], &flags);
static cell AMX_NATIVE_CALL nav_get_hiding_spot(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;
    NavArea* area = g_nav.GetArea(static_cast<size_t>(params[1]));
    if (!area) return 0;

    size_t spotIdx = static_cast<size_t>(params[2]);
    const auto& spots = area->GetHidingSpots();
    if (spotIdx >= spots.size()) return 0;

    cell *c_pos = MF_GetAmxAddr(amx, params[3]);
    cell *c_flags = MF_GetAmxAddr(amx, params[4]);

    c_pos[0] = amx_ftoc(spots[spotIdx].pos.x);
    c_pos[1] = amx_ftoc(spots[spotIdx].pos.y);
    c_pos[2] = amx_ftoc(spots[spotIdx].pos.z);
    *c_flags = spots[spotIdx].flags;
    return 1;
}

// native nav_get_ladder_count();
static cell AMX_NATIVE_CALL nav_get_ladder_count(AMX *amx, cell *params) {
    return static_cast<cell>(g_nav.GetLadders().size());
}

// native nav_get_ladder_info(ladder_index, Float:top[3], Float:bottom[3], &Float:length, &Float:width, &direction);
static cell AMX_NATIVE_CALL nav_get_ladder_info(AMX *amx, cell *params) {
    size_t idx = static_cast<size_t>(params[1]);
    const auto& ladders = g_nav.GetLadders();
    if (idx >= ladders.size()) return 0;

    const NavLadder* l = ladders[idx];
    cell *c_top = MF_GetAmxAddr(amx, params[2]);
    cell *c_bot = MF_GetAmxAddr(amx, params[3]);
    cell *c_len = MF_GetAmxAddr(amx, params[4]);
    cell *c_wid = MF_GetAmxAddr(amx, params[5]);
    cell *c_dir = MF_GetAmxAddr(amx, params[6]);

    c_top[0] = amx_ftoc(l->top.x);
    c_top[1] = amx_ftoc(l->top.y);
    c_top[2] = amx_ftoc(l->top.z);

    c_bot[0] = amx_ftoc(l->bottom.x);
    c_bot[1] = amx_ftoc(l->bottom.y);
    c_bot[2] = amx_ftoc(l->bottom.z);

    *c_len = amx_ftoc(l->length);
    *c_wid = amx_ftoc(l->width);
    *c_dir = static_cast<cell>(l->dir);
    return 1;
}

// native nav_build_path(const Float:start[3], const Float:goal[3], &path_id, flags = 0);
static cell AMX_NATIVE_CALL nav_build_path(AMX *amx, cell *params) {
    if (!g_nav.IsLoaded()) return 0;

    cell *c_start = MF_GetAmxAddr(amx, params[1]);
    cell *c_goal = MF_GetAmxAddr(amx, params[2]);
    cell *c_outId = MF_GetAmxAddr(amx, params[3]);
    int flags = (params[0] >= 4) ? params[4] : NAV_PATH_DEFAULT;

    Vector3 start(amx_ctof(c_start[0]), amx_ctof(c_start[1]), amx_ctof(c_start[2]));
    Vector3 goal(amx_ctof(c_goal[0]), amx_ctof(c_goal[1]), amx_ctof(c_goal[2]));

    NavPath path;
    bool success = NavPathFinder::BuildPath(g_nav.GetGrid(), start, goal, path, flags, g_bsp.IsLoaded() ? &g_bsp : nullptr);
    if (!success || !path.IsValid()) {
        *c_outId = 0;
        return 0;
    }

    int pathId = g_nextPathId++;
    g_activePaths[pathId] = std::move(path);
    *c_outId = pathId;
    return 1;
}

// native nav_path_get_segment_count(path_id);
static cell AMX_NATIVE_CALL nav_path_get_segment_count(AMX *amx, cell *params) {
    int id = params[1];
    auto it = g_activePaths.find(id);
    if (it == g_activePaths.end()) return 0;
    return static_cast<cell>(it->second.GetSegmentCount());
}

// native nav_path_get_point(path_id, segment_index, Float:pos[3]);
static cell AMX_NATIVE_CALL nav_path_get_point(AMX *amx, cell *params) {
    int id = params[1];
    size_t segIdx = static_cast<size_t>(params[2]);
    cell *c_pos = MF_GetAmxAddr(amx, params[3]);

    auto it = g_activePaths.find(id);
    if (it == g_activePaths.end()) return 0;

    const NavPathSegment* seg = it->second.GetSegment(segIdx);
    if (!seg) return 0;

    c_pos[0] = amx_ftoc(seg->pos.x);
    c_pos[1] = amx_ftoc(seg->pos.y);
    c_pos[2] = amx_ftoc(seg->pos.z);
    return 1;
}

// native nav_path_get_area(path_id, segment_index);
static cell AMX_NATIVE_CALL nav_path_get_area(AMX *amx, cell *params) {
    int id = params[1];
    size_t segIdx = static_cast<size_t>(params[2]);

    auto it = g_activePaths.find(id);
    if (it == g_activePaths.end()) return -1;

    const NavPathSegment* seg = it->second.GetSegment(segIdx);
    if (!seg || !seg->area) return -1;

    for (size_t i = 0; i < g_nav.GetAreaCount(); i++) {
        if (g_nav.GetArea(i) == seg->area) return static_cast<cell>(i);
    }
    return -1;
}

// native nav_path_get_how(path_id, segment_index);
static cell AMX_NATIVE_CALL nav_path_get_how(AMX *amx, cell *params) {
    int id = params[1];
    size_t segIdx = static_cast<size_t>(params[2]);

    auto it = g_activePaths.find(id);
    if (it == g_activePaths.end()) return 0;

    const NavPathSegment* seg = it->second.GetSegment(segIdx);
    return seg ? static_cast<cell>(seg->how) : 0;
}

// native Float:nav_path_get_length(path_id);
static cell AMX_NATIVE_CALL nav_path_get_length(AMX *amx, cell *params) {
    int id = params[1];
    auto it = g_activePaths.find(id);
    if (it == g_activePaths.end()) return amx_ftoc(0.0f);
    return amx_ftoc(it->second.GetLength());
}

// native nav_path_get_point_along(path_id, Float:dist, Float:pos[3]);
static cell AMX_NATIVE_CALL nav_path_get_point_along(AMX *amx, cell *params) {
    int id = params[1];
    float dist = amx_ctof(params[2]);
    cell *c_pos = MF_GetAmxAddr(amx, params[3]);

    auto it = g_activePaths.find(id);
    if (it == g_activePaths.end()) return 0;

    Vector3 pt;
    if (it->second.GetPointAlongPath(dist, &pt)) {
        c_pos[0] = amx_ftoc(pt.x);
        c_pos[1] = amx_ftoc(pt.y);
        c_pos[2] = amx_ftoc(pt.z);
        return 1;
    }
    return 0;
}

// native nav_path_destroy(path_id);
static cell AMX_NATIVE_CALL nav_path_destroy(AMX *amx, cell *params) {
    int id = params[1];
    return g_activePaths.erase(id) ? 1 : 0;
}

// native nav_path_clear_all();
static cell AMX_NATIVE_CALL nav_path_clear_all(AMX *amx, cell *params) {
    g_activePaths.clear();
    return 1;
}

static AMX_NATIVE_INFO g_navNatives[] = {
    {"nav_load",                  nav_load},
    {"nav_unload",                nav_unload},
    {"nav_is_loaded",             nav_is_loaded},
    {"nav_get_area_count",        nav_get_area_count},
    {"nav_get_area_by_id",        nav_get_area_by_id},
    {"nav_get_area_id",           nav_get_area_id},
    {"nav_get_nearest_area",      nav_get_nearest_area},
    {"nav_get_area_at_point",     nav_get_area_at_point},
    {"nav_get_area_center",       nav_get_area_center},
    {"nav_get_area_extent",       nav_get_area_extent},
    {"nav_get_area_z",            nav_get_area_z},
    {"nav_get_area_flags",        nav_get_area_flags},
    {"nav_is_point_in_area",      nav_is_point_in_area},
    {"nav_get_closest_point",     nav_get_closest_point},
    {"nav_get_distance_to_area",  nav_get_distance_to_area},
    {"nav_get_adjacent_count",    nav_get_adjacent_count},
    {"nav_get_adjacent_area",     nav_get_adjacent_area},
    {"nav_is_connected",          nav_is_connected},
    {"nav_get_place_name",        nav_get_place_name},
    {"nav_get_hiding_spot_count", nav_get_hiding_spot_count},
    {"nav_get_hiding_spot",       nav_get_hiding_spot},
    {"nav_get_ladder_count",      nav_get_ladder_count},
    {"nav_get_ladder_info",       nav_get_ladder_info},
    {"nav_build_path",            nav_build_path},
    {"nav_path_get_segment_count",nav_path_get_segment_count},
    {"nav_path_get_point",        nav_path_get_point},
    {"nav_path_get_area",         nav_path_get_area},
    {"nav_path_get_how",          nav_path_get_how},
    {"nav_path_get_length",       nav_path_get_length},
    {"nav_path_get_point_along",  nav_path_get_point_along},
    {"nav_path_destroy",          nav_path_destroy},
    {"nav_path_clear_all",        nav_path_clear_all},
    {nullptr, nullptr}
};

void RegisterNavNatives() {
    MF_AddNatives(g_navNatives);
}
