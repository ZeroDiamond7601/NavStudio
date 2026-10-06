#include "editor/render/entity_renderer.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace {

void AddBoxVertices(
    std::vector<BSPVertex>& solidVerts,
    std::vector<uint32_t>& solidIndices,
    std::vector<BSPVertex>& wireVerts,
    std::vector<uint32_t>& wireIndices,
    const Vector3& mins,
    const Vector3& maxs,
    const Vector4& fillColor,
    const Vector4& wireColor
) {
    Vector3 corners[8] = {
        Vector3(mins.x, mins.y, mins.z), // 0: ---
        Vector3(maxs.x, mins.y, mins.z), // 1: +--
        Vector3(maxs.x, maxs.y, mins.z), // 2: ++-
        Vector3(mins.x, maxs.y, mins.z), // 3: -+-
        Vector3(mins.x, mins.y, maxs.z), // 4: --+
        Vector3(maxs.x, mins.y, maxs.z), // 5: +-+
        Vector3(maxs.x, maxs.y, maxs.z), // 6: +++
        Vector3(mins.x, maxs.y, maxs.z)  // 7: -++
    };

    // 6 Faces (2 triangles each)
    static const int faceIndices[6][4] = {
        { 0, 1, 2, 3 }, // Bottom (-Z)
        { 4, 7, 6, 5 }, // Top (+Z)
        { 0, 4, 5, 1 }, // Front (-Y)
        { 2, 6, 7, 3 }, // Back (+Y)
        { 0, 3, 7, 4 }, // Left (-X)
        { 1, 5, 6, 2 }  // Right (+X)
    };

    static const Vector3 faceNormals[6] = {
        Vector3(0.0f, 0.0f, -1.0f),
        Vector3(0.0f, 0.0f, 1.0f),
        Vector3(0.0f, -1.0f, 0.0f),
        Vector3(0.0f, 1.0f, 0.0f),
        Vector3(-1.0f, 0.0f, 0.0f),
        Vector3(1.0f, 0.0f, 0.0f)
    };

    // Solid
    for (int f = 0; f < 6; ++f) {
        uint32_t base = static_cast<uint32_t>(solidVerts.size());
        const Vector3& n = faceNormals[f];

        for (int i = 0; i < 4; ++i) {
            BSPVertex v;
            const Vector3& p = corners[faceIndices[f][i]];
            v.x = p.x; v.y = p.y; v.z = p.z;
            v.nx = n.x; v.ny = n.y; v.nz = n.z;
            v.u = 0.0f; v.v = 0.0f;
            v.r = fillColor.x; v.g = fillColor.y; v.b = fillColor.z; v.a = fillColor.w;
            solidVerts.push_back(v);
        }

        solidIndices.push_back(base + 0);
        solidIndices.push_back(base + 1);
        solidIndices.push_back(base + 2);
        solidIndices.push_back(base + 0);
        solidIndices.push_back(base + 2);
        solidIndices.push_back(base + 3);
    }

    // Wireframe (12 edges)
    static const int wireEdgePairs[12][2] = {
        {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Bottom
        {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Top
        {0, 4}, {1, 5}, {2, 6}, {3, 7}  // Pillars
    };

    for (int e = 0; e < 12; ++e) {
        uint32_t base = static_cast<uint32_t>(wireVerts.size());
        const Vector3& p0 = corners[wireEdgePairs[e][0]];
        const Vector3& p1 = corners[wireEdgePairs[e][1]];

        BSPVertex v0, v1;
        v0.x = p0.x; v0.y = p0.y; v0.z = p0.z;
        v0.nx = 0.0f; v0.ny = 0.0f; v0.nz = 1.0f;
        v0.u = 0.0f; v0.v = 0.0f;
        v0.r = wireColor.x; v0.g = wireColor.y; v0.b = wireColor.z; v0.a = wireColor.w;

        v1.x = p1.x; v1.y = p1.y; v1.z = p1.z;
        v1.nx = 0.0f; v1.ny = 0.0f; v1.nz = 1.0f;
        v1.u = 0.0f; v1.v = 0.0f;
        v1.r = wireColor.x; v1.g = wireColor.y; v1.b = wireColor.z; v1.a = wireColor.w;

        wireVerts.push_back(v0);
        wireVerts.push_back(v1);

        wireIndices.push_back(base + 0);
        wireIndices.push_back(base + 1);
    }
}

void AddOctahedron(
    std::vector<BSPVertex>& solidVerts,
    std::vector<uint32_t>& solidIndices,
    std::vector<BSPVertex>& wireVerts,
    std::vector<uint32_t>& wireIndices,
    const Vector3& center,
    float radiusX, float radiusY, float radiusZ,
    const Vector4& fillColor,
    const Vector4& wireColor
) {
    Vector3 top(center.x, center.y, center.z + radiusZ);
    Vector3 bottom(center.x, center.y, center.z - radiusZ);
    Vector3 eq[4] = {
        Vector3(center.x + radiusX, center.y, center.z),
        Vector3(center.x, center.y + radiusY, center.z),
        Vector3(center.x - radiusX, center.y, center.z),
        Vector3(center.x, center.y - radiusY, center.z)
    };

    // 8 Triangles
    for (int i = 0; i < 4; ++i) {
        int next = (i + 1) % 4;

        // Top triangle
        uint32_t baseTop = static_cast<uint32_t>(solidVerts.size());
        Vector3 nTop = (eq[i] - top).Cross(eq[next] - top);
        float lenTop = std::sqrt(nTop.x * nTop.x + nTop.y * nTop.y + nTop.z * nTop.z);
        if (lenTop > 1e-4f) { nTop.x /= lenTop; nTop.y /= lenTop; nTop.z /= lenTop; }

        BSPVertex vTop[3];
        vTop[0].x = top.x; vTop[0].y = top.y; vTop[0].z = top.z;
        vTop[1].x = eq[i].x; vTop[1].y = eq[i].y; vTop[1].z = eq[i].z;
        vTop[2].x = eq[next].x; vTop[2].y = eq[next].y; vTop[2].z = eq[next].z;
        for (int k = 0; k < 3; ++k) {
            vTop[k].nx = nTop.x; vTop[k].ny = nTop.y; vTop[k].nz = nTop.z;
            vTop[k].u = 0.0f; vTop[k].v = 0.0f;
            vTop[k].r = fillColor.x; vTop[k].g = fillColor.y; vTop[k].b = fillColor.z; vTop[k].a = fillColor.w;
            solidVerts.push_back(vTop[k]);
        }
        solidIndices.push_back(baseTop + 0);
        solidIndices.push_back(baseTop + 1);
        solidIndices.push_back(baseTop + 2);

        // Bottom triangle
        uint32_t baseBot = static_cast<uint32_t>(solidVerts.size());
        Vector3 nBot = (eq[next] - bottom).Cross(eq[i] - bottom);
        float lenBot = std::sqrt(nBot.x * nBot.x + nBot.y * nBot.y + nBot.z * nBot.z);
        if (lenBot > 1e-4f) { nBot.x /= lenBot; nBot.y /= lenBot; nBot.z /= lenBot; }

        BSPVertex vBot[3];
        vBot[0].x = bottom.x; vBot[0].y = bottom.y; vBot[0].z = bottom.z;
        vBot[1].x = eq[next].x; vBot[1].y = eq[next].y; vBot[1].z = eq[next].z;
        vBot[2].x = eq[i].x; vBot[2].y = eq[i].y; vBot[2].z = eq[i].z;
        for (int k = 0; k < 3; ++k) {
            vBot[k].nx = nBot.x; vBot[k].ny = nBot.y; vBot[k].nz = nBot.z;
            vBot[k].u = 0.0f; vBot[k].v = 0.0f;
            vBot[k].r = fillColor.x; vBot[k].g = fillColor.y; vBot[k].b = fillColor.z; vBot[k].a = fillColor.w;
            solidVerts.push_back(vBot[k]);
        }
        solidIndices.push_back(baseBot + 0);
        solidIndices.push_back(baseBot + 1);
        solidIndices.push_back(baseBot + 2);
    }

    // Wireframe: Equator + ribs to top and bottom
    for (int i = 0; i < 4; ++i) {
        int next = (i + 1) % 4;
        uint32_t base = static_cast<uint32_t>(wireVerts.size());

        BSPVertex p0, p1, pTop, pBot;
        p0.x = eq[i].x; p0.y = eq[i].y; p0.z = eq[i].z;
        p1.x = eq[next].x; p1.y = eq[next].y; p1.z = eq[next].z;
        pTop.x = top.x; pTop.y = top.y; pTop.z = top.z;
        pBot.x = bottom.x; pBot.y = bottom.y; pBot.z = bottom.z;

        p0.r = wireColor.x; p0.g = wireColor.y; p0.b = wireColor.z; p0.a = wireColor.w;
        p1.r = wireColor.x; p1.g = wireColor.y; p1.b = wireColor.z; p1.a = wireColor.w;
        pTop.r = wireColor.x; pTop.g = wireColor.y; pTop.b = wireColor.z; pTop.a = wireColor.w;
        pBot.r = wireColor.x; pBot.g = wireColor.y; pBot.b = wireColor.z; pBot.a = wireColor.w;

        wireVerts.push_back(p0);
        wireVerts.push_back(p1);
        wireVerts.push_back(pTop);
        wireVerts.push_back(pBot);

        // Equator segment
        wireIndices.push_back(base + 0);
        wireIndices.push_back(base + 1);
        // Top rib
        wireIndices.push_back(base + 0);
        wireIndices.push_back(base + 2);
        // Bottom rib
        wireIndices.push_back(base + 0);
        wireIndices.push_back(base + 3);
    }
}

void AddDirectionPointer(
    std::vector<BSPVertex>& solidVerts,
    std::vector<uint32_t>& solidIndices,
    std::vector<BSPVertex>& wireVerts,
    std::vector<uint32_t>& wireIndices,
    const Vector3& origin,
    float yawDeg,
    float length,
    const Vector4& color
) {
    float yawRad = yawDeg * static_cast<float>(M_PI / 180.0);
    Vector3 fwd(std::cos(yawRad), std::sin(yawRad), 0.0f);
    Vector3 right(-std::sin(yawRad), std::cos(yawRad), 0.0f);

    Vector3 tip = origin + fwd * length;
    Vector3 left = origin + fwd * (length * 0.55f) + right * (length * 0.32f);
    Vector3 rightPt = origin + fwd * (length * 0.55f) - right * (length * 0.32f);

    uint32_t baseSolid = static_cast<uint32_t>(solidVerts.size());
    BSPVertex v0, v1, v2;
    v0.x = tip.x; v0.y = tip.y; v0.z = tip.z;
    v1.x = left.x; v1.y = left.y; v1.z = left.z;
    v2.x = rightPt.x; v2.y = rightPt.y; v2.z = rightPt.z;

    v0.nx = 0.0f; v0.ny = 0.0f; v0.nz = 1.0f;
    v1.nx = 0.0f; v1.ny = 0.0f; v1.nz = 1.0f;
    v2.nx = 0.0f; v2.ny = 0.0f; v2.nz = 1.0f;

    v0.r = color.x; v0.g = color.y; v0.b = color.z; v0.a = color.w;
    v1.r = color.x; v1.g = color.y; v1.b = color.z; v1.a = color.w;
    v2.r = color.x; v2.g = color.y; v2.b = color.z; v2.a = color.w;

    solidVerts.push_back(v0);
    solidVerts.push_back(v1);
    solidVerts.push_back(v2);

    solidIndices.push_back(baseSolid + 0);
    solidIndices.push_back(baseSolid + 1);
    solidIndices.push_back(baseSolid + 2);
    solidIndices.push_back(baseSolid + 0);
    solidIndices.push_back(baseSolid + 2);
    solidIndices.push_back(baseSolid + 1);

    // Stem and border lines
    uint32_t baseWire = static_cast<uint32_t>(wireVerts.size());
    BSPVertex s0, s1;
    s0.x = origin.x; s0.y = origin.y; s0.z = origin.z;
    s1.x = tip.x; s1.y = tip.y; s1.z = tip.z;
    s0.r = 1.0f; s0.g = 1.0f; s0.b = 1.0f; s0.a = 1.0f;
    s1.r = 1.0f; s1.g = 1.0f; s1.b = 1.0f; s1.a = 1.0f;

    wireVerts.push_back(s0);
    wireVerts.push_back(s1);
    wireVerts.push_back(v1);
    wireVerts.push_back(v2);

    // Stem: origin -> tip
    wireIndices.push_back(baseWire + 0);
    wireIndices.push_back(baseWire + 1);
    // Arrow edges
    wireIndices.push_back(baseWire + 1);
    wireIndices.push_back(baseWire + 2);
    wireIndices.push_back(baseWire + 2);
    wireIndices.push_back(baseWire + 3);
    wireIndices.push_back(baseWire + 3);
    wireIndices.push_back(baseWire + 1);
}

} // anonymous namespace

EntityRenderer::EntityRenderer()
    : m_solidVao(0)
    , m_solidVbo(0)
    , m_solidEbo(0)
    , m_solidIndexCount(0)
    , m_wireVao(0)
    , m_wireVbo(0)
    , m_wireEbo(0)
    , m_wireIndexCount(0)
    , m_targetLineVao(0)
    , m_targetLineVbo(0)
    , m_targetLineEbo(0)
    , m_targetLineIndexCount(0)
    , m_selectVao(0)
    , m_selectVbo(0)
    , m_selectEbo(0)
    , m_selectIndexCount(0)
    , m_loaded(false)
    , m_showEntities(true)
    , m_showSpawns(true)
    , m_showObjectives(true)
    , m_showLights(true)
    , m_showItems(true)
    , m_showTriggers(true)
    , m_showBrushes(true)
    , m_showTargetLines(true)
{
}

EntityRenderer::~EntityRenderer() {
    Clear();
}

void EntityRenderer::Clear() {
    if (m_solidVao != 0) { glDeleteVertexArrays(1, &m_solidVao); m_solidVao = 0; }
    if (m_solidVbo != 0) { glDeleteBuffers(1, &m_solidVbo); m_solidVbo = 0; }
    if (m_solidEbo != 0) { glDeleteBuffers(1, &m_solidEbo); m_solidEbo = 0; }
    m_solidIndexCount = 0;

    if (m_wireVao != 0) { glDeleteVertexArrays(1, &m_wireVao); m_wireVao = 0; }
    if (m_wireVbo != 0) { glDeleteBuffers(1, &m_wireVbo); m_wireVbo = 0; }
    if (m_wireEbo != 0) { glDeleteBuffers(1, &m_wireEbo); m_wireEbo = 0; }
    m_wireIndexCount = 0;

    if (m_targetLineVao != 0) { glDeleteVertexArrays(1, &m_targetLineVao); m_targetLineVao = 0; }
    if (m_targetLineVbo != 0) { glDeleteBuffers(1, &m_targetLineVbo); m_targetLineVbo = 0; }
    if (m_targetLineEbo != 0) { glDeleteBuffers(1, &m_targetLineEbo); m_targetLineEbo = 0; }
    m_targetLineIndexCount = 0;

    if (m_selectVao != 0) { glDeleteVertexArrays(1, &m_selectVao); m_selectVao = 0; }
    if (m_selectVbo != 0) { glDeleteBuffers(1, &m_selectVbo); m_selectVbo = 0; }
    if (m_selectEbo != 0) { glDeleteBuffers(1, &m_selectEbo); m_selectEbo = 0; }
    m_selectIndexCount = 0;
    m_cachedSelectedEntityIndex = -1;

    m_entities.clear();
    m_countSpawnCT = 0;
    m_countSpawnT = 0;
    m_countObjective = 0;
    m_countLight = 0;
    m_countItem = 0;
    m_countTrigger = 0;
    m_countBrush = 0;
    m_loaded = false;
}

const EditorEntity* EntityRenderer::GetEntity(int index) const {
    if (index < 0 || index >= static_cast<int>(m_entities.size())) return nullptr;
    return &m_entities[index];
}

EditorEntity* EntityRenderer::GetEntity(int index) {
    if (index < 0 || index >= static_cast<int>(m_entities.size())) return nullptr;
    return &m_entities[index];
}

bool EntityRenderer::IsEntityVisible(const EditorEntity& ent) const {
    if (!m_showEntities) return false;
    switch (ent.category) {
        case ENT_CAT_SPAWN_CT:
        case ENT_CAT_SPAWN_T:
        case ENT_CAT_SPAWN_VIP:
            return m_showSpawns;
        case ENT_CAT_OBJECTIVE_BOMB:
        case ENT_CAT_OBJECTIVE_HOSTAGE:
        case ENT_CAT_OBJECTIVE_RESCUE:
        case ENT_CAT_OBJECTIVE_BUYZONE:
            return m_showObjectives;
        case ENT_CAT_LIGHT:
            return m_showLights;
        case ENT_CAT_ITEM:
            return m_showItems;
        case ENT_CAT_TRIGGER:
            return m_showTriggers;
        case ENT_CAT_BRUSH:
            return m_showBrushes;
        default:
            return true;
    }
}

bool EntityRenderer::BuildFromBSP(const BSPFile& bsp) {
    Clear();
    if (!bsp.IsLoaded()) return false;

    const auto& bspEntities = bsp.GetEntities();
    if (bspEntities.empty()) return false;

    m_entities.reserve(bspEntities.size());

    // 1. Parse and classify entities
    for (size_t i = 0; i < bspEntities.size(); ++i) {
        const BSPEntity& raw = bspEntities[i];
        if (raw.classname == "worldspawn") continue; // Worldspawn is world geometry

        EditorEntity ent;
        ent.index = static_cast<int>(i);
        ent.classname = raw.classname;
        ent.targetname = raw.GetString("targetname");
        ent.target = raw.GetString("target");
        ent.model = raw.GetString("model");
        ent.keyvalues = raw.keyvalues;

        // Check if brush entity
        if (!ent.model.empty() && ent.model[0] == '*') {
            ent.isBrush = true;
            ent.brushModelIndex = std::atoi(ent.model.c_str() + 1);
            const dmodel_t* dm = bsp.GetModel(ent.brushModelIndex);
            if (dm) {
                ent.mins = dm->mins;
                ent.maxs = dm->maxs;
                ent.worldMins = dm->mins;
                ent.worldMaxs = dm->maxs;
                ent.origin = (dm->mins + dm->maxs) * 0.5f;
            }
        } else {
            raw.GetOrigin(ent.origin);
        }

        // Angles and Yaw
        if (raw.HasKey("angles")) {
            std::string aStr = raw.GetString("angles");
            std::sscanf(aStr.c_str(), "%f %f %f", &ent.angles.x, &ent.angles.y, &ent.angles.z);
            ent.yaw = ent.angles.y;
        } else if (raw.HasKey("angle")) {
            ent.yaw = raw.GetFloat("angle");
            ent.angles = Vector3(0.0f, ent.yaw, 0.0f);
        }

        // Categorize and apply Hammer-style archetype visual presets
        if (ent.classname == "info_player_start") {
            ent.category = ENT_CAT_SPAWN_CT;
            ent.color = Vector4(0.18f, 0.55f, 0.95f, 1.0f); // CT Blue
            ent.mins = Vector3(-16.0f, -16.0f, -36.0f);
            ent.maxs = Vector3(16.0f, 16.0f, 36.0f);
            m_countSpawnCT++;
        } else if (ent.classname == "info_player_deathmatch") {
            ent.category = ENT_CAT_SPAWN_T;
            ent.color = Vector4(0.92f, 0.22f, 0.18f, 1.0f); // T Red
            ent.mins = Vector3(-16.0f, -16.0f, -36.0f);
            ent.maxs = Vector3(16.0f, 16.0f, 36.0f);
            m_countSpawnT++;
        } else if (ent.classname == "info_vip_start") {
            ent.category = ENT_CAT_SPAWN_VIP;
            ent.color = Vector4(0.0f, 0.85f, 0.85f, 1.0f); // VIP Cyan
            ent.mins = Vector3(-16.0f, -16.0f, -36.0f);
            ent.maxs = Vector3(16.0f, 16.0f, 36.0f);
        } else if (ent.classname == "info_bomb_target" || ent.classname == "func_bomb_target") {
            ent.category = ENT_CAT_OBJECTIVE_BOMB;
            ent.color = Vector4(0.95f, 0.15f, 0.15f, 0.85f); // Bomb Red
            if (!ent.isBrush) {
                ent.mins = Vector3(-16.0f, -16.0f, -16.0f);
                ent.maxs = Vector3(16.0f, 16.0f, 16.0f);
            }
            m_countObjective++;
        } else if (ent.classname == "hostage_entity") {
            ent.category = ENT_CAT_OBJECTIVE_HOSTAGE;
            ent.color = Vector4(0.25f, 0.78f, 0.32f, 1.0f); // Hostage Green
            ent.mins = Vector3(-16.0f, -16.0f, -36.0f);
            ent.maxs = Vector3(16.0f, 16.0f, 36.0f);
            m_countObjective++;
        } else if (ent.classname == "info_hostage_rescue" || ent.classname == "func_hostage_rescue") {
            ent.category = ENT_CAT_OBJECTIVE_RESCUE;
            ent.color = Vector4(0.15f, 0.85f, 0.55f, 0.65f); // Rescue Zone Mint
            if (!ent.isBrush) {
                ent.mins = Vector3(-32.0f, -32.0f, -32.0f);
                ent.maxs = Vector3(32.0f, 32.0f, 32.0f);
            }
            m_countObjective++;
        } else if (ent.classname == "func_buyzone") {
            ent.category = ENT_CAT_OBJECTIVE_BUYZONE;
            ent.color = Vector4(0.85f, 0.90f, 0.20f, 0.55f); // Buyzone Chartreuse
            m_countObjective++;
        } else if (ent.classname.rfind("light", 0) == 0) {
            ent.category = ENT_CAT_LIGHT;
            ent.color = Vector4(1.0f, 0.92f, 0.25f, 1.0f); // Light Yellow
            // Check custom _light key
            if (raw.HasKey("_light")) {
                int lr = 255, lg = 235, lb = 60, li = 200;
                std::sscanf(raw.GetString("_light").c_str(), "%d %d %d %d", &lr, &lg, &lb, &li);
                ent.color.x = std::clamp(lr / 255.0f, 0.1f, 1.0f);
                ent.color.y = std::clamp(lg / 255.0f, 0.1f, 1.0f);
                ent.color.z = std::clamp(lb / 255.0f, 0.1f, 1.0f);
            }
            ent.mins = Vector3(-10.0f, -10.0f, -10.0f);
            ent.maxs = Vector3(10.0f, 10.0f, 10.0f);
            m_countLight++;
        } else if (ent.classname == "armoury_entity" || ent.classname.rfind("weapon_", 0) == 0 || ent.classname.rfind("item_", 0) == 0) {
            ent.category = ENT_CAT_ITEM;
            ent.color = Vector4(1.0f, 0.72f, 0.12f, 1.0f); // Item Amber
            ent.mins = Vector3(-14.0f, -14.0f, 0.0f);
            ent.maxs = Vector3(14.0f, 14.0f, 16.0f);
            m_countItem++;
        } else if (ent.classname == "ambient_generic") {
            ent.category = ENT_CAT_SOUND;
            ent.color = Vector4(0.82f, 0.28f, 0.92f, 1.0f); // Sound Magenta
            ent.mins = Vector3(-10.0f, -10.0f, -10.0f);
            ent.maxs = Vector3(10.0f, 10.0f, 10.0f);
        } else if (ent.classname.rfind("trigger_", 0) == 0) {
            ent.category = ENT_CAT_TRIGGER;
            ent.color = Vector4(0.95f, 0.55f, 0.15f, 0.55f); // Trigger Orange
            m_countTrigger++;
        } else if (ent.isBrush) {
            ent.category = ENT_CAT_BRUSH;
            ent.color = Vector4(0.35f, 0.75f, 0.88f, 0.55f); // Brush Cyan
            m_countBrush++;
        } else {
            ent.category = ENT_CAT_OTHER;
            ent.color = Vector4(0.70f, 0.72f, 0.78f, 1.0f); // General Gray
            ent.mins = Vector3(-10.0f, -10.0f, -10.0f);
            ent.maxs = Vector3(10.0f, 10.0f, 10.0f);
        }

        if (!ent.isBrush) {
            ent.worldMins = ent.origin + ent.mins;
            ent.worldMaxs = ent.origin + ent.maxs;
        }

        m_entities.push_back(ent);
    }

    // 2. Generate 3D meshes for entities
    std::vector<BSPVertex> solidVerts;
    std::vector<uint32_t> solidIndices;

    std::vector<BSPVertex> wireVerts;
    std::vector<uint32_t> wireIndices;

    for (const auto& ent : m_entities) {
        Vector4 wireCol(1.0f, 1.0f, 1.0f, 0.85f);
        wireCol.x = std::min(1.0f, ent.color.x * 1.3f);
        wireCol.y = std::min(1.0f, ent.color.y * 1.3f);
        wireCol.z = std::min(1.0f, ent.color.z * 1.3f);

        if (ent.category == ENT_CAT_SPAWN_CT || ent.category == ENT_CAT_SPAWN_T || ent.category == ENT_CAT_SPAWN_VIP || ent.category == ENT_CAT_OBJECTIVE_HOSTAGE) {
            // Humanoid player hull (Torso box + Head box + Forward viewing arrow)
            Vector3 torsoMin = ent.origin + Vector3(-16.0f, -16.0f, -36.0f);
            Vector3 torsoMax = ent.origin + Vector3(16.0f, 16.0f, 18.0f);
            AddBoxVertices(solidVerts, solidIndices, wireVerts, wireIndices, torsoMin, torsoMax, ent.color, wireCol);

            // Head box
            Vector3 headMin = ent.origin + Vector3(-7.0f, -7.0f, 20.0f);
            Vector3 headMax = ent.origin + Vector3(7.0f, 7.0f, 34.0f);
            Vector4 headColor = ent.color;
            headColor.x = std::min(1.0f, headColor.x * 1.15f);
            headColor.y = std::min(1.0f, headColor.y * 1.15f);
            headColor.z = std::min(1.0f, headColor.z * 1.15f);
            AddBoxVertices(solidVerts, solidIndices, wireVerts, wireIndices, headMin, headMax, headColor, wireCol);

            // Forward viewing arrow
            Vector3 arrowOrigin = ent.origin + Vector3(0.0f, 0.0f, 6.0f);
            AddDirectionPointer(solidVerts, solidIndices, wireVerts, wireIndices, arrowOrigin, ent.yaw, 30.0f, ent.color);
        } else if (ent.category == ENT_CAT_LIGHT || ent.category == ENT_CAT_SOUND) {
            // Octahedron / Diamond archetype
            AddOctahedron(solidVerts, solidIndices, wireVerts, wireIndices, ent.origin, 12.0f, 12.0f, 14.0f, ent.color, wireCol);

            // Spotlight directional ray
            if (ent.classname == "light_spot") {
                AddDirectionPointer(solidVerts, solidIndices, wireVerts, wireIndices, ent.origin, ent.yaw, 36.0f, ent.color);
            }
        } else if (ent.category == ENT_CAT_OBJECTIVE_BOMB) {
            if (ent.isBrush) {
                AddBoxVertices(solidVerts, solidIndices, wireVerts, wireIndices, ent.worldMins, ent.worldMaxs, ent.color, wireCol);
            } else {
                AddOctahedron(solidVerts, solidIndices, wireVerts, wireIndices, ent.origin, 16.0f, 16.0f, 18.0f, ent.color, wireCol);
            }
        } else if (ent.category == ENT_CAT_ITEM) {
            // Armoury ammo crate / item marker
            Vector3 boxMin = ent.origin + Vector3(-14.0f, -14.0f, 0.0f);
            Vector3 boxMax = ent.origin + Vector3(14.0f, 14.0f, 14.0f);
            AddBoxVertices(solidVerts, solidIndices, wireVerts, wireIndices, boxMin, boxMax, ent.color, wireCol);

            // Diamond on top
            Vector3 topDiamond = ent.origin + Vector3(0.0f, 0.0f, 24.0f);
            AddOctahedron(solidVerts, solidIndices, wireVerts, wireIndices, topDiamond, 7.0f, 7.0f, 9.0f, ent.color, wireCol);
        } else {
            // Standard entity bounding box
            AddBoxVertices(solidVerts, solidIndices, wireVerts, wireIndices, ent.worldMins, ent.worldMaxs, ent.color, wireCol);

            if (ent.yaw != 0.0f && !ent.isBrush) {
                AddDirectionPointer(solidVerts, solidIndices, wireVerts, wireIndices, ent.origin, ent.yaw, 22.0f, ent.color);
            }
        }
    }

    if (!solidIndices.empty()) {
        GenerateBuffers(solidVerts, solidIndices);
    }
    if (!wireIndices.empty()) {
        GenerateWireBuffers(wireVerts, wireIndices);
    }

    // 3. Generate Target Linkage Lines (Connecting trigger -> target)
    std::vector<BSPVertex> linkVerts;
    std::vector<uint32_t> linkIndices;

    std::unordered_map<std::string, std::vector<const EditorEntity*>> targetnameMap;
    for (const auto& ent : m_entities) {
        if (!ent.targetname.empty()) {
            targetnameMap[ent.targetname].push_back(&ent);
        }
    }

    for (const auto& ent : m_entities) {
        if (ent.target.empty()) continue;
        auto it = targetnameMap.find(ent.target);
        if (it == targetnameMap.end()) continue;

        for (const auto* targetEnt : it->second) {
            uint32_t base = static_cast<uint32_t>(linkVerts.size());

            BSPVertex p0, p1;
            p0.x = ent.origin.x; p0.y = ent.origin.y; p0.z = ent.origin.z;
            p1.x = targetEnt->origin.x; p1.y = targetEnt->origin.y; p1.z = targetEnt->origin.z;

            // Bright Cyan-Amber connection beam
            p0.r = 0.15f; p0.g = 0.95f; p0.b = 1.0f; p0.a = 0.95f;
            p1.r = 1.00f; p1.g = 0.65f; p1.b = 0.2f; p1.a = 0.95f;

            linkVerts.push_back(p0);
            linkVerts.push_back(p1);

            linkIndices.push_back(base + 0);
            linkIndices.push_back(base + 1);

            // Midpoint directional chevron
            Vector3 mid = (ent.origin + targetEnt->origin) * 0.5f;
            Vector3 dir = targetEnt->origin - ent.origin;
            float dist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            if (dist > 1.0f) {
                dir.x /= dist; dir.y /= dist; dir.z /= dist;
                Vector3 perp(-dir.y, dir.x, 0.0f);
                if (std::abs(perp.x) < 1e-3f && std::abs(perp.y) < 1e-3f) {
                    perp = Vector3(1.0f, 0.0f, 0.0f);
                }

                Vector3 tip = mid + dir * 10.0f;
                Vector3 w0 = mid - dir * 6.0f + perp * 8.0f;
                Vector3 w1 = mid - dir * 6.0f - perp * 8.0f;

                uint32_t mBase = static_cast<uint32_t>(linkVerts.size());
                BSPVertex mv0, mv1, mv2;
                mv0.x = tip.x; mv0.y = tip.y; mv0.z = tip.z;
                mv1.x = w0.x; mv1.y = w0.y; mv1.z = w0.z;
                mv2.x = w1.x; mv2.y = w1.y; mv2.z = w1.z;

                mv0.r = 1.0f; mv0.g = 0.9f; mv0.b = 0.2f; mv0.a = 1.0f;
                mv1.r = 1.0f; mv1.g = 0.9f; mv1.b = 0.2f; mv1.a = 1.0f;
                mv2.r = 1.0f; mv2.g = 0.9f; mv2.b = 0.2f; mv2.a = 1.0f;

                linkVerts.push_back(mv0);
                linkVerts.push_back(mv1);
                linkVerts.push_back(mv2);

                linkIndices.push_back(mBase + 0);
                linkIndices.push_back(mBase + 1);
                linkIndices.push_back(mBase + 0);
                linkIndices.push_back(mBase + 2);
            }
        }
    }

    if (!linkIndices.empty()) {
        GenerateTargetLineBuffers(linkVerts, linkIndices);
    }

    m_loaded = (!m_entities.empty());
    return m_loaded;
}

void EntityRenderer::GenerateBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices) {
    glGenVertexArrays(1, &m_solidVao);
    glGenBuffers(1, &m_solidVbo);
    glGenBuffers(1, &m_solidEbo);

    glBindVertexArray(m_solidVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_solidVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(BSPVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_solidEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, x));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, nx));
    glEnableVertexAttribArray(1);

    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, u));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, r));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    m_solidIndexCount = static_cast<GLsizei>(indices.size());
}

