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

    const float kZLift = 1.0f; // Lift slightly above floor to prevent Z-fighting

    for (const NavArea* area : areas) {
        if (!area) continue;

        uint32_t id = area->GetID();
        bool isSelected = (id == selectedId);
        bool isHovered = (id == hoveredId);

        // Determine area color based on selection and attributes
        float r = 0.2f, g = 0.8f, b = 0.3f, a = 0.65f; // Normal: Green

        if (isSelected) {
            r = 1.0f; g = 0.9f; b = 0.1f; a = 0.88f; // Selected: Yellow
        } else if (isHovered) {
            r = 0.1f; g = 0.9f; b = 0.95f; a = 0.78f; // Hovered: Cyan
        } else if (area->HasAttributes(NAV_MESH_CROUCH)) {
            r = 0.15f; g = 0.45f; b = 0.95f; a = 0.65f; // Crouch: Blue
        } else if (area->HasAttributes(NAV_MESH_JUMP)) {
            r = 0.95f; g = 0.55f; b = 0.15f; a = 0.65f; // Jump: Orange
        } else if (area->HasAttributes(NAV_MESH_TRANSIENT) || area->HasAttributes(NAV_MESH_NO_HOSTAGES)) {
            r = 0.9f; g = 0.2f; b = 0.2f; a = 0.65f; // Blocked: Red
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

        // Area border lines
        float lr = isSelected ? 1.0f : (r * 0.5f);
        float lg = isSelected ? 0.95f : (g * 0.5f);
        float lb = isSelected ? 0.2f : (b * 0.5f);
        float la = isSelected ? 1.0f : 0.9f;

        uint32_t baseLineVert = static_cast<uint32_t>(lineVertices.size());
        lineVertices.push_back({ cNW.x, cNW.y, cNW.z + 0.5f, 0,0,1, 0,0, lr, lg, lb, la });
        lineVertices.push_back({ cNE.x, cNE.y, cNE.z + 0.5f, 0,0,1, 0,0, lr, lg, lb, la });
        lineVertices.push_back({ cSE.x, cSE.y, cSE.z + 0.5f, 0,0,1, 0,0, lr, lg, lb, la });
        lineVertices.push_back({ cSW.x, cSW.y, cSW.z + 0.5f, 0,0,1, 0,0, lr, lg, lb, la });

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
        centerA.z += (kZLift + 1.5f);

        for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
            const auto& connects = area->GetAdjacentList(static_cast<NavDirType>(d));
            for (const auto& conn : connects) {
                const NavArea* target = conn.area;
                if (!target) continue;

                Vector3 centerB = target->GetCenter();
                centerB.z += (kZLift + 1.5f);

                bool isTwoWay = target->IsConnected(area);

                // Cyan for two-way, Magenta for one-way
                float cr = isTwoWay ? 0.2f : 0.95f;
                float cg = isTwoWay ? 0.85f : 0.15f;
                float cb = isTwoWay ? 0.95f : 0.95f;

                uint32_t cIdx = static_cast<uint32_t>(lineVertices.size());
                lineVertices.push_back({ centerA.x, centerA.y, centerA.z, 0,0,1, 0,0, cr, cg, cb, 0.8f });
                lineVertices.push_back({ centerB.x, centerB.y, centerB.z, 0,0,1, 0,0, cr, cg, cb, 0.8f });

                lineIndices.push_back(cIdx);
                lineIndices.push_back(cIdx + 1);
            }
        }
    }

    // Ladder rendering
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
        float lr = 0.95f, lg = 0.85f, lb = 0.2f, la = 0.9f;

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

    glDepthMask(GL_TRUE);

    // Render outlines and connection lines
    lineShader.Bind();
    lineShader.SetMat4("u_MVP", mvp);
    lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

    glBindVertexArray(m_lineVao);
    glDrawElements(GL_LINES, m_lineIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    lineShader.Unbind();

    glDisable(GL_BLEND);
}
