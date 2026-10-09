#include "editor/render/nav_renderer.h"

NavRenderer::NavRenderer()
    : m_vao(0)
    , m_vbo(0)
    , m_ebo(0)
    , m_indexCount(0)
    , m_lineVao(0)
    , m_lineVbo(0)
    , m_lineEbo(0)
    , m_lineIndexCount(0)
    , m_loaded(false)
    , m_areaCount(0)
{
}

NavRenderer::~NavRenderer() {
    Clear();
}

void NavRenderer::Clear() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo != 0) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    m_indexCount = 0;

    if (m_lineVao != 0) { glDeleteVertexArrays(1, &m_lineVao); m_lineVao = 0; }
    if (m_lineVbo != 0) { glDeleteBuffers(1, &m_lineVbo); m_lineVbo = 0; }
    if (m_lineEbo != 0) { glDeleteBuffers(1, &m_lineEbo); m_lineEbo = 0; }
    m_lineIndexCount = 0;

    m_loaded = false;
    m_areaCount = 0;
}

bool NavRenderer::BuildFromNav(const NavMesh& nav, uint32_t selectedId, uint32_t hoveredId,
                               uint32_t connectTargetId, int transformAxis,
                               SelectedHandleType hoveredHandle,
                               SelectedHandleType selectedHandle,
                               const std::vector<uint32_t>* selectedIds,
                               uint32_t selectedLadderId,
                               uint32_t diagnosticAreaId) {
    Clear();
    if (!nav.IsLoaded()) return false;

    m_invalidConnectionCount = 0;
    const auto& areas = nav.GetAreas();
    m_areaCount = areas.size();
    if (areas.empty()) return false;

    std::vector<NavVertex> quadVertices;
    std::vector<uint32_t> quadIndices;

    std::vector<NavVertex> lineVertices;
    std::vector<uint32_t> lineIndices;

    const float kZLift = 2.0f; // Elevate above floor to prevent surface overlap

    for (const NavArea* area : areas) {
        if (!area) continue;

        uint32_t id = area->GetID();
        bool isSelected = (id == selectedId);
        if (!isSelected && selectedIds) {
            for (uint32_t sid : *selectedIds) {
                if (sid == id) {
                    isSelected = true;
                    break;
                }
            }
        }
        bool isHovered = (id == hoveredId);

        // Determine area color based on selection, attributes, and island clustering
        float r = 0.15f, g = 0.82f, b = 0.38f, a = 0.70f; // Normal: High-contrast green
        float lr = 0.30f, lg = 1.0f, lb = 0.50f, la = 1.0f;

        if (isSelected) {
            r = 1.0f; g = 0.88f; b = 0.10f; a = 0.90f; // Selected: Glowing gold
            lr = 1.0f; lg = 1.0f; lb = 0.30f; la = 1.0f;
        } else if (isHovered) {
            r = 0.05f; g = 0.92f; b = 1.0f; a = 0.80f; // Hovered: Electric cyan
            lr = 0.50f; lg = 1.0f; lb = 1.0f; la = 1.0f;
        } else if (m_showIslandColors && !m_areaClusterMap.empty()) {
            auto it = m_areaClusterMap.find(id);
            int clusterIdx = (it != m_areaClusterMap.end()) ? it->second : 0;
            static const float palette[8][3] = {
                { 0.15f, 0.82f, 0.38f }, // 0: Main (Green)
                { 1.00f, 0.50f, 0.05f }, // 1: Orange
                { 0.95f, 0.15f, 0.75f }, // 2: Magenta
                { 0.10f, 0.75f, 1.00f }, // 3: Blue
                { 0.95f, 0.85f, 0.10f }, // 4: Yellow
                { 0.65f, 0.25f, 1.00f }, // 5: Purple
                { 1.00f, 0.30f, 0.30f }, // 6: Red
                { 0.10f, 0.95f, 0.80f }  // 7: Teal
            };
            int c = clusterIdx % 8;
            r = palette[c][0]; g = palette[c][1]; b = palette[c][2]; a = 0.75f;
            lr = std::min(1.0f, r + 0.25f);
            lg = std::min(1.0f, g + 0.25f);
            lb = std::min(1.0f, b + 0.25f);
            la = 1.0f;
        } else if (area->HasAttributes(NAV_ATTR_CROUCH)) {
            r = 0.18f; g = 0.52f; b = 1.0f; a = 0.72f; // Crouch: Deep sky blue
            lr = 0.40f; lg = 0.75f; lb = 1.0f; la = 1.0f;
        } else if (area->HasAttributes(NAV_ATTR_JUMP)) {
            r = 1.0f; g = 0.58f; b = 0.12f; a = 0.72f; // Jump: Bright amber
            lr = 1.0f; lg = 0.75f; lb = 0.25f; la = 1.0f;
        } else if (area->HasAttributes(NAV_ATTR_NO_JUMP)) {
            r = 0.92f; g = 0.22f; b = 0.22f; a = 0.72f; // No Jump: Vivid red
            lr = 1.0f; lg = 0.40f; lb = 0.40f; la = 1.0f;
        }

        Vector3 cNW = area->GetCorner(NAV_CORNER_NORTH_WEST);
        Vector3 cNE = area->GetCorner(NAV_CORNER_NORTH_EAST);
        Vector3 cSE = area->GetCorner(NAV_CORNER_SOUTH_EAST);
        Vector3 cSW = area->GetCorner(NAV_CORNER_SOUTH_WEST);

        cNW.z += kZLift;
        cNE.z += kZLift;
        cSE.z += kZLift;
        cSW.z += kZLift;

        Vector3 norm(0.0f, 0.0f, 1.0f);

        uint32_t baseVert = static_cast<uint32_t>(quadVertices.size());

        NavVertex v0 = { cNW.x, cNW.y, cNW.z, norm.x, norm.y, norm.z, 0.0f, 0.0f, r, g, b, a };
        NavVertex v1 = { cNE.x, cNE.y, cNE.z, norm.x, norm.y, norm.z, 1.0f, 0.0f, r, g, b, a };
        NavVertex v2 = { cSE.x, cSE.y, cSE.z, norm.x, norm.y, norm.z, 1.0f, 1.0f, r, g, b, a };
        NavVertex v3 = { cSW.x, cSW.y, cSW.z, norm.x, norm.y, norm.z, 0.0f, 1.0f, r, g, b, a };

        quadVertices.push_back(v0);
        quadVertices.push_back(v1);
        quadVertices.push_back(v2);
        quadVertices.push_back(v3);

        // Quad triangles: (0, 1, 2) and (0, 2, 3)
        quadIndices.push_back(baseVert + 0);
        quadIndices.push_back(baseVert + 1);
        quadIndices.push_back(baseVert + 2);
        quadIndices.push_back(baseVert + 0);
        quadIndices.push_back(baseVert + 2);
        quadIndices.push_back(baseVert + 3);

        // Area border lines elevated slightly above quads
        float lineLift = isSelected ? 1.0f : 0.6f;
        uint32_t baseLineVert = static_cast<uint32_t>(lineVertices.size());
        lineVertices.push_back({ cNW.x, cNW.y, cNW.z + lineLift, 0,0,1, 0,0, lr, lg, lb, la });
        lineVertices.push_back({ cNE.x, cNE.y, cNE.z + lineLift, 0,0,1, 0,0, lr, lg, lb, la });
        lineVertices.push_back({ cSE.x, cSE.y, cSE.z + lineLift, 0,0,1, 0,0, lr, lg, lb, la });
        lineVertices.push_back({ cSW.x, cSW.y, cSW.z + lineLift, 0,0,1, 0,0, lr, lg, lb, la });

        // Edge 0: North (NW -> NE)
        lineIndices.push_back(baseLineVert + 0);
        lineIndices.push_back(baseLineVert + 1);
        // Edge 1: East (NE -> SE)
        lineIndices.push_back(baseLineVert + 1);
        lineIndices.push_back(baseLineVert + 2);
        // Edge 2: South (SE -> SW)
        lineIndices.push_back(baseLineVert + 2);
        lineIndices.push_back(baseLineVert + 3);
        // Edge 3: West (SW -> NW)
        lineIndices.push_back(baseLineVert + 3);
        lineIndices.push_back(baseLineVert + 0);

        // Specific Edge Highlight for Selected Area (Hammer style)
        if (isSelected) {
            auto AddEdgeHighlight = [&](const Vector3& p1, const Vector3& p2) {
                float hlLift = lineLift + 0.8f;
                uint32_t hEdgeIdx = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ p1.x, p1.y, p1.z + hlLift, 0,0,1, 0,0, 1.0f, 0.92f, 0.15f, 1.0f });
                lineVertices.push_back({ p2.x, p2.y, p2.z + hlLift, 0,0,1, 0,0, 1.0f, 0.92f, 0.15f, 1.0f });
                lineIndices.push_back(hEdgeIdx + 0); lineIndices.push_back(hEdgeIdx + 1);

                // Midpoint marker notch
                Vector3 mid = (p1 + p2) * 0.5f;
                Vector3 perp(-(p2.y - p1.y), p2.x - p1.x, 0.0f);
                float pLen = perp.Length();
                if (pLen > 1e-3f) {
                    perp = perp * (6.0f / pLen);
                    uint32_t notchIdx = static_cast<uint32_t>(lineVertices.size());
                    lineVertices.push_back({ mid.x - perp.x, mid.y - perp.y, mid.z + hlLift, 0,0,1, 0,0, 1.0f, 0.95f, 0.2f, 1.0f });
                    lineVertices.push_back({ mid.x + perp.x, mid.y + perp.y, mid.z + hlLift, 0,0,1, 0,0, 1.0f, 0.95f, 0.2f, 1.0f });
                    lineIndices.push_back(notchIdx + 0); lineIndices.push_back(notchIdx + 1);
                }
            };

            if (hoveredHandle == HANDLE_EDGE_NORTH || selectedHandle == HANDLE_EDGE_NORTH) {
                AddEdgeHighlight(cNW, cNE);
            }
            if (hoveredHandle == HANDLE_EDGE_EAST || selectedHandle == HANDLE_EDGE_EAST) {
                AddEdgeHighlight(cNE, cSE);
            }
            if (hoveredHandle == HANDLE_EDGE_SOUTH || selectedHandle == HANDLE_EDGE_SOUTH) {
                AddEdgeHighlight(cSW, cSE);
            }
            if (hoveredHandle == HANDLE_EDGE_WEST || selectedHandle == HANDLE_EDGE_WEST) {
                AddEdgeHighlight(cNW, cSW);
            }
        }

        // Connection lines between area centroids with dual-lane offset & clear contrast
        Vector3 centerA = area->GetCenter();
        bool isSelArea = (area->GetID() == selectedId);

        for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
            const auto& connects = area->GetAdjacentList(static_cast<NavDirType>(d));
            for (const auto& conn : connects) {
                const NavArea* target = conn.area;
                if (!target) continue;

                Vector3 centerB = target->GetCenter();
                Vector3 delta = centerB - centerA;
                float dist = delta.Length();
                if (dist < 1.0f) continue;

                bool isTargetSelected = (target->GetID() == selectedId);
                bool isTwoWay = target->IsConnected(area);

                Vector3 fwd = delta * (1.0f / dist);
                Vector3 lateral(-fwd.y, fwd.x, 0.0f);
                float laneOffset = isTwoWay ? 3.5f : 0.0f;

                float zLiftTotal = kZLift + 2.0f;
                float cr, cg, cb, ca;

                float deltaZ = centerB.z - centerA.z;
                bool isInvalidStep = false;
                if (m_showConnectionValidity) {
                    if (deltaZ > m_maxStepHeight && !(area->GetAttributes() & NAV_ATTR_JUMP)) {
                        isInvalidStep = true;
                    } else if (deltaZ < -200.0f) {
                        isInvalidStep = true;
                    }
                }

                bool isConnSelected = (m_selConnFrom == area->GetID() && m_selConnTo == target->GetID()) ||
                                      (m_selConnFrom == target->GetID() && m_selConnTo == area->GetID() && isTwoWay);

                if (isConnSelected) {
                    // Selected Connection: Brilliant Radiant Gold / Amber
                    cr = 1.0f; cg = 0.90f; cb = 0.10f; ca = 1.0f;
                    zLiftTotal += 5.0f;
                } else if (isInvalidStep) {
                    m_invalidConnectionCount++;
                    // Impassable invalid step: glowing crimson red
                    cr = 1.0f; cg = 0.15f; cb = 0.15f; ca = 1.0f;
                    zLiftTotal += 3.0f;
                } else if (isSelArea || isTargetSelected) {
                    zLiftTotal += 2.0f; // Elevate active connections
                    if (isTwoWay) {
                        // Brilliant Electric Cyan for active two-way
                        cr = 0.0f; cg = 0.96f; cb = 1.0f; ca = 1.0f;
                    } else if (isSelArea) {
                        // Luminous Amber / Gold for active outgoing
                        cr = 1.0f; cg = 0.78f; cb = 0.0f; ca = 1.0f;
                    } else {
                        // Vibrant Spring Green for active incoming
                        cr = 0.0f; cg = 1.0f; cb = 0.50f; ca = 1.0f;
                    }
                } else {
                    if (isTwoWay) {
                        // Clean Sky Blue for passive two-way
                        cr = 0.25f; cg = 0.58f; cb = 0.96f; ca = 0.65f;
                    } else {
                        // Clean Coral Rose for passive one-way
                        cr = 0.98f; cg = 0.40f; cb = 0.48f; ca = 0.75f;
                    }
                }

                Vector3 start = centerA + lateral * laneOffset;
                Vector3 end = centerB + lateral * laneOffset;
                start.z += zLiftTotal;
                end.z += zLiftTotal;

                uint32_t cIdx = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ start.x, start.y, start.z, 0,0,1, 0,0, cr, cg, cb, ca });
                lineVertices.push_back({ end.x, end.y, end.z, 0,0,1, 0,0, cr, cg, cb, ca });

                lineIndices.push_back(cIdx);
                lineIndices.push_back(cIdx + 1);

                if (isConnSelected) {
                    // Radiant parallel accent lines to make selected connection visually bold and unmistakable
                    for (float latOff : { -1.8f, 1.8f, -3.6f, 3.6f }) {
                        Vector3 s2 = start + lateral * latOff;
                        Vector3 e2 = end + lateral * latOff;
                        uint32_t offIdx = static_cast<uint32_t>(lineVertices.size());
                        lineVertices.push_back({ s2.x, s2.y, s2.z, 0,0,1, 0,0, cr, cg, cb, 0.90f });
                        lineVertices.push_back({ e2.x, e2.y, e2.z, 0,0,1, 0,0, cr, cg, cb, 0.90f });
                        lineIndices.push_back(offIdx);
                        lineIndices.push_back(offIdx + 1);
                    }
                    // Midpoint diamond indicator
                    Vector3 midPt = (start + end) * 0.5f;
                    float dRad = 6.0f;
                    Vector3 dN = midPt + fwd * dRad;
                    Vector3 dS = midPt - fwd * dRad;
                    Vector3 dE = midPt + lateral * dRad;
                    Vector3 dW = midPt - lateral * dRad;
                    uint32_t mIdx = static_cast<uint32_t>(lineVertices.size());
                    lineVertices.push_back({ dN.x, dN.y, dN.z, 0,0,1, 0,0, 1.0f, 1.0f, 0.4f, 1.0f });
                    lineVertices.push_back({ dE.x, dE.y, dE.z, 0,0,1, 0,0, 1.0f, 1.0f, 0.4f, 1.0f });
                    lineVertices.push_back({ dS.x, dS.y, dS.z, 0,0,1, 0,0, 1.0f, 1.0f, 0.4f, 1.0f });
                    lineVertices.push_back({ dW.x, dW.y, dW.z, 0,0,1, 0,0, 1.0f, 1.0f, 0.4f, 1.0f });
                    lineIndices.push_back(mIdx + 0); lineIndices.push_back(mIdx + 1);
                    lineIndices.push_back(mIdx + 1); lineIndices.push_back(mIdx + 2);
                    lineIndices.push_back(mIdx + 2); lineIndices.push_back(mIdx + 3);
                    lineIndices.push_back(mIdx + 3); lineIndices.push_back(mIdx + 0);
                }

                // Add directional arrowhead along this lane
                if (dist > 18.0f) {
                    Vector3 tip = start + fwd * (dist * 0.75f);
                    Vector3 leftBar = tip - fwd * 8.0f + lateral * 4.5f;
                    Vector3 rightBar = tip - fwd * 8.0f - lateral * 4.5f;

                    uint32_t aIdx = static_cast<uint32_t>(lineVertices.size());
                    lineVertices.push_back({ tip.x, tip.y, tip.z, 0,0,1, 0,0, cr, cg, cb, std::min(1.0f, ca + 0.15f) });
                    lineVertices.push_back({ leftBar.x, leftBar.y, leftBar.z, 0,0,1, 0,0, cr, cg, cb, std::min(1.0f, ca + 0.15f) });
                    lineVertices.push_back({ tip.x, tip.y, tip.z, 0,0,1, 0,0, cr, cg, cb, std::min(1.0f, ca + 0.15f) });
                    lineVertices.push_back({ rightBar.x, rightBar.y, rightBar.z, 0,0,1, 0,0, cr, cg, cb, std::min(1.0f, ca + 0.15f) });

                    lineIndices.push_back(aIdx + 0);
                    lineIndices.push_back(aIdx + 1);
                    lineIndices.push_back(aIdx + 2);
                    lineIndices.push_back(aIdx + 3);

                    // For one-way connections, add a second chevron to make the arrow unmistakably directional
                    if (!isTwoWay && dist > 32.0f) {
                        Vector3 tip2 = tip - fwd * 7.0f;
                        Vector3 leftBar2 = tip2 - fwd * 6.0f + lateral * 3.5f;
                        Vector3 rightBar2 = tip2 - fwd * 6.0f - lateral * 3.5f;

                        uint32_t aIdx2 = static_cast<uint32_t>(lineVertices.size());
                        lineVertices.push_back({ tip2.x, tip2.y, tip2.z, 0,0,1, 0,0, cr, cg, cb, std::min(1.0f, ca + 0.15f) });
                        lineVertices.push_back({ leftBar2.x, leftBar2.y, leftBar2.z, 0,0,1, 0,0, cr, cg, cb, std::min(1.0f, ca + 0.15f) });
                        lineVertices.push_back({ tip2.x, tip2.y, tip2.z, 0,0,1, 0,0, cr, cg, cb, std::min(1.0f, ca + 0.15f) });
                        lineVertices.push_back({ rightBar2.x, rightBar2.y, rightBar2.z, 0,0,1, 0,0, cr, cg, cb, std::min(1.0f, ca + 0.15f) });

                        lineIndices.push_back(aIdx2 + 0);
                        lineIndices.push_back(aIdx2 + 1);
                        lineIndices.push_back(aIdx2 + 2);
                        lineIndices.push_back(aIdx2 + 3);
                    }

                    // Drop-off ledge indicator for non-climbable drops
                    if (!isTwoWay && deltaZ < -32.0f) {
                        Vector3 dropPt = start + fwd * 6.0f;
                        uint32_t dIdx = static_cast<uint32_t>(lineVertices.size());
                        lineVertices.push_back({ dropPt.x, dropPt.y, dropPt.z, 0,0,1, 0,0, 1.0f, 0.45f, 0.1f, 1.0f });
                        lineVertices.push_back({ dropPt.x, dropPt.y, dropPt.z - 12.0f, 0,0,1, 0,0, 1.0f, 0.45f, 0.1f, 1.0f });
                        lineIndices.push_back(dIdx + 0);
                        lineIndices.push_back(dIdx + 1);
                    }
                }
            }
        }
    }

    // Connect Mode interactive preview line between selected and candidate area
    if (selectedId != 0 && connectTargetId != 0 && selectedId != connectTargetId) {
        const NavArea* sel = nav.GetAreaByID(selectedId);
        const NavArea* tgt = nav.GetAreaByID(connectTargetId);
        if (sel && tgt) {
            Vector3 pA = sel->GetCenter(); pA.z += (kZLift + 6.0f);
            Vector3 pB = tgt->GetCenter(); pB.z += (kZLift + 6.0f);

            uint32_t pIdx = static_cast<uint32_t>(lineVertices.size());
            lineVertices.push_back({ pA.x, pA.y, pA.z, 0,0,1, 0,0, 1.0f, 0.95f, 0.2f, 1.0f });
            lineVertices.push_back({ pB.x, pB.y, pB.z, 0,0,1, 0,0, 1.0f, 0.95f, 0.2f, 1.0f });
            lineIndices.push_back(pIdx);
            lineIndices.push_back(pIdx + 1);
        }
    }

    // 4 Corner vertex handles for selected area
    if (selectedId != 0) {
        const NavArea* sel = nav.GetAreaByID(selectedId);
        if (sel) {
            Vector3 c = sel->GetCenter();
            c.z += (kZLift + 3.0f);

            // 4 Corner vertex handles
            SelectedHandleType cornerHandles[4] = {
                HANDLE_CORNER_NW,
                HANDLE_CORNER_NE,
                HANDLE_CORNER_SE,
                HANDLE_CORNER_SW
            };

            for (int k = 0; k < 4; ++k) {
                Vector3 cp = sel->GetCorner(static_cast<NavCornerType>(k));
                cp.z += (kZLift + 1.2f);
                bool isCornerActive = (hoveredHandle == cornerHandles[k] || selectedHandle == cornerHandles[k]);
                float hSize = isCornerActive ? 7.0f : 5.0f;
                float hr = isCornerActive ? 1.0f : 1.0f;
                float hg = isCornerActive ? 1.0f : 0.85f;
                float hb = isCornerActive ? 0.2f : 0.2f;

                uint32_t hIdx = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ cp.x - hSize, cp.y, cp.z, 0,0,1, 0,0, hr, hg, hb, 1.0f });
                lineVertices.push_back({ cp.x + hSize, cp.y, cp.z, 0,0,1, 0,0, hr, hg, hb, 1.0f });
                lineVertices.push_back({ cp.x, cp.y - hSize, cp.z, 0,0,1, 0,0, hr, hg, hb, 1.0f });
                lineVertices.push_back({ cp.x, cp.y + hSize, cp.z, 0,0,1, 0,0, hr, hg, hb, 1.0f });
                lineIndices.push_back(hIdx + 0); lineIndices.push_back(hIdx + 1);
                lineIndices.push_back(hIdx + 2); lineIndices.push_back(hIdx + 3);
            }

            // Infinite axis guideline if axis constraint is active
            if (transformAxis == 1) { // X axis
                uint32_t ax = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ c.x - 4000.0f, c.y, c.z, 0,0,1, 0,0, 1.0f, 0.2f, 0.2f, 0.85f });
                lineVertices.push_back({ c.x + 4000.0f, c.y, c.z, 0,0,1, 0,0, 1.0f, 0.2f, 0.2f, 0.85f });
                lineIndices.push_back(ax); lineIndices.push_back(ax + 1);
            } else if (transformAxis == 2) { // Y axis
                uint32_t ay = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ c.x, c.y - 4000.0f, c.z, 0,0,1, 0,0, 0.2f, 1.0f, 0.3f, 0.85f });
                lineVertices.push_back({ c.x, c.y + 4000.0f, c.z, 0,0,1, 0,0, 0.2f, 1.0f, 0.3f, 0.85f });
                lineIndices.push_back(ay); lineIndices.push_back(ay + 1);
            } else if (transformAxis == 3) { // Z axis
                uint32_t az = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ c.x, c.y, c.z - 4000.0f, 0,0,1, 0,0, 0.2f, 0.6f, 1.0f, 0.85f });
                lineVertices.push_back({ c.x, c.y, c.z + 4000.0f, 0,0,1, 0,0, 0.2f, 0.6f, 1.0f, 0.85f });
                lineIndices.push_back(az); lineIndices.push_back(az + 1);
            }
        }
    }

    // Tactical Hiding Spots & Sniper Points rendering
    if (m_showHidingSpots) {
        for (const NavArea* a : areas) {
            if (!a) continue;
            for (const NavHidingSpot& spot : a->GetHidingSpots()) {
                float hr = 0.2f, hg = 0.9f, hb = 0.2f, ha = 0.95f; // Default Cover: green
                if (spot.flags & NAV_HIDING_GOOD_SNIPER_SPOT) {
                    hr = 0.2f; hg = 0.6f; hb = 1.0f; // Blue
                }
                if (spot.flags & NAV_HIDING_IDEAL_SNIPER) {
                    hr = 0.85f; hg = 0.2f; hb = 0.9f; // Magenta
                }
                if (spot.flags & NAV_HIDING_EXPOSED) {
                    hr = 1.0f; hg = 0.55f; hb = 0.1f; // Orange
                }

                Vector3 p = spot.pos + Vector3(0, 0, 10.0f);
                float sz = 6.0f;
                uint32_t bIdx = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ p.x - sz, p.y, p.z, 0,0,1, 0,0, hr, hg, hb, ha });
                lineVertices.push_back({ p.x + sz, p.y, p.z, 0,0,1, 0,0, hr, hg, hb, ha });
                lineVertices.push_back({ p.x, p.y - sz, p.z, 0,0,1, 0,0, hr, hg, hb, ha });
                lineVertices.push_back({ p.x, p.y + sz, p.z, 0,0,1, 0,0, hr, hg, hb, ha });
                lineVertices.push_back({ p.x, p.y, p.z - sz, 0,0,1, 0,0, hr, hg, hb, ha });
                lineVertices.push_back({ p.x, p.y, p.z + sz, 0,0,1, 0,0, hr, hg, hb, ha });

                lineIndices.push_back(bIdx + 0); lineIndices.push_back(bIdx + 1);
                lineIndices.push_back(bIdx + 2); lineIndices.push_back(bIdx + 3);
                lineIndices.push_back(bIdx + 4); lineIndices.push_back(bIdx + 5);

                // Ground peg down to spot.pos
                uint32_t pegIdx = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ spot.pos.x, spot.pos.y, spot.pos.z, 0,0,1, 0,0, hr, hg, hb, 0.5f });
                lineIndices.push_back(bIdx + 4);
                lineIndices.push_back(pegIdx);
            }
        }
    }

    // Diagnostic Highlight Box
    if (diagnosticAreaId != 0) {
        const NavArea* diagArea = nav.GetAreaByID(diagnosticAreaId);
        if (diagArea) {
            const NavExtent& ext = diagArea->GetExtent();
            uint32_t dIdx = static_cast<uint32_t>(lineVertices.size());
            float dr = 1.0f, dg = 0.15f, db = 0.15f, da = 1.0f;
            float zTop = std::max(ext.hi.z, diagArea->GetNEZ()) + 12.0f;
            lineVertices.push_back({ ext.lo.x - 4, ext.lo.y - 4, zTop, 0,0,1, 0,0, dr, dg, db, da });
            lineVertices.push_back({ ext.hi.x + 4, ext.lo.y - 4, zTop, 0,0,1, 0,0, dr, dg, db, da });
            lineVertices.push_back({ ext.hi.x + 4, ext.hi.y + 4, zTop, 0,0,1, 0,0, dr, dg, db, da });
            lineVertices.push_back({ ext.lo.x - 4, ext.hi.y + 4, zTop, 0,0,1, 0,0, dr, dg, db, da });

            lineIndices.push_back(dIdx + 0); lineIndices.push_back(dIdx + 1);
            lineIndices.push_back(dIdx + 1); lineIndices.push_back(dIdx + 2);
            lineIndices.push_back(dIdx + 2); lineIndices.push_back(dIdx + 3);
            lineIndices.push_back(dIdx + 3); lineIndices.push_back(dIdx + 0);
        }
    }

    // Ladder rendering with rungs
    for (const NavLadder* ladder : nav.GetLadders()) {
        if (!ladder) continue;
        Vector3 top = ladder->top;
        Vector3 bottom = ladder->bottom;
        float halfW = ladder->width * 0.5f;

        Vector3 normal(0, 1, 0);
        if (ladder->dir == NAV_DIR_NORTH) normal = Vector3(0, 1, 0);
        else if (ladder->dir == NAV_DIR_SOUTH) normal = Vector3(0, -1, 0);
        else if (ladder->dir == NAV_DIR_EAST) normal = Vector3(1, 0, 0);
        else if (ladder->dir == NAV_DIR_WEST) normal = Vector3(-1, 0, 0);

        Vector3 sideDir = normal.Cross(Vector3(0, 0, 1)).Normalized();

        Vector3 p0 = bottom - sideDir * halfW;
        Vector3 p1 = bottom + sideDir * halfW;
        Vector3 p2 = top + sideDir * halfW;
        Vector3 p3 = top - sideDir * halfW;

        uint32_t baseL = static_cast<uint32_t>(lineVertices.size());
        bool isSelLadder = (selectedLadderId != 0 && ladder->id == selectedLadderId);
        float lr = isSelLadder ? 0.2f : 1.0f;
        float lg = isSelLadder ? 0.95f : 0.85f;
        float lb = isSelLadder ? 1.0f : 0.2f;
        float la = isSelLadder ? 1.0f : 0.95f;

        lineVertices.push_back({ p0.x, p0.y, p0.z, 0,0,1, 0,0, lr, lg, lb, la });
        lineVertices.push_back({ p1.x, p1.y, p1.z, 0,0,1, 0,0, lr, lg, lb, la });
        lineVertices.push_back({ p2.x, p2.y, p2.z, 0,0,1, 0,0, lr, lg, lb, la });
        lineVertices.push_back({ p3.x, p3.y, p3.z, 0,0,1, 0,0, lr, lg, lb, la });

        // Outer ladder rails
        lineIndices.push_back(baseL + 0); lineIndices.push_back(baseL + 3);
        lineIndices.push_back(baseL + 1); lineIndices.push_back(baseL + 2);
        // Top and bottom rungs
        lineIndices.push_back(baseL + 0); lineIndices.push_back(baseL + 1);
        lineIndices.push_back(baseL + 3); lineIndices.push_back(baseL + 2);

        // Horizontal ladder rungs
        float height = std::abs(top.z - bottom.z);
        int rungs = std::max(2, static_cast<int>(height / 18.0f));
        for (int r = 1; r < rungs; ++r) {
            float t = static_cast<float>(r) / static_cast<float>(rungs);
            Vector3 rungL = p0 + (p3 - p0) * t;
            Vector3 rungR = p1 + (p2 - p1) * t;
            uint32_t rIdx = static_cast<uint32_t>(lineVertices.size());
            lineVertices.push_back({ rungL.x, rungL.y, rungL.z, 0,0,1, 0,0, lr, lg, lb, 0.85f });
            lineVertices.push_back({ rungR.x, rungR.y, rungR.z, 0,0,1, 0,0, lr, lg, lb, 0.85f });
            lineIndices.push_back(rIdx + 0);
            lineIndices.push_back(rIdx + 1);
        }
    }


    if (!quadIndices.empty()) {
        GenerateBuffers(quadVertices, quadIndices);
    }
    if (!lineIndices.empty()) {
        GenerateLineBuffers(lineVertices, lineIndices);
    }

    m_loaded = (m_indexCount > 0);
    return m_loaded;
}

