#include "amxx_api.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

// Helper to resolve map file path
static std::string FindMapBSP(const char* mapname) {
    char path[512];

    if (!mapname || mapname[0] == '\0') {
        return "";
    }

    // If mapname already ends with .bsp
    if (strstr(mapname, ".bsp") != nullptr) {
        snprintf(path, sizeof(path), "%s", mapname);
        FILE* f = fopen(path, "rb");
        if (f) { fclose(f); return std::string(path); }
    }

    // Try cstrike/maps/<mapname>.bsp
    snprintf(path, sizeof(path), "cstrike/maps/%s.bsp", mapname);
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Try czero/maps/<mapname>.bsp
    snprintf(path, sizeof(path), "czero/maps/%s.bsp", mapname);
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Try valve/maps/<mapname>.bsp
    snprintf(path, sizeof(path), "valve/maps/%s.bsp", mapname);
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Try maps/<mapname>.bsp
    snprintf(path, sizeof(path), "maps/%s.bsp", mapname);
    FILE* f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Fallback default
    snprintf(path, sizeof(path), "cstrike/maps/%s.bsp", mapname);
    return std::string(path);
}

// native bsp_load_map(const mapname[]);
static cell AMX_NATIVE_CALL bsp_load_map(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 1) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_load_map: Expected 1 parameter, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    int len = 0;
    char *mapname = MF_GetAmxString(amx, params[1], 0, &len);
    if (!mapname || len <= 0) {
        MF_LogError(amx, AMX_ERR_NATIVE, "[%s] bsp_load_map: Invalid map name string", MODULE_LOGTAG);
        return 0;
    }

    std::string bspPath = FindMapBSP(mapname);
    if (bspPath.empty() || !g_bsp.Load(bspPath)) {
        MF_Log("[%s] Failed to load BSP file '%s'.", MODULE_LOGTAG, bspPath.c_str());
        return 0;
    }

    MF_Log("[%s] BSP '%s' loaded successfully (%d entities, %d textures, %d leaves, %d models).",
           MODULE_LOGTAG, mapname, static_cast<int>(g_bsp.GetEntities().size()),
           g_bsp.GetTextureCount(), g_bsp.GetLeafCount(), g_bsp.GetModelCount());

    if (g_nav.IsLoaded()) {
        g_nav.BuildLadders(&g_bsp);
    }

    FireMapLoadedForward(true, g_nav.IsLoaded(), static_cast<int>(g_nav.GetAreaCount()));
    return 1;
}

// native bsp_is_loaded();
static cell AMX_NATIVE_CALL bsp_is_loaded(AMX *amx, cell *params) {
    return g_bsp.IsLoaded() ? 1 : 0;
}

// native bsp_get_leaf(const Float:origin[3]);
static cell AMX_NATIVE_CALL bsp_get_leaf(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 1) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_leaf: Expected 1 parameter, got %d", MODULE_LOGTAG, numParams);
        return -1;
    }

    if (!g_bsp.IsLoaded()) return -1;

    cell *ptr = MF_GetAmxAddr(amx, params[1]);
    if (!ptr) {
        MF_LogError(amx, AMX_ERR_NATIVE, "[%s] bsp_get_leaf: Invalid origin vector address", MODULE_LOGTAG);
        return -1;
    }

    Vector3 origin(amx_ctof(ptr[0]), amx_ctof(ptr[1]), amx_ctof(ptr[2]));
    return static_cast<cell>(g_bsp.GetLeafIDAtPoint(origin));
}

// native bsp_check_vis(leaf_a, leaf_b);
static cell AMX_NATIVE_CALL bsp_check_vis(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_check_vis: Expected 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return 1;
    }

    if (!g_bsp.IsLoaded()) return 1;
    return g_bsp.CheckVis(params[1], params[2]) ? 1 : 0;
}

// native bsp_check_pas(leaf_a, leaf_b);
static cell AMX_NATIVE_CALL bsp_check_pas(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_check_pas: Expected 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return 1;
    }

    if (!g_bsp.IsLoaded()) return 1;
    return g_bsp.CheckPAS(params[1], params[2]) ? 1 : 0;
}

