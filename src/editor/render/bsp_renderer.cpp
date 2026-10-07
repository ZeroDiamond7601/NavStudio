#include "editor/render/bsp_renderer.h"
#include <cmath>
#include <map>
#include <unordered_map>
#include <algorithm>

BSPRenderer::BSPRenderer()
    : m_vao(0)
    , m_vbo(0)
    , m_ebo(0)
    , m_indexCount(0)
    , m_wireVao(0)
    , m_wireVbo(0)
    , m_wireEbo(0)
    , m_wireIndexCount(0)
    , m_skyWireVao(0)
    , m_skyWireVbo(0)
    , m_skyWireEbo(0)
    , m_skyWireIndexCount(0)
    , m_loaded(false)
    , m_showWireframeOnSolid(true)
    , m_showSkybox(true)
    , m_showSkyWireframe(false)
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

    if (m_skyWireVao != 0) { glDeleteVertexArrays(1, &m_skyWireVao); m_skyWireVao = 0; }
    if (m_skyWireVbo != 0) { glDeleteBuffers(1, &m_skyWireVbo); m_skyWireVbo = 0; }
    if (m_skyWireEbo != 0) { glDeleteBuffers(1, &m_skyWireEbo); m_skyWireEbo = 0; }
    m_skyWireIndexCount = 0;

    m_textureBatches.clear();
    m_loaded = false;
    m_faceCount = 0;
}

bool BSPRenderer::BuildFromBSP(const BSPFile& bsp, const TextureManager* texMgr) {
    Clear();
    if (!bsp.IsLoaded()) return false;

    std::vector<BSPVertex> solidVertices;
    std::vector<uint32_t> solidIndices;

    std::vector<BSPVertex> wireVertices;
    std::vector<uint32_t> wireIndices;

    std::vector<BSPVertex> skyWireVertices;
    std::vector<uint32_t> skyWireIndices;

    int numFaces = bsp.GetFaceCount();
    m_faceCount = static_cast<size_t>(numFaces);

    Vector3 poly[128];

    // Temporary storage grouping triangle indices by (OpenGL texture ID, isSky)
    std::map<std::pair<GLuint, bool>, std::vector<uint32_t>> batchIndices;
    std::map<std::pair<GLuint, bool>, bool> batchTransparency;

    GLuint fallbackTexId = texMgr ? texMgr->GetCheckerboardTextureID() : 0;

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

        // UV Projection setup
        float sVec[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        float tVec[4] = {0.0f, 1.0f, 0.0f, 0.0f};
        float texW = 64.0f;
        float texH = 64.0f;
        GLuint texId = fallbackTexId;
        bool isTransparent = false;
        bool isSky = false;

        const texinfo_t* ti = bsp.GetTexInfo(face->texinfo);
        if (ti) {
            sVec[0] = ti->vecs[0][0]; sVec[1] = ti->vecs[0][1]; sVec[2] = ti->vecs[0][2]; sVec[3] = ti->vecs[0][3];
            tVec[0] = ti->vecs[1][0]; tVec[1] = ti->vecs[1][1]; tVec[2] = ti->vecs[1][2]; tVec[3] = ti->vecs[1][3];

            int w = 0, h = 0;
            if (bsp.GetTextureDimensions(ti->miptex, w, h) && w > 0 && h > 0) {
                texW = static_cast<float>(w);
                texH = static_cast<float>(h);
            }

            const char* texName = bsp.GetTextureName(ti->miptex);
            if (texName && texName[0] != '\0') {
#if defined(_WIN32)
                if (_strnicmp(texName, "sky", 3) == 0) isSky = true;
#else
                if (strncasecmp(texName, "sky", 3) == 0) isSky = true;
#endif
                if (texMgr) {
                    texId = texMgr->GetTextureID(texName);
                    LoadedTextureInfo tInfo;
                    if (texMgr->GetTextureInfo(texName, tInfo)) {
                        if (tInfo.width > 0 && tInfo.height > 0) {
                            texW = static_cast<float>(tInfo.width);
                            texH = static_cast<float>(tInfo.height);
                        }
                        isTransparent = tInfo.isTransparent;
                    }
                }
                if (texName[0] == '{') {
                    isTransparent = true;
                }
            }
        }

        if (texW <= 0.0f) texW = 64.0f;
        if (texH <= 0.0f) texH = 64.0f;

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

            // Mathematical UV projection from texinfo
            float s = poly[i].x * sVec[0] + poly[i].y * sVec[1] + poly[i].z * sVec[2] + sVec[3];
            float t = poly[i].x * tVec[0] + poly[i].y * tVec[1] + poly[i].z * tVec[2] + tVec[3];
            vert.u = s / texW;
            vert.v = t / texH;

            vert.r = 1.0f;
            vert.g = 1.0f;
            vert.b = 1.0f;
            vert.a = 1.0f;
            solidVertices.push_back(vert);
        }

        auto batchKey = std::make_pair(texId, isSky);
        auto& batchList = batchIndices[batchKey];
        for (int i = 1; i < vertCount - 1; ++i) {
            batchList.push_back(baseSolidVertex);
            batchList.push_back(baseSolidVertex + i);
            batchList.push_back(baseSolidVertex + i + 1);
        }

        if (isTransparent) {
            batchTransparency[batchKey] = true;
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

        // Generate perimeter lines for sky wireframe
        if (isSky) {
            uint32_t baseSkyWireVertex = static_cast<uint32_t>(skyWireVertices.size());
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
                vert.r = 0.2f;
                vert.g = 0.75f;
                vert.b = 1.0f;
                vert.a = 1.0f;
                skyWireVertices.push_back(vert);

                uint32_t next = (i + 1 == vertCount) ? 0 : (i + 1);
                skyWireIndices.push_back(baseSkyWireVertex + i);
                skyWireIndices.push_back(baseSkyWireVertex + next);
            }
        }
    }

    // Assemble index buffer: non-sky opaque first, then transparent, then sky
    std::vector<std::pair<std::pair<GLuint, bool>, bool>> orderedBatches;
    orderedBatches.reserve(batchIndices.size());

    // 1. Opaque non-sky batches
    for (const auto& kv : batchIndices) {
        if (!kv.first.second && !batchTransparency[kv.first]) {
            orderedBatches.push_back({kv.first, false});
        }
    }
    // 2. Transparent batches
    for (const auto& kv : batchIndices) {
        if (!kv.first.second && batchTransparency[kv.first]) {
            orderedBatches.push_back({kv.first, true});
        }
    }
    // 3. Sky batches
    for (const auto& kv : batchIndices) {
        if (kv.first.second) {
            orderedBatches.push_back({kv.first, batchTransparency[kv.first]});
        }
    }

    m_textureBatches.clear();
    m_textureBatches.reserve(orderedBatches.size());

    for (const auto& item : orderedBatches) {
        GLuint texId = item.first.first;
        bool isSky = item.first.second;
        bool isTrans = item.second;
        const auto& indices = batchIndices[item.first];

        BSPTextureBatch batch;
        batch.textureId = texId;
        batch.isSky = isSky;
        batch.startIndex = static_cast<GLsizei>(solidIndices.size());
        batch.indexCount = static_cast<GLsizei>(indices.size());
        batch.isTransparent = isTrans;

        solidIndices.insert(solidIndices.end(), indices.begin(), indices.end());
        m_textureBatches.push_back(batch);
    }

    if (!solidIndices.empty()) {
        GenerateBuffers(solidVertices, solidIndices);
    }
    if (!wireIndices.empty()) {
        GenerateWireframeBuffers(wireVertices, wireIndices);
    }
    if (!skyWireIndices.empty()) {
        GenerateSkyWireframeBuffers(skyWireVertices, skyWireIndices);
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

void BSPRenderer::GenerateSkyWireframeBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& lineIndices) {
    glGenVertexArrays(1, &m_skyWireVao);
    glGenBuffers(1, &m_skyWireVbo);
    glGenBuffers(1, &m_skyWireEbo);

    glBindVertexArray(m_skyWireVao);

    glBindBuffer(GL_ARRAY_BUFFER, m_skyWireVbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(BSPVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_skyWireEbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, lineIndices.size() * sizeof(uint32_t), lineIndices.data(), GL_STATIC_DRAW);

    // aPos
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, x));
    glEnableVertexAttribArray(0);
    // aColor
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(BSPVertex), (void*)offsetof(BSPVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    m_skyWireIndexCount = static_cast<GLsizei>(lineIndices.size());
}