void EntityRenderer::GenerateWireBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices) {
    glGenVertexArrays(1, &m_wireVao);
    glGenBuffers(1, &m_wireVbo);
    glGenBuffers(1, &m_wireEbo);

    glBindVertexArray(m_wireVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_wireVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(BSPVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_wireEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, x));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    m_wireIndexCount = static_cast<GLsizei>(indices.size());
}

void EntityRenderer::GenerateTargetLineBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices) {
    glGenVertexArrays(1, &m_targetLineVao);
    glGenBuffers(1, &m_targetLineVbo);
    glGenBuffers(1, &m_targetLineEbo);

    glBindVertexArray(m_targetLineVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_targetLineVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(BSPVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_targetLineEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, x));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    m_targetLineIndexCount = static_cast<GLsizei>(indices.size());
}

void EntityRenderer::GenerateSelectBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices) {
    if (m_selectVao == 0) {
        glGenVertexArrays(1, &m_selectVao);
        glGenBuffers(1, &m_selectVbo);
        glGenBuffers(1, &m_selectEbo);
    }

    glBindVertexArray(m_selectVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_selectVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(BSPVertex), vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_selectEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, x));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    m_selectIndexCount = static_cast<GLsizei>(indices.size());
}

void EntityRenderer::Render(
    const Shader& meshShader,
    const Shader& lineShader,
    const Matrix4& mvp,
    int selectedEntityIndex,
    const Vector3& camPos
) {
    if (!m_loaded || !m_showEntities) return;

    Matrix4 modelMat = Matrix4::MakeIdentity();

    // 1. Draw solid entities
    if (m_solidVao != 0 && m_solidIndexCount > 0) {
        meshShader.Bind();
        meshShader.SetMat4("u_MVP", mvp);
        meshShader.SetMat4("u_Model", modelMat);
        meshShader.SetVec3("u_CameraPos", camPos.x, camPos.y, camPos.z);
        meshShader.SetVec4("u_BaseColor", 1.0f, 1.0f, 1.0f, 1.0f);
        meshShader.SetInt("u_UseTexture", 0);
        meshShader.SetFloat("u_Alpha", 0.90f);
        meshShader.SetInt("u_EnableLighting", 1);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_TRUE);

        glBindVertexArray(m_solidVao);
        glDrawElements(GL_TRIANGLES, m_solidIndexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    // 2. Draw wireframe outlines for entities
    if (m_wireVao != 0 && m_wireIndexCount > 0) {
        lineShader.Bind();
        lineShader.SetMat4("u_MVP", mvp);
        lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

        glLineWidth(1.5f);
        glBindVertexArray(m_wireVao);
        glDrawElements(GL_LINES, m_wireIndexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    // 3. Draw Target Connections (Trigger -> Target lines)
    if (m_showTargetLines && m_targetLineVao != 0 && m_targetLineIndexCount > 0) {
        lineShader.Bind();
        lineShader.SetMat4("u_MVP", mvp);
        lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

        glLineWidth(2.2f);
        glBindVertexArray(m_targetLineVao);
        glDrawElements(GL_LINES, m_targetLineIndexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

    // 4. Draw Selected Entity Highlight Box
    if (selectedEntityIndex >= 0 && selectedEntityIndex < static_cast<int>(m_entities.size())) {
        if (m_cachedSelectedEntityIndex != selectedEntityIndex) {
            m_cachedSelectedEntityIndex = selectedEntityIndex;
            const EditorEntity& sel = m_entities[selectedEntityIndex];

            std::vector<BSPVertex> selVerts;
            std::vector<uint32_t> selIndices;

            // Expanded bounding box for clear highlight
            Vector3 expMin = sel.worldMins - Vector3(2.0f, 2.0f, 2.0f);
            Vector3 expMax = sel.worldMaxs + Vector3(2.0f, 2.0f, 2.0f);

            std::vector<BSPVertex> dummySolid;
            std::vector<uint32_t> dummySolidIdx;
            Vector4 selColor(1.0f, 0.95f, 0.15f, 1.0f); // Bright yellow highlight

            AddBoxVertices(dummySolid, dummySolidIdx, selVerts, selIndices, expMin, expMax, selColor, selColor);

            // Add center crosshairs at origin
            uint32_t cBase = static_cast<uint32_t>(selVerts.size());
            float cLen = 20.0f;
            Vector3 ax[6] = {
                sel.origin + Vector3(-cLen, 0, 0), sel.origin + Vector3(cLen, 0, 0),
                sel.origin + Vector3(0, -cLen, 0), sel.origin + Vector3(0, cLen, 0),
                sel.origin + Vector3(0, 0, -cLen), sel.origin + Vector3(0, 0, cLen)
            };
            for (int k = 0; k < 6; ++k) {
                BSPVertex cv;
                cv.x = ax[k].x; cv.y = ax[k].y; cv.z = ax[k].z;
                cv.r = 1.0f; cv.g = 1.0f; cv.b = 0.2f; cv.a = 1.0f;
                selVerts.push_back(cv);
            }
            selIndices.push_back(cBase + 0); selIndices.push_back(cBase + 1);
            selIndices.push_back(cBase + 2); selIndices.push_back(cBase + 3);
            selIndices.push_back(cBase + 4); selIndices.push_back(cBase + 5);

            GenerateSelectBuffers(selVerts, selIndices);
        }

        if (m_selectVao != 0 && m_selectIndexCount > 0) {
            lineShader.Bind();
            lineShader.SetMat4("u_MVP", mvp);
            lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

            glDisable(GL_DEPTH_TEST); // Render through walls for immediate visibility
            glLineWidth(3.0f);
            glBindVertexArray(m_selectVao);
            glDrawElements(GL_LINES, m_selectIndexCount, GL_UNSIGNED_INT, nullptr);
            glBindVertexArray(0);
            glEnable(GL_DEPTH_TEST);
        }
    }
}
