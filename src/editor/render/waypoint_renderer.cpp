#include "editor/render/waypoint_renderer.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

WaypointRenderer::WaypointRenderer() = default;

WaypointRenderer::~WaypointRenderer() {
    Clear();
}

void WaypointRenderer::Clear() {
    if (m_lineVAO) {
        glDeleteVertexArrays(1, &m_lineVAO);
        m_lineVAO = 0;
    }
    if (m_lineVBO) {
        glDeleteBuffers(1, &m_lineVBO);
        m_lineVBO = 0;
    }
    if (m_lineEBO) {
        glDeleteBuffers(1, &m_lineEBO);
        m_lineEBO = 0;
    }
    m_lineIndexCount = 0;
    m_nodeCount = 0;
    m_loaded = false;
}

bool WaypointRenderer::BuildFromGraph(
    const WaypointGraph& graph,
    uint32_t selectedId,
    uint32_t hoveredId,
    const std::unordered_set<uint32_t>* selectedSet,
    const Vector3* penPreviewStart,
    const Vector3* penPreviewEnd,
    bool penPreviewClear,
    const std::vector<uint32_t>* ghostBotPath,
    const Vector3* ghostBotPos,
    float ghostBotYaw,
    uint32_t selectedConnFrom,
    uint32_t selectedConnTo
) {
    Clear();
    if (graph.IsEmpty() && !penPreviewStart && !ghostBotPos) return false;

    const auto& nodes = graph.GetNodes();
    m_nodeCount = nodes.size();
    GameMod mod = graph.GetActiveMod();

    std::vector<WaypointVertex> vertices;
    std::vector<uint32_t> indices;

    for (const auto& node : nodes) {
        bool isSelected = (node.id == selectedId) || (selectedSet && selectedSet->find(node.id) != selectedSet->end());
        bool isHovered = (node.id == hoveredId);

        // Determine node colors according to exact CS-EBOT / GoldSrc bot color rules:
        // By default, CS waypoints are Green (0, 255, 0)
        float r = 0.0f, g = 1.0f, b = 0.0f, a = 0.95f;

        if (node.flags & WPT_FLAG_CAMP) {
            r = 0.0f; g = 1.0f; b = 1.0f; // Cyan (0, 255, 255)
        } else if (node.flags & (WPT_FLAG_GOAL | WPT_FLAG_HELICOPTER)) {
            r = 0.50f; g = 0.0f; b = 1.0f; // Purple / Violet (128, 0, 255)
        } else if (node.flags & WPT_FLAG_LADDER) {
            r = 0.50f; g = 0.25f; b = 0.0f; // Brown (128, 64, 0)
        } else if (node.flags & WPT_FLAG_RESCUE) {
            r = 1.0f; g = 1.0f; b = 1.0f; // White (255, 255, 255)
        } else if (node.flags & WPT_FLAG_AVOID) {
            r = 1.0f; g = 0.0f; b = 0.0f; // Red (255, 0, 0)
        } else if (node.flags & WPT_FLAG_FALLCHECK) {
            r = 0.50f; g = 0.50f; b = 0.50f; // Gray (128, 128, 128)
        } else if (node.flags & WPT_FLAG_USEBUTTON) {
            r = 0.0f; g = 0.0f; b = 1.0f; // Blue (0, 0, 255)
        } else if (node.flags & WPT_FLAG_ZMHMCAMP) {
            r = 0.78f; g = 0.27f; b = 0.82f; // Magenta (199, 69, 209)
        } else if (node.flags & WPT_FLAG_HMCAMPMESH) {
            r = 0.20f; g = 0.49f; b = 1.0f; // Sky Blue (50, 125, 255)
        } else if (node.flags & WPT_FLAG_ZOMBIEONLY) {
            r = 1.0f; g = 0.0f; b = 0.0f; // Red (255, 0, 0)
        } else if (node.flags & WPT_FLAG_HUMANONLY) {
            r = 0.0f; g = 0.0f; b = 1.0f; // Blue (0, 0, 255)
        } else if (node.flags & WPT_FLAG_ZOMBIEPUSH) {
            r = 0.98f; g = 0.29f; b = 0.59f; // Pink / Salmon (250, 75, 150)
        } else if (node.flags & (WPT_FLAG_FALLRISK | WPT_FLAG_SPECIFICGRAVITY)) {
            r = 0.50f; g = 0.50f; b = 0.50f; // Gray (128, 128, 128)
        } else if (node.flags & WPT_FLAG_ONLYONE) {
            r = 1.0f; g = 1.0f; b = 0.0f; // Yellow (255, 255, 0)
        } else if (node.flags & WPT_FLAG_WAITUNTIL) {
            r = 0.0f; g = 0.0f; b = 1.0f; // Blue (0, 0, 255)
        } else if (node.flags & WPT_FLAG_TERRORIST) {
            r = 1.0f; g = 0.0f; b = 0.0f; // Red (255, 0, 0)
        } else if (node.flags & WPT_FLAG_COUNTER) {
            r = 0.0f; g = 0.0f; b = 1.0f; // Blue (0, 0, 255)
        } else if (node.flags & WPT_FLAG_SNIPER) {
            r = 0.51f; g = 0.34f; b = 0.0f; // Brown / Amber (130, 87, 0)
        }

        // Subflag tactical color for upper quarter of the vertical pillar
        float fr = r, fg = g, fb = b;
        bool hasFlagColor = false;
        if (node.flags & WPT_FLAG_SNIPER) {
            fr = 0.51f; fg = 0.34f; fb = 0.0f; hasFlagColor = true;
        } else if (node.flags & WPT_FLAG_TERRORIST) {
            fr = 1.0f; fg = 0.0f; fb = 0.0f; hasFlagColor = true;
        } else if (node.flags & WPT_FLAG_COUNTER) {
            fr = 0.0f; fg = 0.0f; fb = 1.0f; hasFlagColor = true;
        } else if (node.flags & (WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH)) {
            fr = 0.0f; fg = 0.0f; fb = 1.0f; hasFlagColor = true;
        } else if (node.flags & (WPT_FLAG_ZOMBIEONLY | WPT_FLAG_HUMANONLY)) {
            fr = 1.0f; fg = 0.0f; fb = 1.0f; hasFlagColor = true;
        } else if (node.flags & WPT_FLAG_ZOMBIEPUSH) {
            fr = 1.0f; fg = 0.0f; fb = 0.0f; hasFlagColor = true;
        } else if (node.flags & (WPT_FLAG_FALLRISK | WPT_FLAG_WAITUNTIL)) {
            fr = 0.98f; fg = 0.29f; fb = 0.59f; hasFlagColor = true;
        } else if (node.flags & WPT_FLAG_SPECIFICGRAVITY) {
            fr = 0.50f; fg = 0.0f; fb = 1.0f; hasFlagColor = true;
        }

        if (isSelected) {
            r = 1.0f; g = 0.95f; b = 0.1f; a = 1.0f; // Highlight Yellow
            fr = 1.0f; fg = 0.95f; fb = 0.1f;
        } else if (isHovered) {
            r = std::min(1.0f, r + 0.3f);
            g = std::min(1.0f, g + 0.3f);
            b = std::min(1.0f, b + 0.3f);
        }

        // 1. Draw In-Game CS Vertical Pillar Beam (Floor to Stand/Crouch Player Height)
        float pillarHeight = (node.flags & WPT_FLAG_CROUCH) ? 36.0f : 72.0f;
        Vector3 floorPt = node.origin;
        Vector3 topPt   = node.origin + Vector3(0.0f, 0.0f, pillarHeight);

        uint32_t pillarBase = static_cast<uint32_t>(vertices.size());
        if (hasFlagColor && !isSelected) {
            Vector3 midPt = node.origin + Vector3(0.0f, 0.0f, pillarHeight * 0.75f);
            // Lower 75% base color
            vertices.push_back({ floorPt.x, floorPt.y, floorPt.z, 0,0,1, 0,0, r, g, b, 0.85f });
            vertices.push_back({ midPt.x, midPt.y, midPt.z, 0,0,1, 0,0, r, g, b, 0.85f });
            // Upper 25% flag color
            vertices.push_back({ midPt.x, midPt.y, midPt.z, 0,0,1, 0,0, fr, fg, fb, 0.95f });
            vertices.push_back({ topPt.x, topPt.y, topPt.z, 0,0,1, 0,0, fr, fg, fb, 0.95f });
            indices.push_back(pillarBase + 0); indices.push_back(pillarBase + 1);
            indices.push_back(pillarBase + 2); indices.push_back(pillarBase + 3);
        } else {
            vertices.push_back({ floorPt.x, floorPt.y, floorPt.z, 0,0,1, 0,0, r, g, b, 0.85f });
            vertices.push_back({ topPt.x, topPt.y, topPt.z, 0,0,1, 0,0, r, g, b, 0.85f });
            indices.push_back(pillarBase + 0); indices.push_back(pillarBase + 1);
        }

        // 2. Draw 3D Cross at Center (Elevated for clear 3D selection)
        Vector3 pos = node.origin + Vector3(0.0f, 0.0f, pillarHeight * 0.5f);
        float sz = isSelected ? 12.0f : 6.0f;

        uint32_t cBase = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ pos.x - sz, pos.y, pos.z, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x + sz, pos.y, pos.z, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x, pos.y - sz, pos.z, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x, pos.y + sz, pos.z, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x, pos.y, pos.z - sz, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x, pos.y, pos.z + sz, 0,0,1, 0,0, r, g, b, a });

        indices.push_back(cBase + 0); indices.push_back(cBase + 1);
        indices.push_back(cBase + 2); indices.push_back(cBase + 3);
        indices.push_back(cBase + 4); indices.push_back(cBase + 5);

        // 3. Player Head Silhouette Ring (At Top of Pillar)
        {
            float headZ = topPt.z;
            float headRad = isSelected ? 12.0f : 8.0f;
            const int hSegs = 8;
            uint32_t headBase = static_cast<uint32_t>(vertices.size());
            for (int s = 0; s < hSegs; ++s) {
                float theta = (2.0f * static_cast<float>(M_PI) * s) / hSegs;
                float hx = node.origin.x + std::cos(theta) * headRad;
                float hy = node.origin.y + std::sin(theta) * headRad;
                vertices.push_back({ hx, hy, headZ, 0,0,1, 0,0, fr, fg, fb, isSelected ? 1.0f : 0.65f });
            }
            for (int s = 0; s < hSegs; ++s) {
                indices.push_back(headBase + s);
                indices.push_back(headBase + ((s + 1) % hSegs));
            }
        }

        // 4. Ground Tolerance Wayzone Disc & Precision Markers
        if (m_showRadii || isSelected) {
            float rad = node.radius;
            if (rad > 0.0f) {
                float effRad = std::max(12.0f, rad);
                int segments = isSelected ? 24 : 16;
                uint32_t circleBase = static_cast<uint32_t>(vertices.size());
                float circleAlpha = isSelected ? 0.90f : 0.40f;

                for (int s = 0; s < segments; ++s) {
                    float theta = (2.0f * static_cast<float>(M_PI) * s) / segments;
                    float cx = node.origin.x + std::cos(theta) * effRad;
                    float cy = node.origin.y + std::sin(theta) * effRad;
                    vertices.push_back({ cx, cy, node.origin.z + 1.0f, 0,0,1, 0,0, r, g, b, circleAlpha });
                }

                for (int s = 0; s < segments; ++s) {
                    indices.push_back(circleBase + s);
                    indices.push_back(circleBase + ((s + 1) % segments));
                }

                // 4 Cardinal Spokes (Crosshairs) connecting center to circle perimeter
                uint32_t spokeBase = static_cast<uint32_t>(vertices.size());
                vertices.push_back({ node.origin.x - effRad, node.origin.y, node.origin.z + 1.0f, 0,0,1, 0,0, r, g, b, circleAlpha * 0.6f });
                vertices.push_back({ node.origin.x + effRad, node.origin.y, node.origin.z + 1.0f, 0,0,1, 0,0, r, g, b, circleAlpha * 0.6f });
                vertices.push_back({ node.origin.x, node.origin.y - effRad, node.origin.z + 1.0f, 0,0,1, 0,0, r, g, b, circleAlpha * 0.6f });
                vertices.push_back({ node.origin.x, node.origin.y + effRad, node.origin.z + 1.0f, 0,0,1, 0,0, r, g, b, circleAlpha * 0.6f });
                indices.push_back(spokeBase + 0); indices.push_back(spokeBase + 1);
                indices.push_back(spokeBase + 2); indices.push_back(spokeBase + 3);

                // Concentric inner ring for wide wayzones (radius >= 48u)
                if (effRad >= 48.0f) {
                    float innerRad = effRad * 0.5f;
                    int iSegs = 12;
                    uint32_t innerBase = static_cast<uint32_t>(vertices.size());
                    for (int s = 0; s < iSegs; ++s) {
                        float theta = (2.0f * static_cast<float>(M_PI) * s) / iSegs;
                        float cx = node.origin.x + std::cos(theta) * innerRad;
                        float cy = node.origin.y + std::sin(theta) * innerRad;
                        vertices.push_back({ cx, cy, node.origin.z + 1.0f, 0,0,1, 0,0, r, g, b, circleAlpha * 0.35f });
                    }
                    for (int s = 0; s < iSegs; ++s) {
                        indices.push_back(innerBase + s);
                        indices.push_back(innerBase + ((s + 1) % iSegs));
                    }
                }
            } else {
                // Strict zero-radius precision waypoint (e.g. ladder, jump takeoff): draw red/amber precision diamond
                float dSz = isSelected ? 10.0f : 6.0f;
                uint32_t dBase = static_cast<uint32_t>(vertices.size());
                vertices.push_back({ node.origin.x - dSz, node.origin.y, node.origin.z + 1.0f, 0,0,1, 0,0, 1.0f, 0.2f, 0.2f, 0.85f });
                vertices.push_back({ node.origin.x, node.origin.y + dSz, node.origin.z + 1.0f, 0,0,1, 0,0, 1.0f, 0.2f, 0.2f, 0.85f });
                vertices.push_back({ node.origin.x + dSz, node.origin.y, node.origin.z + 1.0f, 0,0,1, 0,0, 1.0f, 0.2f, 0.2f, 0.85f });
                vertices.push_back({ node.origin.x, node.origin.y - dSz, node.origin.z + 1.0f, 0,0,1, 0,0, 1.0f, 0.2f, 0.2f, 0.85f });
                indices.push_back(dBase + 0); indices.push_back(dBase + 1);
                indices.push_back(dBase + 1); indices.push_back(dBase + 2);
                indices.push_back(dBase + 2); indices.push_back(dBase + 3);
                indices.push_back(dBase + 3); indices.push_back(dBase + 0);
            }
        }

        // 5. Camp / Sniper Aim Direction Vector & 3D Vision Frustum
        if (m_showDirection && (node.flags & (WPT_FLAG_CAMP | WPT_FLAG_SNIPER | WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH))) {
            float yawRad = node.campYaw * static_cast<float>(M_PI) / 180.0f;
            float pitchRad = node.campPitch * static_cast<float>(M_PI) / 180.0f;

            Vector3 aimDir(
                std::cos(pitchRad) * std::cos(yawRad),
                std::cos(pitchRad) * std::sin(yawRad),
                std::sin(pitchRad)
            );

            float coneDist = isSelected ? 72.0f : 48.0f;
            Vector3 eyePos = node.origin + Vector3(0.0f, 0.0f, pillarHeight * 0.75f);
            Vector3 aimEnd = eyePos + aimDir * coneDist;

            uint32_t aimBase = static_cast<uint32_t>(vertices.size());
            vertices.push_back({ eyePos.x, eyePos.y, eyePos.z, 0,0,1, 0,0, 1.0f, 0.9f, 0.1f, 1.0f });
            vertices.push_back({ aimEnd.x, aimEnd.y, aimEnd.z, 0,0,1, 0,0, 1.0f, 0.2f, 0.2f, 1.0f });
            indices.push_back(aimBase + 0);
            indices.push_back(aimBase + 1);

            // 3D View Frustum Cone (4 edge rays + rectangular end frame + crosshairs)
            Vector3 worldUp(0, 0, 1);
            Vector3 right = aimDir.Cross(worldUp);
            if (right.Length() < 0.01f) right = Vector3(1, 0, 0);
            else right = right.Normalized();
            Vector3 up = right.Cross(aimDir).Normalized();

            float coneSpread = coneDist * 0.38f;
            Vector3 cTL = aimEnd - right * coneSpread + up * coneSpread;
            Vector3 cTR = aimEnd + right * coneSpread + up * coneSpread;
            Vector3 cBR = aimEnd + right * coneSpread - up * coneSpread;
            Vector3 cBL = aimEnd - right * coneSpread - up * coneSpread;

            uint32_t fBase = static_cast<uint32_t>(vertices.size());
            float fAlpha = isSelected ? 0.85f : 0.45f;
            vertices.push_back({ cTL.x, cTL.y, cTL.z, 0,0,1, 0,0, 0.2f, 0.9f, 1.0f, fAlpha });
            vertices.push_back({ cTR.x, cTR.y, cTR.z, 0,0,1, 0,0, 0.2f, 0.9f, 1.0f, fAlpha });
            vertices.push_back({ cBR.x, cBR.y, cBR.z, 0,0,1, 0,0, 0.2f, 0.9f, 1.0f, fAlpha });
            vertices.push_back({ cBL.x, cBL.y, cBL.z, 0,0,1, 0,0, 0.2f, 0.9f, 1.0f, fAlpha });

            // 4 corner rays from eye
            indices.push_back(aimBase + 0); indices.push_back(fBase + 0);
            indices.push_back(aimBase + 0); indices.push_back(fBase + 1);
            indices.push_back(aimBase + 0); indices.push_back(fBase + 2);
            indices.push_back(aimBase + 0); indices.push_back(fBase + 3);

            // End frame perimeter
            indices.push_back(fBase + 0); indices.push_back(fBase + 1);
            indices.push_back(fBase + 1); indices.push_back(fBase + 2);
            indices.push_back(fBase + 2); indices.push_back(fBase + 3);
            indices.push_back(fBase + 3); indices.push_back(fBase + 0);

            // Center crosshairs on the end frame
            Vector3 topMid = (cTL + cTR) * 0.5f;
            Vector3 botMid = (cBL + cBR) * 0.5f;
            Vector3 leftMid = (cTL + cBL) * 0.5f;
            Vector3 rightMid = (cTR + cBR) * 0.5f;
            uint32_t xhairBase = static_cast<uint32_t>(vertices.size());
            vertices.push_back({ topMid.x, topMid.y, topMid.z, 0,0,1, 0,0, 1.0f, 0.4f, 0.4f, fAlpha * 0.75f });
            vertices.push_back({ botMid.x, botMid.y, botMid.z, 0,0,1, 0,0, 1.0f, 0.4f, 0.4f, fAlpha * 0.75f });
            vertices.push_back({ leftMid.x, leftMid.y, leftMid.z, 0,0,1, 0,0, 1.0f, 0.4f, 0.4f, fAlpha * 0.75f });
            vertices.push_back({ rightMid.x, rightMid.y, rightMid.z, 0,0,1, 0,0, 1.0f, 0.4f, 0.4f, fAlpha * 0.75f });
            indices.push_back(xhairBase + 0); indices.push_back(xhairBase + 1);
            indices.push_back(xhairBase + 2); indices.push_back(xhairBase + 3);
        }

        // 6. Outgoing Path Connection Links (Enhanced Direction Chevrons & Parkour Arcs)
        if (m_showConnections) {
            for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                int16_t targetId = node.connections[c];
                if (targetId <= 0) continue;

                const WaypointNode* target = graph.GetNodeByID(static_cast<uint32_t>(targetId));
                if (!target) continue;

                bool targetHasReverse = target->HasConnectionTo(static_cast<int16_t>(node.id));
                if (targetHasReverse && target->id < node.id) continue;

                Vector3 startPt = node.origin + Vector3(0.0f, 0.0f, 10.0f);
                Vector3 endPt   = target->origin + Vector3(0.0f, 0.0f, 10.0f);

                // CS-EBOT Connection Beam Colors:
                // Jumping: Crimson Red (255, 30, 20)
                // Double Jump: Electric Blue (30, 160, 255)
                // Visible: Emerald Green (30, 255, 60)
                // Crouch: Safety Orange (255, 150, 10)
                // Bothways / 2-Way: Golden Yellow (255, 230, 40)
                // Oneway Outgoing: Crisp White (250, 250, 250)
                float lr = 0.98f, lg = 0.98f, lb = 0.98f, la = 0.90f;

                // Case A: Parkour Jump Arcs
                if (node.connectionFlags[c] & WPT_CONN_JUMP) {
                    lr = 1.0f; lg = 0.15f; lb = 0.10f; // Red: Jumping
                    if (m_showParkourArcs) {
                        const int arcSegments = 12;
                        float apexDelta = std::max(28.0f, (endPt.z > startPt.z ? (endPt.z - startPt.z) + 18.0f : 32.0f));
                        float apexHeight = std::max(startPt.z, endPt.z) + apexDelta;
                        uint32_t arcBase = static_cast<uint32_t>(vertices.size());
                        Vector3 apexPt;

                        for (int s = 0; s <= arcSegments; ++s) {
                            float t = static_cast<float>(s) / arcSegments;
                            float px = startPt.x + (endPt.x - startPt.x) * t;
                            float py = startPt.y + (endPt.y - startPt.y) * t;
                            float baseZ = startPt.z + (endPt.z - startPt.z) * t;
                            float pz = baseZ + 4.0f * (apexHeight - std::max(startPt.z, endPt.z)) * t * (1.0f - t);
                            if (s == arcSegments / 2) apexPt = Vector3(px, py, pz);
                            vertices.push_back({ px, py, pz, 0,0,1, 0,0, lr, lg, lb, la });
                        }
                        for (int s = 0; s < arcSegments; ++s) {
                            indices.push_back(arcBase + s);
                            indices.push_back(arcBase + s + 1);
                        }

                        // Vertical apex guideline to floor
                        uint32_t apexBase = static_cast<uint32_t>(vertices.size());
                        float groundApexZ = (startPt.z + endPt.z) * 0.5f;
                        vertices.push_back({ apexPt.x, apexPt.y, apexPt.z, 0,0,1, 0,0, lr, lg, lb, 0.40f });
                        vertices.push_back({ apexPt.x, apexPt.y, groundApexZ, 0,0,1, 0,0, lr, lg, lb, 0.40f });
                        indices.push_back(apexBase + 0); indices.push_back(apexBase + 1);

                        // Landing target disc on ground at endPt
                        float lRad = 10.0f;
                        int lSegs = 10;
                        uint32_t landBase = static_cast<uint32_t>(vertices.size());
                        for (int s = 0; s < lSegs; ++s) {
                            float theta = (2.0f * static_cast<float>(M_PI) * s) / lSegs;
                            vertices.push_back({ endPt.x + std::cos(theta) * lRad, endPt.y + std::sin(theta) * lRad, endPt.z - 8.0f, 0,0,1, 0,0, lr, lg, lb, 0.70f });
                        }
                        for (int s = 0; s < lSegs; ++s) {
                            indices.push_back(landBase + s);
                            indices.push_back(landBase + ((s + 1) % lSegs));
                        }
                        // Landing target inner cross
                        uint32_t lxBase = static_cast<uint32_t>(vertices.size());
                        vertices.push_back({ endPt.x - lRad, endPt.y, endPt.z - 8.0f, 0,0,1, 0,0, lr, lg, lb, 0.60f });
                        vertices.push_back({ endPt.x + lRad, endPt.y, endPt.z - 8.0f, 0,0,1, 0,0, lr, lg, lb, 0.60f });
                        vertices.push_back({ endPt.x, endPt.y - lRad, endPt.z - 8.0f, 0,0,1, 0,0, lr, lg, lb, 0.60f });
                        vertices.push_back({ endPt.x, endPt.y + lRad, endPt.z - 8.0f, 0,0,1, 0,0, lr, lg, lb, 0.60f });
                        indices.push_back(lxBase + 0); indices.push_back(lxBase + 1);
                        indices.push_back(lxBase + 2); indices.push_back(lxBase + 3);
                        continue;
                    }
                } else if (node.connectionFlags[c] & WPT_CONN_DOUBLE) {
                    lr = 0.15f; lg = 0.65f; lb = 1.0f; // Electric Blue: Double-jump
                } else if (node.connectionFlags[c] & WPT_CONN_VISIBLE) {
                    lr = 0.15f; lg = 1.0f; lb = 0.30f; // Green: Line of sight clear
                } else if (node.connectionFlags[c] & WPT_CONN_CROUCH) {
                    lr = 1.0f; lg = 0.58f; lb = 0.05f; // Orange: Crouch ducking connection
                } else if (targetHasReverse) {
                    lr = 1.0f; lg = 0.90f; lb = 0.15f; // Yellow: Two-way bothways
                }

                bool isSelectedConn = (selectedConnFrom == node.id && selectedConnTo == target->id) ||
                                      (targetHasReverse && selectedConnFrom == target->id && selectedConnTo == node.id);
                if (isSelectedConn) {
                    lr = 0.10f; lg = 1.0f; lb = 1.0f; la = 1.0f; // Glowing Cyan when selected
                }

                // Render main link line
                uint32_t lBase = static_cast<uint32_t>(vertices.size());
                vertices.push_back({ startPt.x, startPt.y, startPt.z, 0,0,1, 0,0, lr, lg, lb, la });
                vertices.push_back({ endPt.x, endPt.y, endPt.z, 0,0,1, 0,0, lr, lg, lb, la });
                indices.push_back(lBase + 0);
                indices.push_back(lBase + 1);

                if (isSelectedConn) {
                    // Draw extra halo line for selected connection
                    uint32_t hBase = static_cast<uint32_t>(vertices.size());
                    vertices.push_back({ startPt.x, startPt.y, startPt.z + 1.5f, 0,0,1, 0,0, 1.0f, 1.0f, 1.0f, 1.0f });
                    vertices.push_back({ endPt.x, endPt.y, endPt.z + 1.5f, 0,0,1, 0,0, 1.0f, 1.0f, 1.0f, 1.0f });
                    indices.push_back(hBase + 0);
                    indices.push_back(hBase + 1);
                }

                Vector3 fwd = (endPt - startPt).Normalized();
                Vector3 worldUp(0, 0, 1);
                Vector3 side = fwd.Cross(worldUp);
                if (side.Length() < 0.01f) side = Vector3(1, 0, 0);
                else side = side.Normalized();

                auto DrawArrowHead = [&](const Vector3& tip, const Vector3& dir, const Vector3& lat, float len, float width, float ar, float ag, float ab, float aa) {
                    Vector3 base = tip - dir * len;
                    Vector3 w1 = base + lat * width;
                    Vector3 w2 = base - lat * width;
                    Vector3 up = lat.Cross(dir).Normalized();
                    Vector3 w3 = base + up * (width * 0.7f);
                    Vector3 w4 = base - up * (width * 0.7f);

                    uint32_t aBase = static_cast<uint32_t>(vertices.size());
                    vertices.push_back({ tip.x, tip.y, tip.z, 0,0,1, 0,0, ar, ag, ab, aa });
                    vertices.push_back({ w1.x, w1.y, w1.z, 0,0,1, 0,0, ar, ag, ab, aa });
                    vertices.push_back({ w2.x, w2.y, w2.z, 0,0,1, 0,0, ar, ag, ab, aa });
                    vertices.push_back({ w3.x, w3.y, w3.z, 0,0,1, 0,0, ar, ag, ab, aa });
                    vertices.push_back({ w4.x, w4.y, w4.z, 0,0,1, 0,0, ar, ag, ab, aa });
                    vertices.push_back({ base.x, base.y, base.z, 0,0,1, 0,0, ar, ag, ab, aa * 0.75f });

                    indices.push_back(aBase + 0); indices.push_back(aBase + 1);
                    indices.push_back(aBase + 0); indices.push_back(aBase + 2);
                    indices.push_back(aBase + 0); indices.push_back(aBase + 3);
                    indices.push_back(aBase + 0); indices.push_back(aBase + 4);

                    indices.push_back(aBase + 1); indices.push_back(aBase + 3);
                    indices.push_back(aBase + 3); indices.push_back(aBase + 2);
                    indices.push_back(aBase + 2); indices.push_back(aBase + 4);
                    indices.push_back(aBase + 4); indices.push_back(aBase + 1);

                    indices.push_back(aBase + 1); indices.push_back(aBase + 5);
                    indices.push_back(aBase + 2); indices.push_back(aBase + 5);
                };

                // Directional 3D arrow visualizers along the link:
                if (!targetHasReverse) {
                    // One-way link: draw 2 forward-pointing 3D arrows along path (at 35% and 70%)
                    DrawArrowHead(startPt + (endPt - startPt) * 0.35f, fwd, side, 13.0f, 6.0f, lr, lg, lb, 1.0f);
                    DrawArrowHead(startPt + (endPt - startPt) * 0.70f, fwd, side, 13.0f, 6.0f, lr, lg, lb, 1.0f);
                } else {
                    // Two-way link: draw dual opposing 3D arrows (forward at 65%, backward at 35%)
                    DrawArrowHead(startPt + (endPt - startPt) * 0.65f, fwd, side, 12.0f, 5.5f, lr, lg, lb, 1.0f);
                    DrawArrowHead(startPt + (endPt - startPt) * 0.35f, Vector3(-fwd.x, -fwd.y, -fwd.z), side, 12.0f, 5.5f, lr, lg, lb, 1.0f);

                    // Midpoint diamond
                    Vector3 mid = (startPt + endPt) * 0.5f;
                    Vector3 dFwd = mid + fwd * 6.0f;
                    Vector3 dBack = mid - fwd * 6.0f;
                    Vector3 dRight = mid + side * 4.5f;
                    Vector3 dLeft = mid - side * 4.5f;
                    uint32_t dBase = static_cast<uint32_t>(vertices.size());
                    vertices.push_back({ dFwd.x, dFwd.y, dFwd.z, 0,0,1, 0,0, lr, lg, lb, 0.90f });
                    vertices.push_back({ dRight.x, dRight.y, dRight.z, 0,0,1, 0,0, lr, lg, lb, 0.90f });
                    vertices.push_back({ dBack.x, dBack.y, dBack.z, 0,0,1, 0,0, lr, lg, lb, 0.90f });
                    vertices.push_back({ dLeft.x, dLeft.y, dLeft.z, 0,0,1, 0,0, lr, lg, lb, 0.90f });
                    indices.push_back(dBase + 0); indices.push_back(dBase + 1);
                    indices.push_back(dBase + 1); indices.push_back(dBase + 2);
                    indices.push_back(dBase + 2); indices.push_back(dBase + 3);
                    indices.push_back(dBase + 3); indices.push_back(dBase + 0);
                }

                // If crouch connection, draw crawl clearance frame portal at midpoint
                if (node.connectionFlags[c] & WPT_CONN_CROUCH) {
                    Vector3 mid = (startPt + endPt) * 0.5f;
                    Vector3 pBL = mid - side * 14.0f;
                    Vector3 pBR = mid + side * 14.0f;
                    Vector3 pTL = pBL + Vector3(0.0f, 0.0f, 28.0f);
                    Vector3 pTR = pBR + Vector3(0.0f, 0.0f, 28.0f);
                    uint32_t pBase = static_cast<uint32_t>(vertices.size());
                    vertices.push_back({ pBL.x, pBL.y, pBL.z, 0,0,1, 0,0, 1.0f, 0.60f, 0.05f, 0.70f });
                    vertices.push_back({ pTL.x, pTL.y, pTL.z, 0,0,1, 0,0, 1.0f, 0.60f, 0.05f, 0.70f });
                    vertices.push_back({ pTR.x, pTR.y, pTR.z, 0,0,1, 0,0, 1.0f, 0.60f, 0.05f, 0.70f });
                    vertices.push_back({ pBR.x, pBR.y, pBR.z, 0,0,1, 0,0, 1.0f, 0.60f, 0.05f, 0.70f });
                    indices.push_back(pBase + 0); indices.push_back(pBase + 1);
                    indices.push_back(pBase + 1); indices.push_back(pBase + 2);
                    indices.push_back(pBase + 2); indices.push_back(pBase + 3);
                }
            }
        }
    }

    // Continuous Pen / Breadcrumb Tool rubberband preview line
    if (penPreviewStart && penPreviewEnd) {
        float prR = penPreviewClear ? 0.1f : 1.0f;
        float prG = penPreviewClear ? 1.0f : 0.2f;
        float prB = 0.1f;
        uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ penPreviewStart->x, penPreviewStart->y, penPreviewStart->z + 18.0f, 0,0,1, 0,0, prR, prG, prB, 1.0f });
        vertices.push_back({ penPreviewEnd->x, penPreviewEnd->y, penPreviewEnd->z + 18.0f, 0,0,1, 0,0, prR, prG, prB, 1.0f });
        indices.push_back(base + 0); indices.push_back(base + 1);

        // Ground landing crosshair at preview end
        float crossR = 12.0f;
        uint32_t cBase = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ penPreviewEnd->x - crossR, penPreviewEnd->y, penPreviewEnd->z + 2.0f, 0,0,1, 0,0, prR, prG, prB, 0.9f });
        vertices.push_back({ penPreviewEnd->x + crossR, penPreviewEnd->y, penPreviewEnd->z + 2.0f, 0,0,1, 0,0, prR, prG, prB, 0.9f });
        vertices.push_back({ penPreviewEnd->x, penPreviewEnd->y - crossR, penPreviewEnd->z + 2.0f, 0,0,1, 0,0, prR, prG, prB, 0.9f });
        vertices.push_back({ penPreviewEnd->x, penPreviewEnd->y + crossR, penPreviewEnd->z + 2.0f, 0,0,1, 0,0, prR, prG, prB, 0.9f });
        indices.push_back(cBase + 0); indices.push_back(cBase + 1);
        indices.push_back(cBase + 2); indices.push_back(cBase + 3);
    }

    // Ghost Bot simulated path trail
    if (ghostBotPath && ghostBotPath->size() >= 2) {
        for (size_t i = 0; i + 1 < ghostBotPath->size(); ++i) {
            const WaypointNode* pA = graph.GetNodeByID((*ghostBotPath)[i]);
            const WaypointNode* pB = graph.GetNodeByID((*ghostBotPath)[i + 1]);
            if (pA && pB) {
                uint32_t pBase = static_cast<uint32_t>(vertices.size());
                // Glowing cyan / electric blue trail elevated above ground
                vertices.push_back({ pA->origin.x, pA->origin.y, pA->origin.z + 16.0f, 0,0,1, 0,0, 0.0f, 0.95f, 1.0f, 1.0f });
                vertices.push_back({ pB->origin.x, pB->origin.y, pB->origin.z + 16.0f, 0,0,1, 0,0, 0.0f, 0.95f, 1.0f, 1.0f });
                indices.push_back(pBase + 0); indices.push_back(pBase + 1);
            }
        }
    }

    // Ghost Bot animated player avatar
    if (ghostBotPos) {
        Vector3 botPos = *ghostBotPos;
        float botR = 16.0f;
        float botH = 72.0f;
        const int botSegs = 12;

        for (int i = 0; i < botSegs; ++i) {
            float ang = (float)i * 2.0f * (float)M_PI / (float)botSegs;
            float nextAng = (float)(i + 1) * 2.0f * (float)M_PI / (float)botSegs;
            float cx = std::cos(ang) * botR;
            float cy = std::sin(ang) * botR;
            float ncx = std::cos(nextAng) * botR;
            float ncy = std::sin(nextAng) * botR;

            // Feet ring
            uint32_t segBase = static_cast<uint32_t>(vertices.size());
            vertices.push_back({ botPos.x + cx, botPos.y + cy, botPos.z + 2.0f, 0,0,1, 0,0, 0.1f, 0.9f, 1.0f, 0.95f });
            vertices.push_back({ botPos.x + ncx, botPos.y + ncy, botPos.z + 2.0f, 0,0,1, 0,0, 0.1f, 0.9f, 1.0f, 0.95f });
            indices.push_back(segBase + 0); indices.push_back(segBase + 1);

            // Waist ring
            segBase = static_cast<uint32_t>(vertices.size());
            vertices.push_back({ botPos.x + cx, botPos.y + cy, botPos.z + 36.0f, 0,0,1, 0,0, 0.1f, 0.9f, 1.0f, 0.75f });
            vertices.push_back({ botPos.x + ncx, botPos.y + ncy, botPos.z + 36.0f, 0,0,1, 0,0, 0.1f, 0.9f, 1.0f, 0.75f });
            indices.push_back(segBase + 0); indices.push_back(segBase + 1);

            // Head ring
            segBase = static_cast<uint32_t>(vertices.size());
            vertices.push_back({ botPos.x + cx * 0.7f, botPos.y + cy * 0.7f, botPos.z + botH, 0,0,1, 0,0, 1.0f, 0.95f, 0.2f, 1.0f });
            vertices.push_back({ botPos.x + ncx * 0.7f, botPos.y + ncy * 0.7f, botPos.z + botH, 0,0,1, 0,0, 1.0f, 0.95f, 0.2f, 1.0f });
            indices.push_back(segBase + 0); indices.push_back(segBase + 1);

            // Vertical rib lines
            if (i % 3 == 0) {
                segBase = static_cast<uint32_t>(vertices.size());
                vertices.push_back({ botPos.x + cx, botPos.y + cy, botPos.z + 2.0f, 0,0,1, 0,0, 0.1f, 0.9f, 1.0f, 0.8f });
                vertices.push_back({ botPos.x + cx, botPos.y + cy, botPos.z + botH, 0,0,1, 0,0, 0.1f, 0.9f, 1.0f, 0.8f });
                indices.push_back(segBase + 0); indices.push_back(segBase + 1);
            }
        }

        // Forward gaze line / crosshair (using ghostBotYaw)
        float yawRad = ghostBotYaw * (float)M_PI / 180.0f;
        Vector3 fwdDir(std::cos(yawRad), std::sin(yawRad), 0.0f);
        Vector3 eyePos = botPos + Vector3(0, 0, botH - 8.0f);
        Vector3 gazeTarget = eyePos + fwdDir * 32.0f;
        uint32_t gBase = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ eyePos.x, eyePos.y, eyePos.z, 0,0,1, 0,0, 1.0f, 1.0f, 0.2f, 1.0f });
        vertices.push_back({ gazeTarget.x, gazeTarget.y, gazeTarget.z, 0,0,1, 0,0, 1.0f, 1.0f, 0.2f, 1.0f });
        indices.push_back(gBase + 0); indices.push_back(gBase + 1);
    }

    if (indices.empty()) return false;

    glGenVertexArrays(1, &m_lineVAO);
    glGenBuffers(1, &m_lineVBO);
    glGenBuffers(1, &m_lineEBO);

    glBindVertexArray(m_lineVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(WaypointVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lineEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaypointVertex), (void*)offsetof(WaypointVertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(WaypointVertex), (void*)offsetof(WaypointVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    m_lineIndexCount = indices.size();
    m_loaded = true;

    return true;
}

void WaypointRenderer::Render(const Shader& lineShader, const Matrix4& mvp) {
    if (!m_loaded || !m_showWaypoints || m_lineIndexCount == 0) return;

    lineShader.Bind();
    lineShader.SetMat4("u_MVP", mvp);
    lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

    glLineWidth(2.0f);
    glBindVertexArray(m_lineVAO);
    glDrawElements(GL_LINES, static_cast<GLsizei>(m_lineIndexCount), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    lineShader.Unbind();
}
