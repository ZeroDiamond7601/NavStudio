#include "editor/render/bsp_renderer.h"
#include <cmath>

BSPRenderer::BSPRenderer()
    : m_vao(0)
    , m_vbo(0)
    , m_ebo(0)
    , m_indexCount(0)
    , m_wireVao(0)
    , m_wireVbo(0)
    , m_wireEbo(0)
    , m_wireIndexCount(0)
    , m_loaded(false)
    , m_faceCount(0)
{
}

BSPRenderer::~BSPRenderer() {
    Clear();
}

void BSPRenderer::Clear() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo != 0) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    m_indexCount = 0;

    if (m_wireVao != 0) { glDeleteVertexArrays(1, &m_wireVao); m_wireVao = 0; }
    if (m_wireVbo != 0) { glDeleteBuffers(1, &m_wireVbo); m_wireVbo = 0; }
    if (m_wireEbo != 0) { glDeleteBuffers(1, &m_wireEbo); m_wireEbo = 0; }
    m_wireIndexCount = 0;

    m_loaded = false;
    m_faceCount = 0;
}

bool BSPRenderer::BuildFromBSP(const BSPFile& bsp) {
    Clear();
    if (!bsp.IsLoaded()) return false;

    std::vector<BSPVertex> solidVertices;
    std::vector<uint32_t> solidIndices;

    std::vector<BSPVertex> wireVertices;
    std::vector<uint32_t> wireIndices;

    int numFaces = bsp.GetFaceCount();
    m_faceCount = static_cast<size_t>(numFaces);

    Vector3 poly[128];

    for (int f = 0; f < numFaces; ++f) {
        const dface_t* face = bsp.GetFace(f);
        if (!face) continue;

        int vertCount = bsp.GetFacePolygon(f, poly, 128);
        if (vertCount < 3) continue;

        const dplane_t* plane = bsp.GetPlane(face->planenum);
        Vector3 norm(0.0f, 0.0f, 1.0f);
        if (plane) {
            norm = plane->normal;
            if (face->side != 0) {
                norm = norm * -1.0f;
            }
        }

        // Triangulate solid face (fan triangulation)
        uint32_t baseSolidVertex = static_cast<uint32_t>(solidVertices.size());
        for (int i = 0; i < vertCount; ++i) {
            BSPVertex vert;
            vert.x = poly[i].x;
            vert.y = poly[i].y;
            vert.z = poly[i].z;
            vert.nx = norm.x;
            vert.ny = norm.y;
            vert.nz = norm.z;
            vert.u = 0.0f;
            vert.v = 0.0f;
            vert.r = 0.8f;
            vert.g = 0.8f;
            vert.b = 0.82f;
            vert.a = 1.0f;
            solidVertices.push_back(vert);
        }

        for (int i = 1; i < vertCount - 1; ++i) {
            solidIndices.push_back(baseSolidVertex);
            solidIndices.push_back(baseSolidVertex + i);
            solidIndices.push_back(baseSolidVertex + i + 1);
        }

        // Generate perimeter lines for wireframe
        uint32_t baseWireVertex = static_cast<uint32_t>(wireVertices.size());
        for (int i = 0; i < vertCount; ++i) {
            BSPVertex vert;
            vert.x = poly[i].x;
            vert.y = poly[i].y;
            vert.z = poly[i].z;
            vert.nx = norm.x;
            vert.ny = norm.y;
            vert.nz = norm.z;
            vert.u = 0.0f;
            vert.v = 0.0f;
            vert.r = 0.35f;
            vert.g = 0.35f;
            vert.b = 0.38f;
            vert.a = 1.0f;
            wireVertices.push_back(vert);

            uint32_t next = (i + 1 == vertCount) ? 0 : (i + 1);
            wireIndices.push_back(baseWireVertex + i);
            wireIndices.push_back(baseWireVertex + next);
        }
    }

    if (!solidIndices.empty()) {
        GenerateBuffers(solidVertices, solidIndices);
    }
    if (!wireIndices.empty()) {
        GenerateWireframeBuffers(wireVertices, wireIndices);
    }

    m_loaded = (m_indexCount > 0);
    return m_loaded;
}

void BSPRenderer::GenerateBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices) {
    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(BSPVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    // aPos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, x));
    glEnableVertexAttribArray(0);
    // aNormal
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, nx));
    glEnableVertexAttribArray(1);
    // aTexCoord
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, u));
    glEnableVertexAttribArray(2);
    // aColor
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, r));
    glEnableVertexAttribArray(3);

    glBindVertexArray(0);
    m_indexCount = static_cast<GLsizei>(indices.size());
}

void BSPRenderer::GenerateWireframeBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& lineIndices) {
    glGenVertexArrays(1, &m_wireVao);
    glGenBuffers(1, &m_wireVbo);
    glGenBuffers(1, &m_wireEbo);

    glBindVertexArray(m_wireVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_wireVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(BSPVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_wireEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, lineIndices.size() * sizeof(uint32_t), lineIndices.data(), GL_STATIC_DRAW);

    // aPos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, x));
    glEnableVertexAttribArray(0);
    // aColor
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    m_wireIndexCount = static_cast<GLsizei>(lineIndices.size());
}

void BSPRenderer::Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp, BSPRenderMode mode) {
    if (!m_loaded) return;

    Matrix4 modelMat = Matrix4::MakeIdentity();

    if (mode == BSP_RENDER_SOLID) {
        meshShader.Bind();
        meshShader.SetMat4("u_MVP", mvp);
        meshShader.SetMat4("u_Model", modelMat);
        meshShader.SetVec4("u_BaseColor", 1.0f, 1.0f, 1.0f, 1.0f);
        meshShader.SetInt("u_UseTexture", 0);
        meshShader.SetFloat("u_Alpha", 1.0f);
        meshShader.SetInt("u_EnableLighting", 1);

        glBindVertexArray(m_vao);
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        meshShader.Unbind();
    } else if (mode == BSP_RENDER_WIREFRAME) {
        lineShader.Bind();
        lineShader.SetMat4("u_MVP", mvp);
        lineShader.SetVec4("u_Color", 0.45f, 0.45f, 0.48f, 1.0f);

        glBindVertexArray(m_wireVao);
        glDrawElements(GL_LINES, m_wireIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        lineShader.Unbind();
    } else if (mode == BSP_RENDER_GHOST) {
        // Translucent BSP surfaces so NavMesh inside rooms is visible
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        meshShader.Bind();
        meshShader.SetMat4("u_MVP", mvp);
        meshShader.SetMat4("u_Model", modelMat);
        meshShader.SetVec4("u_BaseColor", 0.6f, 0.65f, 0.75f, 1.0f);
        meshShader.SetInt("u_UseTexture", 0);
        meshShader.SetFloat("u_Alpha", 0.18f);
        meshShader.SetInt("u_EnableLighting", 1);

        glBindVertexArray(m_vao);
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        meshShader.Unbind();

        glDepthMask(GL_TRUE);

        // Draw subtle wireframe on top
        lineShader.Bind();
        lineShader.SetMat4("u_MVP", mvp);
        lineShader.SetVec4("u_Color", 0.4f, 0.45f, 0.55f, 0.35f);

        glBindVertexArray(m_wireVao);
        glDrawElements(GL_LINES, m_wireIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        lineShader.Unbind();

        glDisable(GL_BLEND);
    }
}
