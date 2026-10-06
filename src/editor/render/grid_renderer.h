#ifndef GRID_RENDERER_H
#define GRID_RENDERER_H

#include <vector>
#include <cstdint>
#include "editor/math/matrix4.h"
#include "editor/render/shader.h"
#include "editor/glad/include/glad/glad.h"

struct GridVertex {
    float x, y, z;
    float r, g, b, a;
};

class GridRenderer {
public:
    GridRenderer();
    ~GridRenderer();

    GridRenderer(const GridRenderer&) = delete;
    GridRenderer& operator=(const GridRenderer&) = delete;

    void Clear();

    // Renders the Hammer-style ground reference grid centered around camPos.
    void Render(const Shader& lineShader, const Matrix4& mvp, const Vector3& camPos,
                float gridSize, float gridElevation);

private:
    void Update(const Vector3& camPos, float gridSize, float gridElevation);

    GLuint m_vao{0};
    GLuint m_vbo{0};
    GLuint m_ebo{0};
    GLsizei m_indexCount{0};

    // Cache to avoid regenerating vertices when camera stays within the same grid cell
    float m_lastSnapX{1e9f};
    float m_lastSnapY{1e9f};
    float m_lastGridSize{0.0f};
    float m_lastGridElevation{1e9f};
};

#endif // GRID_RENDERER_H
