#ifndef GIZMO_RENDERER_H
#define GIZMO_RENDERER_H

#include <vector>
#include <cstdint>
#include "editor/glad/include/glad/glad.h"
#include "editor/math/matrix4.h"
#include "editor/render/shader.h"
#include "editor/scene/editor_handles.h"

enum GizmoMode {
    GIZMO_MODE_COMBINED = 0, // Translate + Rotate + Scale (Universal Blender/Hammer style)
    GIZMO_MODE_TRANSLATE,    // Move arrows only
    GIZMO_MODE_ROTATE,       // Rotate rings only
    GIZMO_MODE_SCALE         // Scale boxes only
};

struct GizmoVertex {
    float x, y, z;
    float r, g, b, a;
};

class GizmoRenderer {
public:
    GizmoRenderer();
    ~GizmoRenderer();

    GizmoRenderer(const GizmoRenderer&) = delete;
    GizmoRenderer& operator=(const GizmoRenderer&) = delete;

    void Clear();

    void Render(const Shader& lineShader, const Matrix4& mvp,
                const Vector3& center, const Vector3& camPos,
                GizmoMode mode,
                SelectedHandleType hoveredHandle,
                SelectedHandleType selectedHandle);

    void RenderLineSegment(const Shader& lineShader, const Matrix4& mvp,
                           const Vector3& p0, const Vector3& p1,
                           float r, float g, float b, float a,
                           float lineWidth = 3.5f);

    void RenderRectMarquee(const Shader& lineShader, const Matrix4& mvp,
                           const Vector3& p0, const Vector3& p1,
                           float r = 0.0f, float g = 0.9f, float b = 1.0f, float a = 1.0f);

    void RenderRectMarquee4(const Shader& lineShader, const Matrix4& mvp,
                            float minX, float maxX, float minY, float maxY,
                            float nwZ, float neZ, float seZ, float swZ,
                            float r = 0.0f, float g = 0.9f, float b = 1.0f, float a = 1.0f);

    void RenderBoxWireframe(const Shader& lineShader, const Matrix4& mvp,
                           const Vector3& mins, const Vector3& maxs,
                           float r, float g, float b, float a,
                           float lineWidth = 2.5f);

    void RenderPathRibbon(const Shader& lineShader, const Matrix4& mvp,
                          const std::vector<Vector3>& points,
                          const std::vector<bool>& jumpFlags,
                          float lineWidth = 4.0f);

private:
    void BuildBuffers(const Vector3& center, const Vector3& camPos,
                      GizmoMode mode,
                      SelectedHandleType hoveredHandle,
                      SelectedHandleType selectedHandle);

    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_ebo{0};
    GLsizei m_indexCount{0};
};

#endif // GIZMO_RENDERER_H
