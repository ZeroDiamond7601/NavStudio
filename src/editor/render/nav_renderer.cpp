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

bool NavRenderer::BuildFromNav(const NavMesh& nav, uint32_t selectedId, uint32_t hoveredId) {
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

        lineIndices.push_back(baseLineVert + 0);
        lineIndices.push_back(baseLineVert + 1);
        lineIndices.push_back(baseLineVert + 1);
        lineIndices.push_back(baseLineVert + 2);
        lineIndices.push_back(baseLineVert + 2);
        lineIndices.push_back(baseLineVert + 3);
        lineIndices.push_back(baseLineVert + 3);
        lineIndices.push_back(baseLineVert + 0);

        // Connection lines between area centroids
        Vector3 centerA = area->GetCenter();
        centerA.z += (kZLift + 2.0f);

        for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
            const auto& connects = area->GetAdjacentList(static_cast<NavDirType>(d));
            for (const auto& conn : connects) {
                const NavArea* target = conn.area;
                if (!target) continue;

                Vector3 centerB = target->GetCenter();
                centerB.z += (kZLift + 2.0f);

                bool isTwoWay = target->IsConnected(area);

                // Bright Cyan for two-way, Vivid Magenta for one-way
                float cr = isTwoWay ? 0.15f : 1.0f;
                float cg = isTwoWay ? 0.90f : 0.20f;
                float cb = isTwoWay ? 1.0f : 0.85f;

                uint32_t cIdx = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ centerA.x, centerA.y, centerA.z, 0,0,1, 0,0, cr, cg, cb, 0.85f });
                lineVertices.push_back({ centerB.x, centerB.y, centerB.z, 0,0,1, 0,0, cr, cg, cb, 0.85f });

                lineIndices.push_back(cIdx);
                lineIndices.push_back(cIdx + 1);

                // Add directional arrowhead
                Vector3 delta = centerB - centerA;
                float dist = delta.Length();
                if (dist > 24.0f) {
                    Vector3 fwd = delta * (1.0f / dist);
                    Vector3 side(-fwd.y, fwd.x, 0.0f);
                    Vector3 tip = centerA + fwd * (dist * 0.72f);
                    Vector3 leftBar = tip - fwd * 8.0f + side * 4.0f;
                    Vector3 rightBar = tip - fwd * 8.0f - side * 4.0f;

                    uint32_t aIdx = static_cast<uint32_t>(lineVertices.size());
                    lineVertices.push_back({ tip.x, tip.y, tip.z, 0,0,1, 0,0, cr, cg, cb, 0.95f });
                    lineVertices.push_back({ leftBar.x, leftBar.y, leftBar.z, 0,0,1, 0,0, cr, cg, cb, 0.95f });
                    lineVertices.push_back({ tip.x, tip.y, tip.z, 0,0,1, 0,0, cr, cg, cb, 0.95f });
                    lineVertices.push_back({ rightBar.x, rightBar.y, rightBar.z, 0,0,1, 0,0, cr, cg, cb, 0.95f });

                    lineIndices.push_back(aIdx + 0);
                    lineIndices.push_back(aIdx + 1);
                    lineIndices.push_back(aIdx + 2);
                    lineIndices.push_back(aIdx + 3);
                }
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
