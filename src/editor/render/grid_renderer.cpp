#include "editor/render/grid_renderer.h"
#include <cmath>
#include <algorithm>

GridRenderer::GridRenderer()
    : m_vao(0)
    , m_vbo(0)
    , m_ebo(0)
    , m_indexCount(0)
    , m_lastSnapX(1e9f)
    , m_lastSnapY(1e9f)
    , m_lastGridSize(0.0f)
    , m_lastGridElevation(1e9f)
{
}

GridRenderer::~GridRenderer() {
    Clear();
}

void GridRenderer::Clear() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo != 0) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    m_indexCount = 0;
    m_lastSnapX = 1e9f;
    m_lastSnapY = 1e9f;
    m_lastGridSize = 0.0f;
    m_lastGridElevation = 1e9f;
}

void GridRenderer::Update(const Vector3& camPos, float gridSize, float gridElevation) {
    if (gridSize < 1.0f) gridSize = 1.0f;

    // Center grid lines around quantized camera position to give seamless infinite coverage
    // while keeping line coordinates locked to integer multiples of gridSize in world space.
    float snapX = std::floor(camPos.x / gridSize) * gridSize;
    float snapY = std::floor(camPos.y / gridSize) * gridSize;

    // Check if cached buffer is already valid
    if (m_vao != 0 &&
        std::abs(snapX - m_lastSnapX) < 0.01f &&
        std::abs(snapY - m_lastSnapY) < 0.01f &&
        std::abs(gridSize - m_lastGridSize) < 0.01f &&
        std::abs(gridElevation - m_lastGridElevation) < 0.01f) {
        return;
    }

    m_lastSnapX = snapX;
    m_lastSnapY = snapY;
    m_lastGridSize = gridSize;
    m_lastGridElevation = gridElevation;

    // Determine line budget based on grid spacing
    int halfLines = 64;
    if (gridSize >= 128.0f) {
        halfLines = 32;
    } else if (gridSize >= 64.0f) {
        halfLines = 48;
    } else if (gridSize >= 32.0f) {
        halfLines = 64;
    } else if (gridSize >= 16.0f) {
        halfLines = 96;
    } else {
        halfLines = 128;
    }

    float halfExtent = static_cast<float>(halfLines) * gridSize;
    if (halfExtent > 4096.0f) {
        halfExtent = 4096.0f;
        halfLines = static_cast<int>(halfExtent / gridSize);
    }

    // Both X and Y axes span the exact same extent [-halfExtent, +halfExtent] to guarantee
    // a 100% symmetric, square grid without one-way whiskers or hanging line segments.
    float minX = snapX - halfExtent;
    float maxX = snapX + halfExtent;
    float minY = snapY - halfExtent;
    float maxY = snapY + halfExtent;

    // Slight lift above floor to prevent coplanar Z-fighting with BSP horizontal brushes
    const float z = gridElevation + 0.15f;
    const float fadeDist = halfExtent * 0.22f;
    const float innerMinX = minX + fadeDist;
    const float innerMaxX = maxX - fadeDist;
    const float innerMinY = minY + fadeDist;
    const float innerMaxY = maxY - fadeDist;

    // Major grid line interval (Hammer: multiples of 8 grid units or 64/128/512/1024)
    float majorInterval = gridSize * 8.0f;
    if (gridSize <= 8.0f && majorInterval < 64.0f) {
        majorInterval = 64.0f;
    }

    std::vector<GridVertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(static_cast<size_t>((halfLines * 2 + 1) * 8));
    indices.reserve(static_cast<size_t>((halfLines * 2 + 1) * 12));

    // 1. Horizontal lines (sweeping along X, spaced along Y)
    for (int i = -halfLines; i <= halfLines; ++i) {
        float y = snapY + static_cast<float>(i) * gridSize;

        float r = 0.22f, g = 0.27f, b = 0.35f, baseAlpha = 0.40f;
        bool isOriginAxis = (std::abs(y) < 0.01f);
        bool isMajor = false;

        float rem = std::abs(std::remainder(y, majorInterval));
        if (rem < 0.01f) {
            isMajor = true;
        }

        if (isOriginAxis) {
            // World X-Axis: Vivid Red
            r = 0.90f; g = 0.25f; b = 0.25f; baseAlpha = 0.90f;
        } else if (isMajor) {
            // Major grid step: Bright Slate
            r = 0.42f; g = 0.50f; b = 0.62f; baseAlpha = 0.68f;
        }

        // 4 vertices per line: 3 segments with smooth edge transparency fade
        uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ minX,      y, z, r, g, b, 0.0f });
        vertices.push_back({ innerMinX, y, z, r, g, b, baseAlpha });
        vertices.push_back({ innerMaxX, y, z, r, g, b, baseAlpha });
        vertices.push_back({ maxX,      y, z, r, g, b, 0.0f });

        indices.push_back(baseIdx + 0); indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 1); indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 2); indices.push_back(baseIdx + 3);
    }

    // 2. Vertical lines (sweeping along Y, spaced along X)
    for (int i = -halfLines; i <= halfLines; ++i) {
        float x = snapX + static_cast<float>(i) * gridSize;

        float r = 0.22f, g = 0.27f, b = 0.35f, baseAlpha = 0.40f;
        bool isOriginAxis = (std::abs(x) < 0.01f);
        bool isMajor = false;

        float rem = std::abs(std::remainder(x, majorInterval));
        if (rem < 0.01f) {
            isMajor = true;
        }

        if (isOriginAxis) {
            // World Y-Axis: Vivid Green
            r = 0.25f; g = 0.90f; b = 0.35f; baseAlpha = 0.90f;
        } else if (isMajor) {
            // Major grid step: Bright Slate
            r = 0.42f; g = 0.50f; b = 0.62f; baseAlpha = 0.68f;
        }

        // 4 vertices per line: 3 segments with smooth edge transparency fade
        uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ x, minY,      z, r, g, b, 0.0f });
        vertices.push_back({ x, innerMinY, z, r, g, b, baseAlpha });
        vertices.push_back({ x, innerMaxY, z, r, g, b, baseAlpha });
        vertices.push_back({ x, maxY,      z, r, g, b, 0.0f });

        indices.push_back(baseIdx + 0); indices.push_back(baseIdx + 1);
        indices.push_back(baseIdx + 1); indices.push_back(baseIdx + 2);
        indices.push_back(baseIdx + 2); indices.push_back(baseIdx + 3);
    }

    // Upload to OpenGL buffers
    if (m_vao == 0) glGenVertexArrays(1, &m_vao);
    if (m_vbo == 0) glGenBuffers(1, &m_vbo);
    if (m_ebo == 0) glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GridVertex), vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);

    // aPos (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GridVertex), (void*)offsetof(GridVertex, x));
    glEnableVertexAttribArray(0);

    // aColor (location 1)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GridVertex), (void*)offsetof(GridVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    m_indexCount = static_cast<GLsizei>(indices.size());
}

void GridRenderer::Render(const Shader& lineShader, const Matrix4& mvp, const Vector3& camPos,
                          float gridSize, float gridElevation) {
    Update(camPos, gridSize, gridElevation);
    if (m_indexCount == 0 || m_vao == 0) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE); // Read depth to be occluded by walls, but do not write depth

    lineShader.Bind();
    lineShader.SetMat4("u_MVP", mvp);
    lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

    glLineWidth(1.0f);
    glBindVertexArray(m_vao);
    glDrawElements(GL_LINES, m_indexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    lineShader.Unbind();

    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
}