void NavRenderer::GenerateBuffers(const std::vector<NavVertex>& vertices, const std::vector<uint32_t>& indices) {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(NavVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(NavVertex), (void*)offsetof(NavVertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(NavVertex), (void*)offsetof(NavVertex, nx));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(NavVertex), (void*)offsetof(NavVertex, u));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(NavVertex), (void*)offsetof(NavVertex, r));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    m_indexCount = static_cast<GLsizei>(indices.size());
}

void NavRenderer::GenerateLineBuffers(const std::vector<NavVertex>& vertices, const std::vector<uint32_t>& indices) {
    glGenVertexArrays(1, &m_lineVao);
    glGenBuffers(1, &m_lineVbo);
    glGenBuffers(1, &m_lineEbo);

    glBindVertexArray(m_lineVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_lineVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(NavVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lineEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(NavVertex), (void*)offsetof(NavVertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(NavVertex), (void*)offsetof(NavVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    m_lineIndexCount = static_cast<GLsizei>(indices.size());
}

void NavRenderer::Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp) {
    if (!m_loaded) return;

    Matrix4 modelMat = Matrix4::MakeIdentity();

    // Polygon offset prevents Z-fighting against BSP floor geometry
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(-2.5f, -2.5f);

    // Render area quads with alpha blending
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE); // Don't write depth for transparent areas to prevent occluding lines

    meshShader.Bind();
    meshShader.SetMat4("u_MVP", mvp);
    meshShader.SetMat4("u_Model", modelMat);
    meshShader.SetVec4("u_BaseColor", 1.0f, 1.0f, 1.0f, 1.0f);
    meshShader.SetInt("u_UseTexture", 0);
    meshShader.SetFloat("u_Alpha", 1.0f);
    meshShader.SetInt("u_EnableLighting", 0);

    glBindVertexArray(m_vao);
    glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    meshShader.Unbind();

    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);

    // Render outlines and connection lines with bold width
    lineShader.Bind();
    lineShader.SetMat4("u_MVP", mvp);
    lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

    glLineWidth(2.2f);
    glBindVertexArray(m_lineVao);
    glDrawElements(GL_LINES, m_lineIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    lineShader.Unbind();
    glLineWidth(1.0f);

    glDisable(GL_BLEND);
}
