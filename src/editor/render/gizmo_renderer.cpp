#include "editor/render/gizmo_renderer.h"
#include <cmath>
#include <algorithm>

static const float kPi = 3.14159265358979323846f;

GizmoRenderer::GizmoRenderer()
    : m_vao(0)
    , m_vbo(0)
    , m_ebo(0)
    , m_indexCount(0)
{
}

GizmoRenderer::~GizmoRenderer() {
    Clear();
}

void GizmoRenderer::Clear() {
    if (m_vao != 0) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
    if (m_vbo != 0) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
    if (m_ebo != 0) { glDeleteBuffers(1, &m_ebo); m_ebo = 0; }
    m_indexCount = 0;
}

void GizmoRenderer::BuildBuffers(const Vector3& center, const Vector3& camPos,
                                 GizmoMode mode,
                                 SelectedHandleType hoveredHandle,
                                 SelectedHandleType selectedHandle) {
    std::vector<GizmoVertex> vertices;
    std::vector<uint32_t> indices;

    float camDist = (camPos - center).Length();
    float gLen = std::max(42.0f, std::min(130.0f, camDist * 0.082f));

    float coneH = gLen * 0.22f;
    float coneR = gLen * 0.08f;
    float cubeDist = gLen * 0.72f;
    float cubeH = gLen * 0.065f;
    float rotR = gLen * 0.58f;
    float screenR = gLen * 0.88f;

    float planeDist = gLen * 0.35f;
    float planeSize = gLen * 0.18f;

    auto AddLine = [&](const Vector3& p0, const Vector3& p1, float r, float g, float b, float a) {
        uint32_t baseIdx = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ p0.x, p0.y, p0.z, r, g, b, a });
        vertices.push_back({ p1.x, p1.y, p1.z, r, g, b, a });
        indices.push_back(baseIdx + 0);
        indices.push_back(baseIdx + 1);
    };

    auto AddPlaneQuad = [&](const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3,
                            float r, float g, float b, float a) {
        AddLine(p0, p1, r, g, b, a);
        AddLine(p1, p2, r, g, b, a);
        AddLine(p2, p3, r, g, b, a);
        AddLine(p3, p0, r, g, b, a);
        Vector3 m01 = (p0 + p1) * 0.5f;
        Vector3 m23 = (p2 + p3) * 0.5f;
        Vector3 m12 = (p1 + p2) * 0.5f;
        Vector3 m30 = (p3 + p0) * 0.5f;
        AddLine(m01, m23, r, g, b, a * 0.75f);
        AddLine(m12, m30, r, g, b, a * 0.75f);
        AddLine(p0, p2, r, g, b, a * 0.40f);
        AddLine(p1, p3, r, g, b, a * 0.40f);
    };

    auto AddBox = [&](const Vector3& bCenter, float hSize, float r, float g, float b, float a) {
        Vector3 c0(bCenter.x - hSize, bCenter.y - hSize, bCenter.z - hSize);
        Vector3 c1(bCenter.x + hSize, bCenter.y - hSize, bCenter.z - hSize);
        Vector3 c2(bCenter.x + hSize, bCenter.y + hSize, bCenter.z - hSize);
        Vector3 c3(bCenter.x - hSize, bCenter.y + hSize, bCenter.z - hSize);
        Vector3 c4(bCenter.x - hSize, bCenter.y - hSize, bCenter.z + hSize);
        Vector3 c5(bCenter.x + hSize, bCenter.y - hSize, bCenter.z + hSize);
        Vector3 c6(bCenter.x + hSize, bCenter.y + hSize, bCenter.z + hSize);
        Vector3 c7(bCenter.x - hSize, bCenter.y + hSize, bCenter.z + hSize);

        // Bottom quad
        AddLine(c0, c1, r, g, b, a); AddLine(c1, c2, r, g, b, a);
        AddLine(c2, c3, r, g, b, a); AddLine(c3, c0, r, g, b, a);
        // Top quad
        AddLine(c4, c5, r, g, b, a); AddLine(c5, c6, r, g, b, a);
        AddLine(c6, c7, r, g, b, a); AddLine(c7, c4, r, g, b, a);
        // Vertical pillars
        AddLine(c0, c4, r, g, b, a); AddLine(c1, c5, r, g, b, a);
        AddLine(c2, c6, r, g, b, a); AddLine(c3, c7, r, g, b, a);
    };

    auto AddCone = [&](const Vector3& baseP, const Vector3& tipP, const Vector3& uDir, const Vector3& vDir,
                       float r, float g, float b, float a) {
        const int kSides = 8;
        Vector3 basePts[kSides];
        for (int k = 0; k < kSides; ++k) {
            float ang = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(kSides);
            basePts[k] = baseP + uDir * (coneR * std::cos(ang)) + vDir * (coneR * std::sin(ang));
        }
        for (int k = 0; k < kSides; ++k) {
            int nextK = (k + 1) % kSides;
            AddLine(basePts[k], basePts[nextK], r, g, b, a);
            AddLine(basePts[k], tipP, r, g, b, a);
        }
    };

    auto AddCircle = [&](const Vector3& orig, const Vector3& uDir, const Vector3& vDir, float radius,
                         float r, float g, float b, float a) {
        const int kSegs = 36;
        Vector3 prevP = orig + uDir * radius;
        for (int k = 1; k <= kSegs; ++k) {
            float ang = 2.0f * kPi * static_cast<float>(k) / static_cast<float>(kSegs);
            Vector3 curP = orig + uDir * (radius * std::cos(ang)) + vDir * (radius * std::sin(ang));
            AddLine(prevP, curP, r, g, b, a);
            prevP = curP;
        }
    };

    // Calculate view-aligned axes for camera-facing circles
    Vector3 fwd = (camPos - center).Normalized();
    Vector3 upGuide(0.0f, 0.0f, 1.0f);
    if (std::abs(fwd.z) > 0.92f) upGuide = Vector3(0.0f, 1.0f, 0.0f);
    Vector3 right = fwd.Cross(upGuide).Normalized();
    Vector3 up = right.Cross(fwd).Normalized();

    // 1. Center Translation Diamond / Sphere (View / Free Move)
    {
        bool isCenterActive = (hoveredHandle == HANDLE_GIZMO_CENTER || selectedHandle == HANDLE_GIZMO_CENTER);
        float cr = isCenterActive ? 1.0f : 0.85f;
        float cg = isCenterActive ? 0.95f : 0.85f;
        float cb = isCenterActive ? 0.20f : 0.85f;
        float cSize = isCenterActive ? (gLen * 0.11f) : (gLen * 0.08f);

        Vector3 d0(center.x - cSize, center.y, center.z);
        Vector3 d1(center.x, center.y + cSize, center.z);
        Vector3 d2(center.x + cSize, center.y, center.z);
        Vector3 d3(center.x, center.y - cSize, center.z);
        AddLine(d0, d1, cr, cg, cb, 1.0f);
        AddLine(d1, d2, cr, cg, cb, 1.0f);
        AddLine(d2, d3, cr, cg, cb, 1.0f);
        AddLine(d3, d0, cr, cg, cb, 1.0f);

        Vector3 dz0(center.x, center.y, center.z - cSize);
        Vector3 dz1(center.x, center.y, center.z + cSize);
        AddLine(d0, dz0, cr, cg, cb, 0.7f); AddLine(d0, dz1, cr, cg, cb, 0.7f);
        AddLine(d1, dz0, cr, cg, cb, 0.7f); AddLine(d1, dz1, cr, cg, cb, 0.7f);
        AddLine(d2, dz0, cr, cg, cb, 0.7f); AddLine(d2, dz1, cr, cg, cb, 0.7f);
        AddLine(d3, dz0, cr, cg, cb, 0.7f); AddLine(d3, dz1, cr, cg, cb, 0.7f);

        // Center view circle
        AddCircle(center, right, up, cSize * 1.35f, cr, cg, cb, 0.65f);
    }

    // 2. Translate Arrows (+X East, +Y North, +Z Up/Elevation)
    if (mode == GIZMO_MODE_COMBINED || mode == GIZMO_MODE_TRANSLATE) {
        // X-Axis (Red)
        {
            bool isXActive = (hoveredHandle == HANDLE_GIZMO_X || selectedHandle == HANDLE_GIZMO_X);
            float r = isXActive ? 1.0f : 0.95f;
            float g = isXActive ? 0.95f : 0.22f;
            float b = isXActive ? 0.20f : 0.22f;
            Vector3 baseP = center + Vector3(gLen, 0.0f, 0.0f);
            Vector3 tipP = center + Vector3(gLen + coneH, 0.0f, 0.0f);
            AddLine(center, baseP, r, g, b, 1.0f);
            AddCone(baseP, tipP, Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), r, g, b, 1.0f);
        }

        // Y-Axis (Green)
        {
            bool isYActive = (hoveredHandle == HANDLE_GIZMO_Y || selectedHandle == HANDLE_GIZMO_Y);
            float r = isYActive ? 1.0f : 0.22f;
            float g = isYActive ? 0.95f : 0.92f;
            float b = isYActive ? 0.20f : 0.32f;
            Vector3 baseP = center + Vector3(0.0f, gLen, 0.0f);
            Vector3 tipP = center + Vector3(0.0f, gLen + coneH, 0.0f);
            AddLine(center, baseP, r, g, b, 1.0f);
            AddCone(baseP, tipP, Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), r, g, b, 1.0f);
        }

        // Z-Axis (Blue - Up/Down Elevation)
        {
            bool isZActive = (hoveredHandle == HANDLE_GIZMO_Z || selectedHandle == HANDLE_GIZMO_Z);
            float r = isZActive ? 1.0f : 0.25f;
            float g = isZActive ? 0.95f : 0.58f;
            float b = isZActive ? 0.20f : 1.0f;
            Vector3 baseP = center + Vector3(0.0f, 0.0f, gLen);
            Vector3 tipP = center + Vector3(0.0f, 0.0f, gLen + coneH);
            AddLine(center, baseP, r, g, b, 1.0f);
            AddCone(baseP, tipP, Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), r, g, b, 1.0f);
        }

        // Planar Translation Quads (XY Ground, XZ East-West Vert, YZ North-South Vert)
        {
            // XY Plane (Blue)
            bool isXYActive = (hoveredHandle == HANDLE_PLANE_XY || selectedHandle == HANDLE_PLANE_XY);
            float r = isXYActive ? 1.0f : 0.25f;
            float g = isXYActive ? 0.95f : 0.58f;
            float b = isXYActive ? 0.20f : 1.0f;
            float a = isXYActive ? 1.0f : 0.85f;
            Vector3 xy0 = center + Vector3(planeDist, planeDist, 0.0f);
            Vector3 xy1 = center + Vector3(planeDist + planeSize, planeDist, 0.0f);
            Vector3 xy2 = center + Vector3(planeDist + planeSize, planeDist + planeSize, 0.0f);
            Vector3 xy3 = center + Vector3(planeDist, planeDist + planeSize, 0.0f);
            AddPlaneQuad(xy0, xy1, xy2, xy3, r, g, b, a);
        }
        {
            // XZ Plane (Green)
            bool isXZActive = (hoveredHandle == HANDLE_PLANE_XZ || selectedHandle == HANDLE_PLANE_XZ);
            float r = isXZActive ? 1.0f : 0.22f;
            float g = isXZActive ? 0.95f : 0.92f;
            float b = isXZActive ? 0.20f : 0.32f;
            float a = isXZActive ? 1.0f : 0.85f;
            Vector3 xz0 = center + Vector3(planeDist, 0.0f, planeDist);
            Vector3 xz1 = center + Vector3(planeDist + planeSize, 0.0f, planeDist);
            Vector3 xz2 = center + Vector3(planeDist + planeSize, 0.0f, planeDist + planeSize);
            Vector3 xz3 = center + Vector3(planeDist, 0.0f, planeDist + planeSize);
            AddPlaneQuad(xz0, xz1, xz2, xz3, r, g, b, a);
        }
        {
            // YZ Plane (Red)
            bool isYZActive = (hoveredHandle == HANDLE_PLANE_YZ || selectedHandle == HANDLE_PLANE_YZ);
            float r = isYZActive ? 1.0f : 0.95f;
            float g = isYZActive ? 0.95f : 0.22f;
            float b = isYZActive ? 0.20f : 0.22f;
            float a = isYZActive ? 1.0f : 0.85f;
            Vector3 yz0 = center + Vector3(0.0f, planeDist, planeDist);
            Vector3 yz1 = center + Vector3(0.0f, planeDist + planeSize, planeDist);
            Vector3 yz2 = center + Vector3(0.0f, planeDist + planeSize, planeDist + planeSize);
            Vector3 yz3 = center + Vector3(0.0f, planeDist, planeDist + planeSize);
            AddPlaneQuad(yz0, yz1, yz2, yz3, r, g, b, a);
        }
    }

    // 3. Scale Handles (3D Cube Boxes on +X, +Y, +Z axes and Planar Scale Quads)
    if (mode == GIZMO_MODE_COMBINED || mode == GIZMO_MODE_SCALE) {
        // In Scale mode, draw axis lines connecting center to boxes
        if (mode == GIZMO_MODE_SCALE) {
            AddLine(center, center + Vector3(cubeDist, 0.0f, 0.0f), 0.95f, 0.25f, 0.25f, 0.85f);
            AddLine(center, center + Vector3(0.0f, cubeDist, 0.0f), 0.25f, 0.92f, 0.35f, 0.85f);
            AddLine(center, center + Vector3(0.0f, 0.0f, cubeDist), 0.25f, 0.58f, 1.0f, 0.85f);
        }

        // Scale X Cube
        {
            bool isScXActive = (hoveredHandle == HANDLE_SCALE_X || selectedHandle == HANDLE_SCALE_X);
            float r = isScXActive ? 1.0f : 0.95f;
            float g = isScXActive ? 0.95f : 0.28f;
            float b = isScXActive ? 0.20f : 0.28f;
            Vector3 bCenter = center + Vector3(cubeDist, 0.0f, 0.0f);
            AddBox(bCenter, isScXActive ? (cubeH * 1.3f) : cubeH, r, g, b, 1.0f);
        }

        // Scale Y Cube
        {
            bool isScYActive = (hoveredHandle == HANDLE_SCALE_Y || selectedHandle == HANDLE_SCALE_Y);
            float r = isScYActive ? 1.0f : 0.28f;
            float g = isScYActive ? 0.95f : 0.92f;
            float b = isScYActive ? 0.20f : 0.35f;
            Vector3 bCenter = center + Vector3(0.0f, cubeDist, 0.0f);
            AddBox(bCenter, isScYActive ? (cubeH * 1.3f) : cubeH, r, g, b, 1.0f);
        }

        // Scale Z Cube
        {
            bool isScZActive = (hoveredHandle == HANDLE_SCALE_Z || selectedHandle == HANDLE_SCALE_Z);
            float r = isScZActive ? 1.0f : 0.30f;
            float g = isScZActive ? 0.95f : 0.60f;
            float b = isScZActive ? 0.20f : 1.0f;
            Vector3 bCenter = center + Vector3(0.0f, 0.0f, cubeDist);
            AddBox(bCenter, isScZActive ? (cubeH * 1.3f) : cubeH, r, g, b, 1.0f);
        }

        // Planar Scale Quads in Scale Mode
        if (mode == GIZMO_MODE_SCALE) {
            {
                // XY Plane Scale (Blue)
                bool isXYActive = (hoveredHandle == HANDLE_SCALE_PLANE_XY || selectedHandle == HANDLE_SCALE_PLANE_XY);
                float r = isXYActive ? 1.0f : 0.25f;
                float g = isXYActive ? 0.95f : 0.58f;
                float b = isXYActive ? 0.20f : 1.0f;
                float a = isXYActive ? 1.0f : 0.85f;
                Vector3 xy0 = center + Vector3(planeDist, planeDist, 0.0f);
                Vector3 xy1 = center + Vector3(planeDist + planeSize, planeDist, 0.0f);
                Vector3 xy2 = center + Vector3(planeDist + planeSize, planeDist + planeSize, 0.0f);
                Vector3 xy3 = center + Vector3(planeDist, planeDist + planeSize, 0.0f);
                AddPlaneQuad(xy0, xy1, xy2, xy3, r, g, b, a);
            }
            {
                // XZ Plane Scale (Green)
                bool isXZActive = (hoveredHandle == HANDLE_SCALE_PLANE_XZ || selectedHandle == HANDLE_SCALE_PLANE_XZ);
                float r = isXZActive ? 1.0f : 0.22f;
                float g = isXZActive ? 0.95f : 0.92f;
                float b = isXZActive ? 0.20f : 0.32f;
                float a = isXZActive ? 1.0f : 0.85f;
                Vector3 xz0 = center + Vector3(planeDist, 0.0f, planeDist);
                Vector3 xz1 = center + Vector3(planeDist + planeSize, 0.0f, planeDist);
                Vector3 xz2 = center + Vector3(planeDist + planeSize, 0.0f, planeDist + planeSize);
                Vector3 xz3 = center + Vector3(planeDist, 0.0f, planeDist + planeSize);
                AddPlaneQuad(xz0, xz1, xz2, xz3, r, g, b, a);
            }
            {
                // YZ Plane Scale (Red)
                bool isYZActive = (hoveredHandle == HANDLE_SCALE_PLANE_YZ || selectedHandle == HANDLE_SCALE_PLANE_YZ);
                float r = isYZActive ? 1.0f : 0.95f;
                float g = isYZActive ? 0.95f : 0.22f;
                float b = isYZActive ? 0.20f : 0.22f;
                float a = isYZActive ? 1.0f : 0.85f;
                Vector3 yz0 = center + Vector3(0.0f, planeDist, planeDist);
                Vector3 yz1 = center + Vector3(0.0f, planeDist + planeSize, planeDist);
                Vector3 yz2 = center + Vector3(0.0f, planeDist + planeSize, planeDist + planeSize);
                Vector3 yz3 = center + Vector3(0.0f, planeDist, planeDist + planeSize);
                AddPlaneQuad(yz0, yz1, yz2, yz3, r, g, b, a);
            }

            // Outer Uniform Scale Ring (White / Gold)
            {
                bool isUniActive = (hoveredHandle == HANDLE_SCALE_UNIFORM || selectedHandle == HANDLE_SCALE_UNIFORM);
                float ur = isUniActive ? 1.0f : 0.88f;
                float ug = isUniActive ? 0.95f : 0.88f;
                float ub = isUniActive ? 0.20f : 0.90f;
                float ua = isUniActive ? 1.0f : 0.60f;
                AddCircle(center, right, up, screenR * 1.05f, ur, ug, ub, ua);
            }
        }
    }

    // 4. Rotate Rings (Circles in XY, YZ, XZ planes and screen trackball)
    if (mode == GIZMO_MODE_COMBINED || mode == GIZMO_MODE_ROTATE) {
        // Blue Ring: Yaw / Z-Axis (in XY plane, normal = +Z)
        {
            bool isRotZActive = (hoveredHandle == HANDLE_ROTATE_Z || selectedHandle == HANDLE_ROTATE_Z);
            float r = isRotZActive ? 1.0f : 0.28f;
            float g = isRotZActive ? 0.95f : 0.60f;
            float b = isRotZActive ? 0.20f : 1.0f;
            float a = isRotZActive ? 1.0f : 0.85f;
            AddCircle(center, Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), rotR, r, g, b, a);
        }

        // Red Ring: Pitch / X-Axis (in YZ plane, normal = +X)
        {
            bool isRotXActive = (hoveredHandle == HANDLE_ROTATE_X || selectedHandle == HANDLE_ROTATE_X);
            float r = isRotXActive ? 1.0f : 0.92f;
            float g = isRotXActive ? 0.95f : 0.25f;
            float b = isRotXActive ? 0.20f : 0.25f;
            float a = isRotXActive ? 1.0f : 0.85f;
            AddCircle(center, Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), rotR, r, g, b, a);
        }

        // Green Ring: Roll / Y-Axis (in XZ plane, normal = +Y)
        {
            bool isRotYActive = (hoveredHandle == HANDLE_ROTATE_Y || selectedHandle == HANDLE_ROTATE_Y);
            float r = isRotYActive ? 1.0f : 0.25f;
            float g = isRotYActive ? 0.95f : 0.88f;
            float b = isRotYActive ? 0.20f : 0.35f;
            float a = isRotYActive ? 1.0f : 0.85f;
            AddCircle(center, Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), rotR, r, g, b, a);
        }

        // White Outer Trackball Screen Ring
        {
            bool isRotScreenActive = (hoveredHandle == HANDLE_ROTATE_SCREEN || selectedHandle == HANDLE_ROTATE_SCREEN);
            float r = isRotScreenActive ? 1.0f : 0.85f;
            float g = isRotScreenActive ? 0.95f : 0.85f;
            float b = isRotScreenActive ? 0.20f : 0.88f;
            float a = isRotScreenActive ? 1.0f : 0.50f;
            AddCircle(center, right, up, screenR, r, g, b, a);
        }
    }

    // Upload to OpenGL buffers
    if (m_vao == 0) glGenVertexArrays(1, &m_vao);
    if (m_vbo == 0) glGenBuffers(1, &m_vbo);
    if (m_ebo == 0) glGenBuffers(1, &m_ebo);

    glBindVertexArray(m_vao);

    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GizmoVertex), vertices.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_DYNAMIC_DRAW);

    // aPos (location 0)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GizmoVertex), (void*)offsetof(GizmoVertex, x));
    glEnableVertexAttribArray(0);

    // aColor (location 1)
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GizmoVertex), (void*)offsetof(GizmoVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);

    m_indexCount = static_cast<GLsizei>(indices.size());
}

