#ifndef NAV_RENDERER_H
#define NAV_RENDERER_H

#include <vector>
#include <cstdint>
#include "editor/glad/include/glad/glad.h"
#include "nav/nav_file.h"
#include "editor/render/shader.h"
#include "editor/math/matrix4.h"

#include "editor/scene/editor_handles.h"

struct NavVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
};

class NavRenderer {
public:
    NavRenderer();
    ~NavRenderer();

    bool BuildFromNav(const NavMesh& nav, uint32_t selectedId = 0, uint32_t hoveredId = 0,
                      uint32_t connectTargetId = 0, int transformAxis = 0,
                      SelectedHandleType hoveredHandle = HANDLE_NONE,
                      SelectedHandleType selectedHandle = HANDLE_NONE,
                      const std::vector<uint32_t>* selectedIds = nullptr);
    void Clear();

    void Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp);

    bool IsLoaded() const { return m_loaded; }
    size_t GetAreaCount() const { return m_areaCount; }

    bool GetShowConnectionValidity() const { return m_showConnectionValidity; }
    void SetShowConnectionValidity(bool show) { m_showConnectionValidity = show; }
    float GetMaxStepHeight() const { return m_maxStepHeight; }
    void SetMaxStepHeight(float step) { m_maxStepHeight = step; }
    size_t GetInvalidConnectionCount() const { return m_invalidConnectionCount; }

    void SetSelectedConnection(uint32_t fromId, uint32_t toId) {
        m_selConnFrom = fromId;
        m_selConnTo = toId;
    }
    uint32_t GetSelectedConnectionFrom() const { return m_selConnFrom; }
    uint32_t GetSelectedConnectionTo() const { return m_selConnTo; }

private:
    void GenerateBuffers(const std::vector<NavVertex>& vertices, const std::vector<uint32_t>& indices);
    void GenerateLineBuffers(const std::vector<NavVertex>& vertices, const std::vector<uint32_t>& indices);

    GLuint m_vao;
    GLuint m_vbo;
    GLuint m_ebo;
    GLsizei m_indexCount;

    GLuint m_lineVao;
    GLuint m_lineVbo;
    GLuint m_lineEbo;
    GLsizei m_lineIndexCount;

    bool m_loaded;
    size_t m_areaCount;
    bool m_showConnectionValidity{true};
    float m_maxStepHeight{18.0f};
    size_t m_invalidConnectionCount{0};
    uint32_t m_selConnFrom{0};
    uint32_t m_selConnTo{0};
};

#endif // NAV_RENDERER_H