// native bsp_is_point_visible(const Float:ptA[3], const Float:ptB[3]);
static cell AMX_NATIVE_CALL bsp_is_point_visible(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_is_point_visible: Expected 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return 1;
    }

    if (!g_bsp.IsLoaded()) return 1;

    cell *cA = MF_GetAmxAddr(amx, params[1]);
    cell *cB = MF_GetAmxAddr(amx, params[2]);
    if (!cA || !cB) return 1;

    Vector3 ptA(amx_ctof(cA[0]), amx_ctof(cA[1]), amx_ctof(cA[2]));
    Vector3 ptB(amx_ctof(cB[0]), amx_ctof(cB[1]), amx_ctof(cB[2]));
    return g_bsp.IsPointVisible(ptA, ptB) ? 1 : 0;
}

// native bsp_is_point_audible(const Float:ptA[3], const Float:ptB[3]);
static cell AMX_NATIVE_CALL bsp_is_point_audible(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_is_point_audible: Expected 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return 1;
    }

    if (!g_bsp.IsLoaded()) return 1;

    cell *cA = MF_GetAmxAddr(amx, params[1]);
    cell *cB = MF_GetAmxAddr(amx, params[2]);
    if (!cA || !cB) return 1;

    Vector3 ptA(amx_ctof(cA[0]), amx_ctof(cA[1]), amx_ctof(cA[2]));
    Vector3 ptB(amx_ctof(cB[0]), amx_ctof(cB[1]), amx_ctof(cB[2]));
    return g_bsp.IsPointAudible(ptA, ptB) ? 1 : 0;
}

// native bsp_get_pvs_size();
static cell AMX_NATIVE_CALL bsp_get_pvs_size(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;
    return static_cast<cell>(g_bsp.GetPVSByteSize());
}

// native bsp_get_leaf_pvs(leaf_index, buffer[], maxlen);
static cell AMX_NATIVE_CALL bsp_get_leaf_pvs(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_leaf_pvs: Expected 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int leafIndex = params[1];
    cell *buffer = MF_GetAmxAddr(amx, params[2]);
    int maxlen = params[3];
    if (!buffer || maxlen <= 0) return 0;

    int pvsSize = g_bsp.GetPVSByteSize();
    int copyBytes = std::min(maxlen, pvsSize);

    std::vector<uint8_t> pvs(pvsSize, 0);
    if (!g_bsp.DecompressPVS(leafIndex, pvs.data(), pvsSize)) {
        return 0;
    }

    for (int i = 0; i < copyBytes; ++i) {
        buffer[i] = static_cast<cell>(pvs[i]);
    }
    return static_cast<cell>(copyBytes);
}

// native bsp_get_leaf_pas(leaf_index, buffer[], maxlen);
static cell AMX_NATIVE_CALL bsp_get_leaf_pas(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_leaf_pas: Expected 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int leafIndex = params[1];
    cell *buffer = MF_GetAmxAddr(amx, params[2]);
    int maxlen = params[3];
    if (!buffer || maxlen <= 0) return 0;

    int pvsSize = g_bsp.GetPVSByteSize();
    int copyBytes = std::min(maxlen, pvsSize);

    std::vector<uint8_t> pas(pvsSize, 0);
    if (!g_bsp.DecompressPAS(leafIndex, pas.data(), pvsSize)) {
        return 0;
    }

    for (int i = 0; i < copyBytes; ++i) {
        buffer[i] = static_cast<cell>(pas[i]);
    }
    return static_cast<cell>(copyBytes);
}

// native bsp_get_visible_leaf_count(leaf_index);
static cell AMX_NATIVE_CALL bsp_get_visible_leaf_count(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 1) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_visible_leaf_count: Expected 1 parameter, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;
    return static_cast<cell>(g_bsp.GetVisibleLeafCount(params[1]));
}

