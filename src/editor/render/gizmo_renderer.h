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
