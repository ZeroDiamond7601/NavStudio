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
                               SelectedHandleType selectedHandle) {
    Clear();
    if (!nav.IsLoaded()) return false;

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
        bool isHovered = (id == hoveredId);

        // Determine area color based on selection and attributes with high contrast
        float r = 0.15f, g = 0.82f, b = 0.38f, a = 0.70f; // Normal: High-contrast green
        float lr = 0.30f, lg = 1.0f, lb = 0.50f, la = 1.0f;

        if (isSelected) {
            r = 1.0f; g = 0.88f; b = 0.10f; a = 0.90f; // Selected: Glowing gold
            lr = 1.0f; lg = 1.0f; lb = 0.30f; la = 1.0f;
        } else if (isHovered) {
            r = 0.05f; g = 0.92f; b = 1.0f; a = 0.80f; // Hovered: Electric cyan
            lr = 0.50f; lg = 1.0f; lb = 1.0f; la = 1.0f;
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

                if (isSelArea || isTargetSelected) {
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

                // Add directional arrowhead along this lane
                if (dist > 20.0f) {
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

    // 3D Transform Gizmo, Center box, and vertex handles for selected area
    if (selectedId != 0) {
        const NavArea* sel = nav.GetAreaByID(selectedId);
        if (sel) {
            Vector3 c = sel->GetCenter();
            c.z += (kZLift + 3.0f);
            float gLen = 48.0f;

            // Center Position Handle: Small diamond box
            bool isCenterActive = (hoveredHandle == HANDLE_GIZMO_CENTER || selectedHandle == HANDLE_GIZMO_CENTER);
            float cbR = isCenterActive ? 1.0f : 0.85f;
            float cbG = isCenterActive ? 0.95f : 0.85f;
            float cbB = isCenterActive ? 0.20f : 0.85f;
            float cbSize = isCenterActive ? 5.5f : 4.0f;
            uint32_t cbIdx = static_cast<uint32_t>(lineVertices.size());
            lineVertices.push_back({ c.x - cbSize, c.y, c.z, 0,0,1, 0,0, cbR, cbG, cbB, 1.0f });
            lineVertices.push_back({ c.x, c.y + cbSize, c.z, 0,0,1, 0,0, cbR, cbG, cbB, 1.0f });
            lineVertices.push_back({ c.x + cbSize, c.y, c.z, 0,0,1, 0,0, cbR, cbG, cbB, 1.0f });
            lineVertices.push_back({ c.x, c.y - cbSize, c.z, 0,0,1, 0,0, cbR, cbG, cbB, 1.0f });
            lineIndices.push_back(cbIdx + 0); lineIndices.push_back(cbIdx + 1);
            lineIndices.push_back(cbIdx + 1); lineIndices.push_back(cbIdx + 2);
            lineIndices.push_back(cbIdx + 2); lineIndices.push_back(cbIdx + 3);
            lineIndices.push_back(cbIdx + 3); lineIndices.push_back(cbIdx + 0);

            // X Axis: Red (+X East) - Highlighted if active
            bool isXActive = (hoveredHandle == HANDLE_GIZMO_X || selectedHandle == HANDLE_GIZMO_X);
            float xR = isXActive ? 1.0f : 0.95f;
            float xG = isXActive ? 0.90f : 0.20f;
            float xB = isXActive ? 0.20f : 0.20f;
            float xLen = isXActive ? (gLen + 6.0f) : gLen;
            uint32_t gx = static_cast<uint32_t>(lineVertices.size());
            lineVertices.push_back({ c.x, c.y, c.z, 0,0,1, 0,0, xR, xG, xB, 1.0f });
            lineVertices.push_back({ c.x + xLen, c.y, c.z, 0,0,1, 0,0, xR, xG, xB, 1.0f });
            lineVertices.push_back({ c.x + xLen, c.y, c.z, 0,0,1, 0,0, xR, xG, xB, 1.0f });
            lineVertices.push_back({ c.x + xLen - 8.0f, c.y + 4.5f, c.z, 0,0,1, 0,0, xR, xG, xB, 1.0f });
            lineVertices.push_back({ c.x + xLen, c.y, c.z, 0,0,1, 0,0, xR, xG, xB, 1.0f });
            lineVertices.push_back({ c.x + xLen - 8.0f, c.y - 4.5f, c.z, 0,0,1, 0,0, xR, xG, xB, 1.0f });
            lineIndices.push_back(gx + 0); lineIndices.push_back(gx + 1);
            lineIndices.push_back(gx + 2); lineIndices.push_back(gx + 3);
            lineIndices.push_back(gx + 4); lineIndices.push_back(gx + 5);

            // Y Axis: Green (+Y North) - Highlighted if active
            bool isYActive = (hoveredHandle == HANDLE_GIZMO_Y || selectedHandle == HANDLE_GIZMO_Y);
            float yR = isYActive ? 0.35f : 0.20f;
            float yG = isYActive ? 1.0f : 0.95f;
            float yB = isYActive ? 0.90f : 0.30f;
            float yLen = isYActive ? (gLen + 6.0f) : gLen;
            uint32_t gy = static_cast<uint32_t>(lineVertices.size());
            lineVertices.push_back({ c.x, c.y, c.z, 0,0,1, 0,0, yR, yG, yB, 1.0f });
            lineVertices.push_back({ c.x, c.y + yLen, c.z, 0,0,1, 0,0, yR, yG, yB, 1.0f });
            lineVertices.push_back({ c.x, c.y + yLen, c.z, 0,0,1, 0,0, yR, yG, yB, 1.0f });
            lineVertices.push_back({ c.x + 4.5f, c.y + yLen - 8.0f, c.z, 0,0,1, 0,0, yR, yG, yB, 1.0f });
            lineVertices.push_back({ c.x, c.y + yLen, c.z, 0,0,1, 0,0, yR, yG, yB, 1.0f });
            lineVertices.push_back({ c.x - 4.5f, c.y + yLen - 8.0f, c.z, 0,0,1, 0,0, yR, yG, yB, 1.0f });
            lineIndices.push_back(gy + 0); lineIndices.push_back(gy + 1);
            lineIndices.push_back(gy + 2); lineIndices.push_back(gy + 3);
            lineIndices.push_back(gy + 4); lineIndices.push_back(gy + 5);

            // Z Axis: Blue (+Z Up) - Highlighted if active
            bool isZActive = (hoveredHandle == HANDLE_GIZMO_Z || selectedHandle == HANDLE_GIZMO_Z);
            float zR = isZActive ? 0.40f : 0.20f;
            float zG = isZActive ? 0.90f : 0.55f;
            float zB = isZActive ? 1.0f : 1.0f;
            float zLen = isZActive ? (gLen + 6.0f) : gLen;
            uint32_t gz = static_cast<uint32_t>(lineVertices.size());
            lineVertices.push_back({ c.x, c.y, c.z, 0,0,1, 0,0, zR, zG, zB, 1.0f });
            lineVertices.push_back({ c.x, c.y, c.z + zLen, 0,0,1, 0,0, zR, zG, zB, 1.0f });
            lineVertices.push_back({ c.x, c.y, c.z + zLen, 0,0,1, 0,0, zR, zG, zB, 1.0f });
            lineVertices.push_back({ c.x + 4.5f, c.y, c.z + zLen - 8.0f, 0,0,1, 0,0, zR, zG, zB, 1.0f });
            lineVertices.push_back({ c.x, c.y, c.z + zLen, 0,0,1, 0,0, zR, zG, zB, 1.0f });
            lineVertices.push_back({ c.x - 4.5f, c.y, c.z + zLen - 8.0f, 0,0,1, 0,0, zR, zG, zB, 1.0f });
            lineIndices.push_back(gz + 0); lineIndices.push_back(gz + 1);
            lineIndices.push_back(gz + 2); lineIndices.push_back(gz + 3);
            lineIndices.push_back(gz + 4); lineIndices.push_back(gz + 5);

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
        float lr = 1.0f, lg = 0.85f, lb = 0.2f, la = 0.95f;

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