// native bsp_trace_line(const Float:start[3], const Float:end[3], Float:hitPos[3] = Float:{0.0,...}, Float:hitNormal[3] = Float:{0.0,...});
static cell AMX_NATIVE_CALL bsp_trace_line(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_trace_line: Expected at least 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    if (!c_s || !c_e) {
        MF_LogError(amx, AMX_ERR_NATIVE, "[%s] bsp_trace_line: Invalid start/end vector address", MODULE_LOGTAG);
        return 0;
    }

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    BSPTraceResult tr;
    bool hit = g_bsp.TraceWorld(start, end, HULL_POINT, &tr);

    if (numParams >= 3 && params[3] != 0) {
        cell *c_hit = MF_GetAmxAddr(amx, params[3]);
        if (c_hit) {
            c_hit[0] = amx_ftoc(tr.endpos.x);
            c_hit[1] = amx_ftoc(tr.endpos.y);
            c_hit[2] = amx_ftoc(tr.endpos.z);
        }
    }
    if (numParams >= 4 && params[4] != 0) {
        cell *c_norm = MF_GetAmxAddr(amx, params[4]);
        if (c_norm) {
            c_norm[0] = amx_ftoc(tr.planeNormal.x);
            c_norm[1] = amx_ftoc(tr.planeNormal.y);
            c_norm[2] = amx_ftoc(tr.planeNormal.z);
        }
    }

    return hit ? 1 : 0;
}

// native bsp_trace_line_ex(const Float:start[3], const Float:end[3], Float:hitPos[3], Float:hitNormal[3], texture[], maxlen);
static cell AMX_NATIVE_CALL bsp_trace_line_ex(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 6) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_trace_line_ex: Expected 6 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    cell *c_hit = MF_GetAmxAddr(amx, params[3]);
    cell *c_norm = MF_GetAmxAddr(amx, params[4]);
    int maxlen = params[6];
    if (!c_s || !c_e) return 0;

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    BSPTraceResult tr;
    bool hit = g_bsp.TraceWorld(start, end, HULL_POINT, &tr);

    if (c_hit) {
        c_hit[0] = amx_ftoc(tr.endpos.x);
        c_hit[1] = amx_ftoc(tr.endpos.y);
        c_hit[2] = amx_ftoc(tr.endpos.z);
    }
    if (c_norm) {
        c_norm[0] = amx_ftoc(tr.planeNormal.x);
        c_norm[1] = amx_ftoc(tr.planeNormal.y);
        c_norm[2] = amx_ftoc(tr.planeNormal.z);
    }
    if (maxlen > 0) {
        MF_SetAmxString(amx, params[5], tr.hitTexture, maxlen);
    }

    return hit ? 1 : 0;
}

// native bsp_trace_hull(const Float:start[3], const Float:end[3], hull_type, Float:hitPos[3] = Float:{0.0,...}, Float:hitNormal[3] = Float:{0.0,...});
static cell AMX_NATIVE_CALL bsp_trace_hull(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_trace_hull: Expected at least 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int hullType = params[3];
    if (hullType < 0 || hullType > 3) {
        MF_LogError(amx, AMX_ERR_NATIVE, "[%s] bsp_trace_hull: Invalid hull type %d (0-3)", MODULE_LOGTAG, hullType);
        return 0;
    }

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    if (!c_s || !c_e) {
        MF_LogError(amx, AMX_ERR_NATIVE, "[%s] bsp_trace_hull: Invalid start/end vector address", MODULE_LOGTAG);
        return 0;
    }

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    BSPTraceResult tr;
    bool hit = g_bsp.TraceWorld(start, end, hullType, &tr);

    if (numParams >= 4 && params[4] != 0) {
        cell *c_hit = MF_GetAmxAddr(amx, params[4]);
        if (c_hit) {
            c_hit[0] = amx_ftoc(tr.endpos.x);
            c_hit[1] = amx_ftoc(tr.endpos.y);
            c_hit[2] = amx_ftoc(tr.endpos.z);
        }
    }
    if (numParams >= 5 && params[5] != 0) {
        cell *c_norm = MF_GetAmxAddr(amx, params[5]);
        if (c_norm) {
            c_norm[0] = amx_ftoc(tr.planeNormal.x);
            c_norm[1] = amx_ftoc(tr.planeNormal.y);
            c_norm[2] = amx_ftoc(tr.planeNormal.z);
        }
    }

    return hit ? 1 : 0;
}