void GizmoRenderer::Render(const Shader& lineShader, const Matrix4& mvp,
                           const Vector3& center, const Vector3& camPos,
                           GizmoMode mode,
                           SelectedHandleType hoveredHandle,
                           SelectedHandleType selectedHandle) {
    BuildBuffers(center, camPos, mode, hoveredHandle, selectedHandle);
    if (m_indexCount == 0 || m_vao == 0) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE); // High-visibility overlay without overwriting depth buffer

    lineShader.Bind();
    lineShader.SetMat4("u_MVP", mvp);
    lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

    glLineWidth(2.2f);
    glBindVertexArray(m_vao);
    glDrawElements(GL_LINES, m_indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    lineShader.Unbind();

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
}

void GizmoRenderer::RenderLineSegment(const Shader& lineShader, const Matrix4& mvp,
                                      const Vector3& p0, const Vector3& p1,
                                      float r, float g, float b, float a,
                                      float lineWidth) {
    if (m_vao == 0) {
        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);
    }

    GizmoVertex v[2] = {
        { p0.x, p0.y, p0.z, r, g, b, a },
        { p1.x, p1.y, p1.z, r, g, b, a }
    };
    uint32_t indices[2] = { 0, 1 };

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_DYNAMIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GizmoVertex), (void*)offsetof(GizmoVertex, x));
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(GizmoVertex), (void*)offsetof(GizmoVertex, r));
    glEnableVertexAttribArray(1);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);

    lineShader.Bind();
    lineShader.SetMat4("u_MVP", mvp);
    lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

    glLineWidth(lineWidth);
    glDrawElements(GL_LINES, 2, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
    lineShader.Unbind();

    glLineWidth(1.0f);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);
    glDisable(GL_BLEND);
}
