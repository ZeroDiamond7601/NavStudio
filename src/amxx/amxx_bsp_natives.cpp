#include "amxx_api.h"
#include <cstdio>
#include <cstring>
#include <string>

// Helper to resolve map file path
static std::string FindMapBSP(const char* mapname) {
    char path[512];

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
    f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Try valve/maps/<mapname>.bsp
    snprintf(path, sizeof(path), "valve/maps/%s.bsp", mapname);
    f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Try maps/<mapname>.bsp
    snprintf(path, sizeof(path), "maps/%s.bsp", mapname);
    f = fopen(path, "rb");
    if (f) { fclose(f); return std::string(path); }

    // Fallback: cstrike/maps/<mapname>.bsp
    snprintf(path, sizeof(path), "cstrike/maps/%s.bsp", mapname);
    return std::string(path);
}

// native bsp_load_map(const mapname[]);
static cell AMX_NATIVE_CALL bsp_load_map(AMX *amx, cell *params) {
    int len = 0;
    char *mapname = MF_GetAmxString(amx, params[1], 1, &len);
    if (!mapname || len <= 0) return 0;

    std::string bspPath = FindMapBSP(mapname);
    if (!g_bsp.Load(bspPath)) {
        MF_Log("[%s] Failed to load BSP file '%s'.", MODULE_LOGTAG, bspPath.c_str());
        return 0;
    }

    MF_Log("[%s] BSP '%s' loaded successfully (%d entities, %d models).",
           MODULE_LOGTAG, mapname, static_cast<int>(g_bsp.GetEntities().size()), g_bsp.GetModelCount());
    return 1;
}

// native bsp_is_loaded();
static cell AMX_NATIVE_CALL bsp_is_loaded(AMX *amx, cell *params) {
    return g_bsp.IsLoaded() ? 1 : 0;
}

// native bsp_get_leaf(const Float:origin[3]);
static cell AMX_NATIVE_CALL bsp_get_leaf(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return -1;
    cell *ptr = MF_GetAmxAddr(amx, params[1]);
    Vector3 origin(amx_ctof(ptr[0]), amx_ctof(ptr[1]), amx_ctof(ptr[2]));
    return static_cast<cell>(g_bsp.GetLeafIDAtPoint(origin));
}

// native bsp_check_vis(leaf_a, leaf_b);
static cell AMX_NATIVE_CALL bsp_check_vis(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 1;
    return g_bsp.CheckVis(params[1], params[2]) ? 1 : 0;
}

// native bsp_check_pas(leaf_a, leaf_b);
static cell AMX_NATIVE_CALL bsp_check_pas(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 1;
    return g_bsp.CheckPAS(params[1], params[2]) ? 1 : 0;
}

// native bsp_trace_line(const Float:start[3], const Float:end[3], Float:hitPos[3] = {0.0, ...}, Float:hitNormal[3] = {0.0, ...});
static cell AMX_NATIVE_CALL bsp_trace_line(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    BSPTraceResult tr;
    bool hit = g_bsp.TraceWorld(start, end, HULL_POINT, &tr);

    if (params[0] >= 3 && params[3] != 0) {
        cell *c_hit = MF_GetAmxAddr(amx, params[3]);
        c_hit[0] = amx_ftoc(tr.endpos.x);
        c_hit[1] = amx_ftoc(tr.endpos.y);
        c_hit[2] = amx_ftoc(tr.endpos.z);
    }
    if (params[0] >= 4 && params[4] != 0) {
        cell *c_norm = MF_GetAmxAddr(amx, params[4]);
        c_norm[0] = amx_ftoc(tr.planeNormal.x);
        c_norm[1] = amx_ftoc(tr.planeNormal.y);
        c_norm[2] = amx_ftoc(tr.planeNormal.z);
    }

    return hit ? 1 : 0;
}

// native bsp_trace_hull(const Float:start[3], const Float:end[3], hull_type, Float:hitPos[3] = {0.0, ...}, Float:hitNormal[3] = {0.0, ...});
static cell AMX_NATIVE_CALL bsp_trace_hull(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    int hullType = params[3];
    if (hullType < 0 || hullType > 3) return 0;

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    BSPTraceResult tr;
    bool hit = g_bsp.TraceWorld(start, end, hullType, &tr);

    if (params[0] >= 4 && params[4] != 0) {
        cell *c_hit = MF_GetAmxAddr(amx, params[4]);
        c_hit[0] = amx_ftoc(tr.endpos.x);
        c_hit[1] = amx_ftoc(tr.endpos.y);
        c_hit[2] = amx_ftoc(tr.endpos.z);
    }
    if (params[0] >= 5 && params[5] != 0) {
        cell *c_norm = MF_GetAmxAddr(amx, params[5]);
        c_norm[0] = amx_ftoc(tr.planeNormal.x);
        c_norm[1] = amx_ftoc(tr.planeNormal.y);
        c_norm[2] = amx_ftoc(tr.planeNormal.z);
    }

    return hit ? 1 : 0;
}