// native bsp_trace_hull_ex(const Float:start[3], const Float:end[3], hull_type, Float:hitPos[3], Float:hitNormal[3], texture[], maxlen);
static cell AMX_NATIVE_CALL bsp_trace_hull_ex(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 7) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_trace_hull_ex: Expected 7 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int hullType = params[3];
    if (hullType < 0 || hullType > 3) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    cell *c_hit = MF_GetAmxAddr(amx, params[4]);
    cell *c_norm = MF_GetAmxAddr(amx, params[5]);
    int maxlen = params[7];
    if (!c_s || !c_e) return 0;

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    BSPTraceResult tr;
    bool hit = g_bsp.TraceWorld(start, end, hullType, &tr);

    if (c_hit) {
        c_hit[0] = amx_ftoc(tr.endpos.x);
        c_hit[1] = amx_ftoc(tr.endpos.y);
        c_hit[2] = amx_ftoc(tr.endpos.z);
    }
    if (c_norm) {
        c_norm[0] = amx_ftoc(tr.planeNormal.x);
        c_norm[1] = amx_ftoc(tr.planeNormal.y);
        c_norm[2] = amx_ftoc(tr.planeNormal.z);
    }
    if (maxlen > 0) {
        MF_SetAmxString(amx, params[6], tr.hitTexture, maxlen);
    }

    return hit ? 1 : 0;
}

// native bsp_trace_texture(const Float:start[3], const Float:end[3], texture[], maxlen);
static cell AMX_NATIVE_CALL bsp_trace_texture(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 4) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_trace_texture: Expected 4 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    int maxlen = params[4];
    if (!c_s || !c_e || maxlen <= 0) return 0;

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    char texName[64] = {0};
    if (g_bsp.TraceTexture(start, end, texName, sizeof(texName))) {
        MF_SetAmxString(amx, params[3], texName, maxlen);
        return 1;
    }

    MF_SetAmxString(amx, params[3], "", maxlen);
    return 0;
}

// native bsp_trace_wall(const Float:start[3], const Float:end[3], hull_type);
static cell AMX_NATIVE_CALL bsp_trace_wall(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_trace_wall: Expected 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int hullType = params[3];
    if (hullType < 0 || hullType > 3) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    if (!c_s || !c_e) return 0;

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    return g_bsp.TraceWorld(start, end, hullType) ? 1 : 0;
}

// native bsp_trace_model(model_idx, const Float:start[3], const Float:end[3], hull_type);
static cell AMX_NATIVE_CALL bsp_trace_model(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 4) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_trace_model: Expected 4 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int idx = params[1];
    if (idx < 0 || idx >= g_bsp.GetModelCount()) {
        MF_LogError(amx, AMX_ERR_NATIVE, "[%s] bsp_trace_model: Model index %d out of bounds (0-%d)",
                    MODULE_LOGTAG, idx, g_bsp.GetModelCount() - 1);
        return 0;
    }

    int hullType = params[4];
    if (hullType < 0 || hullType > 3) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[2]);
    cell *c_e = MF_GetAmxAddr(amx, params[3]);
    if (!c_s || !c_e) return 0;

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    return g_bsp.TraceModel(idx, start, end, hullType) ? 1 : 0;
}

// native bsp_get_ground(const Float:start[3], Float:out[3], Float:max_drop = 2000.0);
static cell AMX_NATIVE_CALL bsp_get_ground(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_ground: Expected at least 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_out = MF_GetAmxAddr(amx, params[2]);
    if (!c_s || !c_out) return 0;

    float maxDrop = (numParams >= 3) ? amx_ctof(params[3]) : 2000.0f;
    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 ground;

    if (g_bsp.GetGround(start, &ground, maxDrop)) {
        c_out[0] = amx_ftoc(ground.x);
        c_out[1] = amx_ftoc(ground.y);
        c_out[2] = amx_ftoc(ground.z);
        return 1;
    }
    return 0;
}

// native bsp_get_contents(const Float:origin[3]);
static cell AMX_NATIVE_CALL bsp_get_contents(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 1) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_contents: Expected 1 parameter, got %d", MODULE_LOGTAG, numParams);
        return CONTENTS_EMPTY;
    }

    if (!g_bsp.IsLoaded()) return CONTENTS_EMPTY;

    cell *ptr = MF_GetAmxAddr(amx, params[1]);
    if (!ptr) return CONTENTS_EMPTY;

    Vector3 origin(amx_ctof(ptr[0]), amx_ctof(ptr[1]), amx_ctof(ptr[2]));
    return static_cast<cell>(g_bsp.GetContents(origin));
}

