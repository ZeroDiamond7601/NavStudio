#ifndef BSP_RENDERER_H
#define BSP_RENDERER_H

#include <vector>
#include "editor/glad/include/glad/glad.h"
#include "bsp/bsp_file.h"
#include "editor/render/shader.h"
#include "editor/math/matrix4.h"

enum BSPRenderMode {
    BSP_RENDER_SOLID,
    BSP_RENDER_WIREFRAME,
    BSP_RENDER_GHOST
};

struct BSPVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
};

class BSPRenderer {
public:
    BSPRenderer();
    ~BSPRenderer();

    bool BuildFromBSP(const BSPFile& bsp);
    void Clear();

    void Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp, BSPRenderMode mode, const Vector3& camPos);

    bool IsLoaded() const { return m_loaded; }
    size_t GetFaceCount() const { return m_faceCount; }
    size_t GetTriangleCount() const { return m_indexCount / 3; }

    bool GetShowWireframeOnSolid() const { return m_showWireframeOnSolid; }
    void SetShowWireframeOnSolid(bool show) { m_showWireframeOnSolid = show; }

private:
    void GenerateBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& indices);
    void GenerateWireframeBuffers(const std::vector<BSPVertex>& vertices, const std::vector<uint32_t>& lineIndices);

    GLuint m_vao;
    GLuint m_vbo;
    GLuint m_ebo;
    GLsizei m_indexCount;

    GLuint m_wireVao;
    GLuint m_wireVbo;
    GLuint m_wireEbo;
    GLsizei m_wireIndexCount;

    bool m_loaded;
    bool m_showWireframeOnSolid;
    size_t m_faceCount;
};

#endif // BSP_RENDERER_H