// native bsp_trace_wall(const Float:start[3], const Float:end[3], hull_type);
static cell AMX_NATIVE_CALL bsp_trace_wall(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_e = MF_GetAmxAddr(amx, params[2]);
    int hullType = params[3];
    if (hullType < 0 || hullType > 3) return 0;

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    return g_bsp.TraceWorld(start, end, hullType) ? 1 : 0;
}

// native bsp_trace_model(model_idx, const Float:start[3], const Float:end[3], hull_type);
static cell AMX_NATIVE_CALL bsp_trace_model(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;

    int idx = params[1];
    cell *c_s = MF_GetAmxAddr(amx, params[2]);
    cell *c_e = MF_GetAmxAddr(amx, params[3]);
    int hullType = params[4];
    if (hullType < 0 || hullType > 3) return 0;

    Vector3 start(amx_ctof(c_s[0]), amx_ctof(c_s[1]), amx_ctof(c_s[2]));
    Vector3 end(amx_ctof(c_e[0]), amx_ctof(c_e[1]), amx_ctof(c_e[2]));

    return g_bsp.TraceModel(idx, start, end, hullType) ? 1 : 0;
}

// native bsp_get_ground(const Float:start[3], Float:out[3], Float:max_drop = 2000.0);
static cell AMX_NATIVE_CALL bsp_get_ground(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;

    cell *c_s = MF_GetAmxAddr(amx, params[1]);
    cell *c_out = MF_GetAmxAddr(amx, params[2]);
    float maxDrop = (params[0] >= 3) ? amx_ctof(params[3]) : 2000.0f;

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
    if (!g_bsp.IsLoaded()) return CONTENTS_EMPTY;

    cell *ptr = MF_GetAmxAddr(amx, params[1]);
    Vector3 origin(amx_ctof(ptr[0]), amx_ctof(ptr[1]), amx_ctof(ptr[2]));
    return static_cast<cell>(g_bsp.GetContents(origin));
}

// native bsp_get_entity_count(const classname[] = "");
static cell AMX_NATIVE_CALL bsp_get_entity_count(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;

    if (params[0] >= 1) {
        int len = 0;
        char *targetClass = MF_GetAmxString(amx, params[1], 1, &len);
        if (targetClass && len > 0) {
            return static_cast<cell>(g_bsp.FindEntities(targetClass).size());
        }
    }
    return static_cast<cell>(g_bsp.GetEntities().size());
}

// native bsp_get_entity_origin(const classname[], target_index, Float:output[3]);
static cell AMX_NATIVE_CALL bsp_get_entity_origin(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;

    int len = 0;
    char *targetClass = MF_GetAmxString(amx, params[1], 1, &len);
    int targetIndex = params[2];
    cell *output = MF_GetAmxAddr(amx, params[3]);

    auto ents = g_bsp.FindEntities(targetClass ? targetClass : "");
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

// native bsp_get_brush_model(const classname[], target_index, Float:out_mins[3], Float:out_maxs[3]);
static cell AMX_NATIVE_CALL bsp_get_brush_model(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;

    int len = 0;
    char *targetClass = MF_GetAmxString(amx, params[1], 1, &len);
    int targetIndex = params[2];
    cell *outMins = MF_GetAmxAddr(amx, params[3]);
    cell *outMaxs = MF_GetAmxAddr(amx, params[4]);

    auto ents = g_bsp.FindEntities(targetClass ? targetClass : "");
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

// native bsp_get_entities(const classname[], Float:output[], max_found);
static cell AMX_NATIVE_CALL bsp_get_entities(AMX *amx, cell *params) {
    if (!g_bsp.IsLoaded()) return 0;

    int len = 0;
    char *targetClass = MF_GetAmxString(amx, params[1], 1, &len);
    cell *output = MF_GetAmxAddr(amx, params[2]);
    int maxFound = params[3];
    int count = 0;

    auto ents = g_bsp.FindEntities(targetClass ? targetClass : "");
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
    {"bsp_load_map",          bsp_load_map},
    {"bsp_is_loaded",         bsp_is_loaded},
    {"bsp_get_leaf",          bsp_get_leaf},
    {"bsp_check_vis",         bsp_check_vis},
    {"bsp_check_pas",         bsp_check_pas},
    {"bsp_trace_line",        bsp_trace_line},
    {"bsp_trace_hull",        bsp_trace_hull},
    {"bsp_trace_wall",        bsp_trace_wall},
    {"bsp_trace_model",       bsp_trace_model},
    {"bsp_get_ground",        bsp_get_ground},
    {"bsp_get_contents",      bsp_get_contents},
    {"bsp_get_entity_count",  bsp_get_entity_count},
    {"bsp_get_entity_origin", bsp_get_entity_origin},
    {"bsp_get_brush_model",   bsp_get_brush_model},
    {"bsp_get_entities",      bsp_get_entities},

    // bsp-core backwards compatibility aliases
    {"nav_get_entities",      bsp_get_entities},
    {"nav_get_ground",        bsp_get_ground},
    {"nav_trace_wall",        bsp_trace_wall},
    {"nav_trace_model",       bsp_trace_model},
    {"nav_get_contents",      bsp_get_contents},

    {nullptr, nullptr}
};

void RegisterBSPNatives() {
    MF_AddNatives(g_bspNatives);
}