// native bsp_get_world_bounds(Float:mins[3], Float:maxs[3]);
static cell AMX_NATIVE_CALL bsp_get_world_bounds(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_world_bounds: Expected 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    cell *c_mins = MF_GetAmxAddr(amx, params[1]);
    cell *c_maxs = MF_GetAmxAddr(amx, params[2]);
    if (!c_mins || !c_maxs) return 0;

    Vector3 mins, maxs;
    if (g_bsp.GetWorldBounds(mins, maxs)) {
        c_mins[0] = amx_ftoc(mins.x);
        c_mins[1] = amx_ftoc(mins.y);
        c_mins[2] = amx_ftoc(mins.z);

        c_maxs[0] = amx_ftoc(maxs.x);
        c_maxs[1] = amx_ftoc(maxs.y);
        c_maxs[2] = amx_ftoc(maxs.z);
        return 1;
    }
    return 0;
}

// native bsp_get_leaf_bounds(leaf_index, Float:mins[3], Float:maxs[3]);
static cell AMX_NATIVE_CALL bsp_get_leaf_bounds(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_leaf_bounds: Expected 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int leafIdx = params[1];
    cell *c_mins = MF_GetAmxAddr(amx, params[2]);
    cell *c_maxs = MF_GetAmxAddr(amx, params[3]);
    if (!c_mins || !c_maxs) return 0;

    Vector3 mins, maxs;
    if (g_bsp.GetLeafBounds(leafIdx, mins, maxs)) {
        c_mins[0] = amx_ftoc(mins.x);
        c_mins[1] = amx_ftoc(mins.y);
        c_mins[2] = amx_ftoc(mins.z);

        c_maxs[0] = amx_ftoc(maxs.x);
        c_maxs[1] = amx_ftoc(maxs.y);
        c_maxs[2] = amx_ftoc(maxs.z);
        return 1;
    }
    return 0;
}

// native bsp_get_leaf_contents(leaf_index);
static cell AMX_NATIVE_CALL bsp_get_leaf_contents(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 1) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_leaf_contents: Expected 1 parameter, got %d", MODULE_LOGTAG, numParams);
        return CONTENTS_EMPTY;
    }

    if (!g_bsp.IsLoaded()) return CONTENTS_EMPTY;
    return static_cast<cell>(g_bsp.GetLeafContents(params[1]));
}

// native bsp_get_leaf_ambient(leaf_index, channel);
static cell AMX_NATIVE_CALL bsp_get_leaf_ambient(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_leaf_ambient: Expected 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;
    return static_cast<cell>(g_bsp.GetLeafAmbient(params[1], params[2]));
}

// native bsp_get_leaf_count();
static cell AMX_NATIVE_CALL bsp_get_leaf_count(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;
    return static_cast<cell>(g_bsp.GetLeafCount());
}

// native bsp_get_node_count();
static cell AMX_NATIVE_CALL bsp_get_node_count(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;
    return static_cast<cell>(g_bsp.GetNodeCount());
}

// native bsp_get_plane_count();
static cell AMX_NATIVE_CALL bsp_get_plane_count(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;
    return static_cast<cell>(g_bsp.GetPlaneCount());
}

// native bsp_get_face_count();
static cell AMX_NATIVE_CALL bsp_get_face_count(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;
    return static_cast<cell>(g_bsp.GetFaceCount());
}

// native bsp_get_plane(plane_index, Float:normal[3], &Float:dist, &type);
static cell AMX_NATIVE_CALL bsp_get_plane(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 4) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_plane: Expected 4 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int idx = params[1];
    const dplane_t* plane = g_bsp.GetPlane(idx);
    if (!plane) return 0;

    cell *c_norm = MF_GetAmxAddr(amx, params[2]);
    cell *c_dist = MF_GetAmxAddr(amx, params[3]);
    cell *c_type = MF_GetAmxAddr(amx, params[4]);

    if (c_norm) {
        c_norm[0] = amx_ftoc(plane->normal.x);
        c_norm[1] = amx_ftoc(plane->normal.y);
        c_norm[2] = amx_ftoc(plane->normal.z);
    }
    if (c_dist) {
        *c_dist = amx_ftoc(plane->dist);
    }
    if (c_type) {
        *c_type = static_cast<cell>(plane->type);
    }
    return 1;
}

// native bsp_get_texture_count();
static cell AMX_NATIVE_CALL bsp_get_texture_count(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;
    return static_cast<cell>(g_bsp.GetTextureCount());
}