void BSPRenderer::Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp, BSPRenderMode mode, const Vector3& camPos) {
    if (!m_loaded) return;

    Matrix4 modelMat = Matrix4::MakeIdentity();

    if (mode == BSP_RENDER_TEXTURED) {
        meshShader.Bind();
        meshShader.SetMat4("u_MVP", mvp);
        meshShader.SetMat4("u_Model", modelMat);
        meshShader.SetVec3("u_CameraPos", camPos.x, camPos.y, camPos.z);
        meshShader.SetVec4("u_BaseColor", 1.0f, 1.0f, 1.0f, 1.0f);
        meshShader.SetInt("u_UseTexture", 1);
        meshShader.SetInt("u_DiffuseTexture", 0);
        meshShader.SetFloat("u_Alpha", 1.0f);
        meshShader.SetInt("u_EnableLighting", 1);

        if (m_showWireframeOnSolid) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
        }

        glBindVertexArray(m_vao);
        glActiveTexture(GL_TEXTURE0);

        for (const auto& batch : m_textureBatches) {
            if (m_showSkybox && batch.isSky) {
                // Skip sky surfaces so background 3D skybox is visible
                continue;
            }
            glBindTexture(GL_TEXTURE_2D, batch.textureId);
            glDrawElements(GL_TRIANGLES, batch.indexCount, GL_UNSIGNED_INT, (const void*)(static_cast<uintptr_t>(batch.startIndex) * sizeof(uint32_t)));
        }

        glBindVertexArray(0);
        meshShader.Unbind();

        if (m_showWireframeOnSolid) {
            glDisable(GL_POLYGON_OFFSET_FILL);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            lineShader.Bind();
            lineShader.SetMat4("u_MVP", mvp);
            lineShader.SetVec4("u_Color", 0.18f, 0.20f, 0.24f, 0.65f);

            glLineWidth(1.2f);
            glBindVertexArray(m_wireVao);
            glDrawElements(GL_LINES, m_wireIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            lineShader.Unbind();
            glLineWidth(1.0f);

            glDisable(GL_BLEND);
        }

        // Sky brush wireframe outlines (cyan)
        if (m_showSkybox && m_showSkyWireframe && m_skyWireIndexCount > 0) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            lineShader.Bind();
            lineShader.SetMat4("u_MVP", mvp);
            lineShader.SetVec4("u_Color", 0.2f, 0.75f, 1.0f, 0.85f);

            glLineWidth(1.5f);
            glBindVertexArray(m_skyWireVao);
            glDrawElements(GL_LINES, m_skyWireIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            lineShader.Unbind();
            glLineWidth(1.0f);

            glDisable(GL_BLEND);
        }
    } else if (mode == BSP_RENDER_SOLID) {
        meshShader.Bind();
        meshShader.SetMat4("u_MVP", mvp);
        meshShader.SetMat4("u_Model", modelMat);
        meshShader.SetVec3("u_CameraPos", camPos.x, camPos.y, camPos.z);
        meshShader.SetVec4("u_BaseColor", 0.80f, 0.80f, 0.82f, 1.0f);
        meshShader.SetInt("u_UseTexture", 0);
        meshShader.SetFloat("u_Alpha", 1.0f);
        meshShader.SetInt("u_EnableLighting", 1);

        if (m_showWireframeOnSolid) {
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1.0f, 1.0f);
        }

        glBindVertexArray(m_vao);
        for (const auto& batch : m_textureBatches) {
            if (m_showSkybox && batch.isSky) {
                continue;
            }
            glDrawElements(GL_TRIANGLES, batch.indexCount, GL_UNSIGNED_INT, (const void*)(static_cast<uintptr_t>(batch.startIndex) * sizeof(uint32_t)));
        }
        glBindVertexArray(0);
        meshShader.Unbind();

        if (m_showWireframeOnSolid) {
            glDisable(GL_POLYGON_OFFSET_FILL);

            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            lineShader.Bind();
            lineShader.SetMat4("u_MVP", mvp);
            lineShader.SetVec4("u_Color", 0.18f, 0.20f, 0.24f, 0.65f);

            glLineWidth(1.2f);
            glBindVertexArray(m_wireVao);
            glDrawElements(GL_LINES, m_wireIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            lineShader.Unbind();
            glLineWidth(1.0f);

            glDisable(GL_BLEND);
        }

        if (m_showSkybox && m_showSkyWireframe && m_skyWireIndexCount > 0) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

            lineShader.Bind();
            lineShader.SetMat4("u_MVP", mvp);
            lineShader.SetVec4("u_Color", 0.2f, 0.75f, 1.0f, 0.85f);

            glLineWidth(1.5f);
            glBindVertexArray(m_skyWireVao);
            glDrawElements(GL_LINES, m_skyWireIndexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
            lineShader.Unbind();
            glLineWidth(1.0f);

            glDisable(GL_BLEND);
        }
    } else if (mode == BSP_RENDER_WIREFRAME) {
        lineShader.Bind();
        lineShader.SetMat4("u_MVP", mvp);
        lineShader.SetVec4("u_Color", 0.50f, 0.52f, 0.58f, 1.0f);

        glLineWidth(1.2f);
        glBindVertexArray(m_wireVao);
        glDrawElements(GL_LINES, m_wireIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        lineShader.Unbind();
        glLineWidth(1.0f);
    } else if (mode == BSP_RENDER_GHOST) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);

        meshShader.Bind();
        meshShader.SetMat4("u_MVP", mvp);
        meshShader.SetMat4("u_Model", modelMat);
        meshShader.SetVec3("u_CameraPos", camPos.x, camPos.y, camPos.z);
        meshShader.SetVec4("u_BaseColor", 0.65f, 0.70f, 0.80f, 1.0f);
        meshShader.SetInt("u_UseTexture", 0);
        meshShader.SetFloat("u_Alpha", 0.18f);
        meshShader.SetInt("u_EnableLighting", 1);

        glBindVertexArray(m_vao);
        glDrawElements(GL_TRIANGLES, m_indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        meshShader.Unbind();

        glDepthMask(GL_TRUE);

        lineShader.Bind();
        lineShader.SetMat4("u_MVP", mvp);
        lineShader.SetVec4("u_Color", 0.42f, 0.48f, 0.60f, 0.40f);

        glLineWidth(1.0f);
        glBindVertexArray(m_wireVao);
        glDrawElements(GL_LINES, m_wireIndexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        lineShader.Unbind();

        glDisable(GL_BLEND);
    }
}