// native bsp_get_texture_name(texture_index, name[], maxlen);
static cell AMX_NATIVE_CALL bsp_get_texture_name(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_texture_name: Expected 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int idx = params[1];
    int maxlen = params[3];
    if (idx < 0 || idx >= g_bsp.GetTextureCount() || maxlen <= 0) return 0;

    const char* name = g_bsp.GetTextureName(idx);
    return static_cast<cell>(MF_SetAmxString(amx, params[2], name, maxlen));
}

// native bsp_get_texture_size(texture_index, &width, &height);
static cell AMX_NATIVE_CALL bsp_get_texture_size(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_texture_size: Expected 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int idx = params[1];
    cell *c_w = MF_GetAmxAddr(amx, params[2]);
    cell *c_h = MF_GetAmxAddr(amx, params[3]);

    int w = 0, h = 0;
    if (g_bsp.GetTextureDimensions(idx, w, h)) {
        if (c_w) *c_w = static_cast<cell>(w);
        if (c_h) *c_h = static_cast<cell>(h);
        return 1;
    }
    return 0;
}

// native bsp_find_texture(const name[]);
static cell AMX_NATIVE_CALL bsp_find_texture(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 1) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_find_texture: Expected 1 parameter, got %d", MODULE_LOGTAG, numParams);
        return -1;
    }

    if (!g_bsp.IsLoaded()) return -1;

    int len = 0;
    char *name = MF_GetAmxString(amx, params[1], 0, &len);
    if (!name || len <= 0) return -1;

    return static_cast<cell>(g_bsp.FindTexture(name));
}

// native bsp_get_entity_count(const classname[] = "");
static cell AMX_NATIVE_CALL bsp_get_entity_count(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;
    const int numParams = static_cast<int>(params[0] / sizeof(cell));

    if (numParams >= 1) {
        int len = 0;
        char *targetClass = MF_GetAmxString(amx, params[1], 0, &len);
        if (targetClass && len > 0) {
            return static_cast<cell>(g_bsp.FindEntities(targetClass).size());
        }
    }
    return static_cast<cell>(g_bsp.GetEntities().size());
}

// native bsp_get_entity_origin(const classname[], target_index, Float:output[3]);
static cell AMX_NATIVE_CALL bsp_get_entity_origin(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_entity_origin: Expected 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int len = 0;
    char *targetClass = MF_GetAmxString(amx, params[1], 0, &len);
    int targetIndex = params[2];
    cell *output = MF_GetAmxAddr(amx, params[3]);
    if (!targetClass || !output) return 0;

    auto ents = g_bsp.FindEntities(targetClass);
    if (targetIndex >= 0 && targetIndex < static_cast<int>(ents.size())) {
        Vector3 origin;
        if (ents[targetIndex]->GetOrigin(origin)) {
            output[0] = amx_ftoc(origin.x);
            output[1] = amx_ftoc(origin.y);
            output[2] = amx_ftoc(origin.z);
            return 1;
        }
    }
    return 0;
}

// native bsp_get_entity_key(entity_index, const key[], value[], maxlen);
static cell AMX_NATIVE_CALL bsp_get_entity_key(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 4) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_entity_key: Expected 4 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int entIdx = params[1];
    int len = 0;
    char *key = MF_GetAmxString(amx, params[2], 0, &len);
    int maxlen = params[4];
    if (!key || len <= 0 || maxlen <= 0) return 0;

    const BSPEntity* ent = g_bsp.GetEntity(entIdx);
    if (!ent) return 0;

    std::string val = ent->GetString(key);
    return static_cast<cell>(MF_SetAmxString(amx, params[3], val.c_str(), maxlen));
}

// native bsp_find_entity_by_key(const key[], const value[], start_index = 0);
static cell AMX_NATIVE_CALL bsp_find_entity_by_key(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_find_entity_by_key: Expected at least 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return -1;
    }

    if (!g_bsp.IsLoaded()) return -1;

    int lenK = 0, lenV = 0;
    char *key = MF_GetAmxString(amx, params[1], 0, &lenK);
    char *val = MF_GetAmxString(amx, params[2], 0, &lenV);
    int startIdx = (numParams >= 3) ? params[3] : 0;
    if (!key || lenK <= 0 || !val) return -1;

    int totalEnts = g_bsp.GetEntityCount();
    for (int i = std::max(0, startIdx); i < totalEnts; ++i) {
        const BSPEntity* ent = g_bsp.GetEntity(i);
        if (ent) {
            if (strcmp(key, "classname") == 0 && ent->classname == val) {
                return static_cast<cell>(i);
            }
            if (ent->GetString(key) == val) {
                return static_cast<cell>(i);
            }
        }
    }
    return -1;
}

// native bsp_get_brush_model(const classname[], target_index, Float:out_mins[3], Float:out_maxs[3]);
static cell AMX_NATIVE_CALL bsp_get_brush_model(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 4) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_brush_model: Expected 4 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int len = 0;
    char *targetClass = MF_GetAmxString(amx, params[1], 0, &len);
    int targetIndex = params[2];
    cell *outMins = MF_GetAmxAddr(amx, params[3]);
    cell *outMaxs = MF_GetAmxAddr(amx, params[4]);
    if (!targetClass || !outMins || !outMaxs) return 0;

    auto ents = g_bsp.FindEntities(targetClass);
    if (targetIndex >= 0 && targetIndex < static_cast<int>(ents.size())) {
        std::string modelStr = ents[targetIndex]->GetString("model");
        if (!modelStr.empty() && modelStr[0] == '*') {
            int modelIdx = std::atoi(modelStr.c_str() + 1);
            const dmodel_t* mod = g_bsp.GetModel(modelIdx);
            if (mod) {
                outMins[0] = amx_ftoc(mod->mins.x);
                outMins[1] = amx_ftoc(mod->mins.y);
                outMins[2] = amx_ftoc(mod->mins.z);

                outMaxs[0] = amx_ftoc(mod->maxs.x);
                outMaxs[1] = amx_ftoc(mod->maxs.y);
                outMaxs[2] = amx_ftoc(mod->maxs.z);
                return 1;
            }
        }
    }
    return 0;
}

// native bsp_get_model_count();
static cell AMX_NATIVE_CALL bsp_get_model_count(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;
    return static_cast<cell>(g_bsp.GetModelCount());
}

// native bsp_get_model_bounds(model_index, Float:mins[3], Float:maxs[3]);
static cell AMX_NATIVE_CALL bsp_get_model_bounds(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_model_bounds: Expected 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int idx = params[1];
    const dmodel_t* mod = g_bsp.GetModel(idx);
    if (!mod) return 0;

    cell *c_mins = MF_GetAmxAddr(amx, params[2]);
    cell *c_maxs = MF_GetAmxAddr(amx, params[3]);
    if (!c_mins || !c_maxs) return 0;

    c_mins[0] = amx_ftoc(mod->mins.x);
    c_mins[1] = amx_ftoc(mod->mins.y);
    c_mins[2] = amx_ftoc(mod->mins.z);

    c_maxs[0] = amx_ftoc(mod->maxs.x);
    c_maxs[1] = amx_ftoc(mod->maxs.y);
    c_maxs[2] = amx_ftoc(mod->maxs.z);
    return 1;
}

// native bsp_get_model_origin(model_index, Float:origin[3]);
static cell AMX_NATIVE_CALL bsp_get_model_origin(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 2) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_model_origin: Expected 2 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int idx = params[1];
    const dmodel_t* mod = g_bsp.GetModel(idx);
    if (!mod) return 0;

    cell *c_orig = MF_GetAmxAddr(amx, params[2]);
    if (!c_orig) return 0;

    c_orig[0] = amx_ftoc(mod->origin.x);
    c_orig[1] = amx_ftoc(mod->origin.y);
    c_orig[2] = amx_ftoc(mod->origin.z);
    return 1;
}

// native bsp_get_entities(const classname[], Float:output[], max_found);
static cell AMX_NATIVE_CALL bsp_get_entities(AMX *amx, cell *params) {
    const int numParams = static_cast<int>(params[0] / sizeof(cell));
    if (numParams < 3) {
        MF_LogError(amx, AMX_ERR_PARAMS, "[%s] bsp_get_entities: Expected 3 parameters, got %d", MODULE_LOGTAG, numParams);
        return 0;
    }

    if (!g_bsp.IsLoaded()) return 0;

    int len = 0;
    char *targetClass = MF_GetAmxString(amx, params[1], 0, &len);
    cell *output = MF_GetAmxAddr(amx, params[2]);
    int maxFound = params[3];
    if (!targetClass || !output || maxFound <= 0) return 0;

    int count = 0;
    auto ents = g_bsp.FindEntities(targetClass);
    for (const auto* ent : ents) {
        if (count >= maxFound) break;
        Vector3 origin;
        if (ent->GetOrigin(origin)) {
            output[count * 3 + 0] = amx_ftoc(origin.x);
            output[count * 3 + 1] = amx_ftoc(origin.y);
            output[count * 3 + 2] = amx_ftoc(origin.z);
            count++;
        }
    }
    return static_cast<cell>(count);
}

static AMX_NATIVE_INFO g_bspNatives[] = {
    // Map loading & status
    {"bsp_load_map",              bsp_load_map},
    {"bsp_is_loaded",             bsp_is_loaded},

    // Visibility & Audibility (PVS & PAS)
    {"bsp_get_leaf",              bsp_get_leaf},
    {"bsp_check_vis",             bsp_check_vis},
    {"bsp_check_pas",             bsp_check_pas},
    {"bsp_is_point_visible",      bsp_is_point_visible},
    {"bsp_is_point_audible",      bsp_is_point_audible},
    {"bsp_get_pvs_size",          bsp_get_pvs_size},
    {"bsp_get_leaf_pvs",          bsp_get_leaf_pvs},
    {"bsp_get_leaf_pas",          bsp_get_leaf_pas},
    {"bsp_get_visible_leaf_count",bsp_get_visible_leaf_count},

    // Collision & Tracing
    {"bsp_trace_line",            bsp_trace_line},
    {"bsp_trace_line_ex",         bsp_trace_line_ex},
    {"bsp_trace_hull",            bsp_trace_hull},
    {"bsp_trace_hull_ex",         bsp_trace_hull_ex},
    {"bsp_trace_texture",         bsp_trace_texture},
    {"bsp_trace_wall",            bsp_trace_wall},
    {"bsp_trace_model",           bsp_trace_model},
    {"bsp_get_ground",            bsp_get_ground},
    {"bsp_get_contents",          bsp_get_contents},

    // World & Leaves & Geometry
    {"bsp_get_world_bounds",      bsp_get_world_bounds},
    {"bsp_get_leaf_bounds",       bsp_get_leaf_bounds},
    {"bsp_get_leaf_contents",     bsp_get_leaf_contents},
    {"bsp_get_leaf_ambient",      bsp_get_leaf_ambient},
    {"bsp_get_leaf_count",        bsp_get_leaf_count},
    {"bsp_get_node_count",        bsp_get_node_count},
    {"bsp_get_plane_count",       bsp_get_plane_count},
    {"bsp_get_face_count",        bsp_get_face_count},
    {"bsp_get_plane",             bsp_get_plane},

    // Textures & Surfaces
    {"bsp_get_texture_count",     bsp_get_texture_count},
    {"bsp_get_texture_name",      bsp_get_texture_name},
    {"bsp_get_texture_size",      bsp_get_texture_size},
    {"bsp_find_texture",          bsp_find_texture},

    // Entities
    {"bsp_get_entity_count",      bsp_get_entity_count},
    {"bsp_get_entity_origin",     bsp_get_entity_origin},
    {"bsp_get_entity_key",        bsp_get_entity_key},
    {"bsp_find_entity_by_key",    bsp_find_entity_by_key},
    {"bsp_get_brush_model",       bsp_get_brush_model},
    {"bsp_get_entities",          bsp_get_entities},

    // Submodels
    {"bsp_get_model_count",       bsp_get_model_count},
    {"bsp_get_model_bounds",      bsp_get_model_bounds},
    {"bsp_get_model_origin",      bsp_get_model_origin},

    // Backward compatibility aliases
    {"nav_get_entities",          bsp_get_entities},
    {"nav_get_ground",            bsp_get_ground},
    {"nav_trace_wall",            bsp_trace_wall},
    {"nav_trace_model",           bsp_trace_model},
    {"nav_get_contents",          bsp_get_contents},

    {nullptr, nullptr}
};

void RegisterBSPNatives() {
    MF_AddNatives(g_bspNatives);
}
