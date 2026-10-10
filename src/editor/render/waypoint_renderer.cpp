#include "editor/render/waypoint_renderer.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

WaypointRenderer::WaypointRenderer() = default;

WaypointRenderer::~WaypointRenderer() {
    Clear();
}

void WaypointRenderer::Clear() {
    if (m_lineVAO) {
        glDeleteVertexArrays(1, &m_lineVAO);
        m_lineVAO = 0;
    }
    if (m_lineVBO) {
        glDeleteBuffers(1, &m_lineVBO);
        m_lineVBO = 0;
    }
    if (m_lineEBO) {
        glDeleteBuffers(1, &m_lineEBO);
        m_lineEBO = 0;
    }
    m_lineIndexCount = 0;
    m_nodeCount = 0;
    m_loaded = false;
}

bool WaypointRenderer::BuildFromGraph(const WaypointGraph& graph, uint32_t selectedId, uint32_t hoveredId) {
    Clear();
    if (graph.IsEmpty()) return false;

    const auto& nodes = graph.GetNodes();
    m_nodeCount = nodes.size();
    GameMod mod = graph.GetActiveMod();

    std::vector<WaypointVertex> vertices;
    std::vector<uint32_t> indices;

    for (const auto& node : nodes) {
        bool isSelected = (node.id == selectedId);
        bool isHovered = (node.id == hoveredId);

        // Determine node colors according to exact CS-EBOT / GoldSrc bot color rules:
        // By default, CS waypoints are Green (0, 255, 0)
        float r = 0.0f, g = 1.0f, b = 0.0f, a = 0.95f;

        if (node.flags & WPT_FLAG_CAMP) {
            r = 0.0f; g = 1.0f; b = 1.0f; // Cyan (0, 255, 255)
        } else if (node.flags & (WPT_FLAG_GOAL | WPT_FLAG_HELICOPTER)) {
            r = 0.50f; g = 0.0f; b = 1.0f; // Purple / Violet (128, 0, 255)
        } else if (node.flags & WPT_FLAG_LADDER) {
            r = 0.50f; g = 0.25f; b = 0.0f; // Brown (128, 64, 0)
        } else if (node.flags & WPT_FLAG_RESCUE) {
            r = 1.0f; g = 1.0f; b = 1.0f; // White (255, 255, 255)
        } else if (node.flags & WPT_FLAG_AVOID) {
            r = 1.0f; g = 0.0f; b = 0.0f; // Red (255, 0, 0)
        } else if (node.flags & WPT_FLAG_FALLCHECK) {
            r = 0.50f; g = 0.50f; b = 0.50f; // Gray (128, 128, 128)
        } else if (node.flags & WPT_FLAG_USEBUTTON) {
            r = 0.0f; g = 0.0f; b = 1.0f; // Blue (0, 0, 255)
        } else if (node.flags & WPT_FLAG_ZMHMCAMP) {
            r = 0.78f; g = 0.27f; b = 0.82f; // Magenta (199, 69, 209)
        } else if (node.flags & WPT_FLAG_HMCAMPMESH) {
            r = 0.20f; g = 0.49f; b = 1.0f; // Sky Blue (50, 125, 255)
        } else if (node.flags & WPT_FLAG_ZOMBIEONLY) {
            r = 1.0f; g = 0.0f; b = 0.0f; // Red (255, 0, 0)
        } else if (node.flags & WPT_FLAG_HUMANONLY) {
            r = 0.0f; g = 0.0f; b = 1.0f; // Blue (0, 0, 255)
        } else if (node.flags & WPT_FLAG_ZOMBIEPUSH) {
            r = 0.98f; g = 0.29f; b = 0.59f; // Pink / Salmon (250, 75, 150)
        } else if (node.flags & (WPT_FLAG_FALLRISK | WPT_FLAG_SPECIFICGRAVITY)) {
            r = 0.50f; g = 0.50f; b = 0.50f; // Gray (128, 128, 128)
        } else if (node.flags & WPT_FLAG_ONLYONE) {
            r = 1.0f; g = 1.0f; b = 0.0f; // Yellow (255, 255, 0)
        } else if (node.flags & WPT_FLAG_WAITUNTIL) {
            r = 0.0f; g = 0.0f; b = 1.0f; // Blue (0, 0, 255)
        } else if (node.flags & WPT_FLAG_TERRORIST) {
            r = 1.0f; g = 0.0f; b = 0.0f; // Red (255, 0, 0)
        } else if (node.flags & WPT_FLAG_COUNTER) {
            r = 0.0f; g = 0.0f; b = 1.0f; // Blue (0, 0, 255)
        } else if (node.flags & WPT_FLAG_SNIPER) {
            r = 0.51f; g = 0.34f; b = 0.0f; // Brown / Amber (130, 87, 0)
        }

        // Subflag tactical color for upper quarter of the vertical pillar
        float fr = r, fg = g, fb = b;
        bool hasFlagColor = false;
        if (node.flags & WPT_FLAG_SNIPER) {
            fr = 0.51f; fg = 0.34f; fb = 0.0f; hasFlagColor = true;
        } else if (node.flags & WPT_FLAG_TERRORIST) {
            fr = 1.0f; fg = 0.0f; fb = 0.0f; hasFlagColor = true;
        } else if (node.flags & WPT_FLAG_COUNTER) {
            fr = 0.0f; fg = 0.0f; fb = 1.0f; hasFlagColor = true;
        } else if (node.flags & (WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH)) {
            fr = 0.0f; fg = 0.0f; fb = 1.0f; hasFlagColor = true;
        } else if (node.flags & (WPT_FLAG_ZOMBIEONLY | WPT_FLAG_HUMANONLY)) {
            fr = 1.0f; fg = 0.0f; fb = 1.0f; hasFlagColor = true;
        } else if (node.flags & WPT_FLAG_ZOMBIEPUSH) {
            fr = 1.0f; fg = 0.0f; fb = 0.0f; hasFlagColor = true;
        } else if (node.flags & (WPT_FLAG_FALLRISK | WPT_FLAG_WAITUNTIL)) {
            fr = 0.98f; fg = 0.29f; fb = 0.59f; hasFlagColor = true;
        } else if (node.flags & WPT_FLAG_SPECIFICGRAVITY) {
            fr = 0.50f; fg = 0.0f; fb = 1.0f; hasFlagColor = true;
        }

        if (isSelected) {
            r = 1.0f; g = 0.95f; b = 0.1f; a = 1.0f; // Highlight Yellow
            fr = 1.0f; fg = 0.95f; fb = 0.1f;
        } else if (isHovered) {
            r = std::min(1.0f, r + 0.3f);
            g = std::min(1.0f, g + 0.3f);
            b = std::min(1.0f, b + 0.3f);
        }

        // 1. Draw In-Game CS Vertical Pillar Beam (Floor to Stand/Crouch Player Height)
        float pillarHeight = (node.flags & WPT_FLAG_CROUCH) ? 36.0f : 72.0f;
        Vector3 floorPt = node.origin;
        Vector3 topPt   = node.origin + Vector3(0.0f, 0.0f, pillarHeight);

        uint32_t pillarBase = static_cast<uint32_t>(vertices.size());
        if (hasFlagColor && !isSelected) {
            Vector3 midPt = node.origin + Vector3(0.0f, 0.0f, pillarHeight * 0.75f);
            // Lower 75% base color
            vertices.push_back({ floorPt.x, floorPt.y, floorPt.z, 0,0,1, 0,0, r, g, b, 0.85f });
            vertices.push_back({ midPt.x, midPt.y, midPt.z, 0,0,1, 0,0, r, g, b, 0.85f });
            // Upper 25% flag color
            vertices.push_back({ midPt.x, midPt.y, midPt.z, 0,0,1, 0,0, fr, fg, fb, 0.95f });
            vertices.push_back({ topPt.x, topPt.y, topPt.z, 0,0,1, 0,0, fr, fg, fb, 0.95f });
            indices.push_back(pillarBase + 0); indices.push_back(pillarBase + 1);
            indices.push_back(pillarBase + 2); indices.push_back(pillarBase + 3);
        } else {
            vertices.push_back({ floorPt.x, floorPt.y, floorPt.z, 0,0,1, 0,0, r, g, b, 0.85f });
            vertices.push_back({ topPt.x, topPt.y, topPt.z, 0,0,1, 0,0, r, g, b, 0.85f });
            indices.push_back(pillarBase + 0); indices.push_back(pillarBase + 1);
        }

        // 2. Draw 3D Cross at Center (Elevated for clear 3D selection)
        Vector3 pos = node.origin + Vector3(0.0f, 0.0f, pillarHeight * 0.5f);
        float sz = isSelected ? 12.0f : 6.0f;

        uint32_t cBase = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ pos.x - sz, pos.y, pos.z, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x + sz, pos.y, pos.z, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x, pos.y - sz, pos.z, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x, pos.y + sz, pos.z, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x, pos.y, pos.z - sz, 0,0,1, 0,0, r, g, b, a });
        vertices.push_back({ pos.x, pos.y, pos.z + sz, 0,0,1, 0,0, r, g, b, a });

        indices.push_back(cBase + 0); indices.push_back(cBase + 1);
        indices.push_back(cBase + 2); indices.push_back(cBase + 3);
        indices.push_back(cBase + 4); indices.push_back(cBase + 5);

        // 3. Ground Projection Circle (Tolerance radius)
        float rad = (m_showRadii || isSelected) ? std::max(8.0f, node.radius) : 8.0f;
        int segments = isSelected ? 16 : 8;
        uint32_t circleBase = static_cast<uint32_t>(vertices.size());
        float circleAlpha = isSelected ? 0.9f : 0.45f;

        for (int s = 0; s < segments; ++s) {
            float theta = (2.0f * static_cast<float>(M_PI) * s) / segments;
            float cx = node.origin.x + std::cos(theta) * rad;
            float cy = node.origin.y + std::sin(theta) * rad;
            vertices.push_back({ cx, cy, node.origin.z + 1.0f, 0,0,1, 0,0, r, g, b, circleAlpha });
        }

        for (int s = 0; s < segments; ++s) {
            indices.push_back(circleBase + s);
            indices.push_back(circleBase + ((s + 1) % segments));
        }

        // 4. Camp / Sniper Aim Direction Vector
        if (node.flags & (WPT_FLAG_CAMP | WPT_FLAG_SNIPER | WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH)) {
            float yawRad = node.campYaw * static_cast<float>(M_PI) / 180.0f;
            float pitchRad = node.campPitch * static_cast<float>(M_PI) / 180.0f;

            Vector3 aimDir(
                std::cos(pitchRad) * std::cos(yawRad),
                std::cos(pitchRad) * std::sin(yawRad),
                std::sin(pitchRad)
            );

            Vector3 aimEnd = pos + aimDir * 32.0f;
            uint32_t aimBase = static_cast<uint32_t>(vertices.size());
            vertices.push_back({ pos.x, pos.y, pos.z, 0,0,1, 0,0, 1.0f, 1.0f, 0.0f, 1.0f });
            vertices.push_back({ aimEnd.x, aimEnd.y, aimEnd.z, 0,0,1, 0,0, 1.0f, 0.2f, 0.2f, 1.0f });
            indices.push_back(aimBase + 0);
            indices.push_back(aimBase + 1);
        }

        // 5. Outgoing Path Connection Links (Exact CS-EBOT Link Beam Colors)
        if (m_showConnections) {
            for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                int16_t targetId = node.connections[c];
                if (targetId <= 0) continue;

                const WaypointNode* target = graph.GetNodeByID(static_cast<uint32_t>(targetId));
                if (!target) continue;

                // Don't draw link twice if bidirectional and targetId < node.id
                bool targetHasReverse = target->HasConnectionTo(static_cast<int16_t>(node.id));
                if (targetHasReverse && target->id < node.id) continue;

                Vector3 startPt = node.origin + Vector3(0.0f, 0.0f, 10.0f);
                Vector3 endPt   = target->origin + Vector3(0.0f, 0.0f, 10.0f);

                // CS-EBOT Connection Beam Colors:
                // Jumping: Red (255, 0, 0)
                // Double Jump: Blue (0, 0, 255)
                // Visible: Green (0, 255, 0)
                // Bothways / 2-Way: Yellow (255, 255, 0)
                // Oneway Outgoing: White (250, 250, 250)
                float lr = 0.98f, lg = 0.98f, lb = 0.98f, la = 0.90f; // Default Oneway: White

                if (node.connectionFlags[c] & WPT_CONN_JUMP) {
                    lr = 1.0f; lg = 0.0f; lb = 0.0f; // Red: Jumping
                } else if (node.connectionFlags[c] & WPT_CONN_DOUBLE) {
                    lr = 0.0f; lg = 0.0f; lb = 1.0f; // Blue: Double-jump
                } else if (node.connectionFlags[c] & WPT_CONN_VISIBLE) {
                    lr = 0.0f; lg = 1.0f; lb = 0.0f; // Green: Line of sight clear
                } else if (node.connectionFlags[c] & WPT_CONN_CROUCH) {
                    lr = 1.0f; lg = 0.65f; lb = 0.0f; // Orange: Crouch ducking connection
                } else if (targetHasReverse) {
                    lr = 1.0f; lg = 1.0f; lb = 0.0f; // Yellow: Two-way bothways
                }

                uint32_t lBase = static_cast<uint32_t>(vertices.size());
                vertices.push_back({ startPt.x, startPt.y, startPt.z, 0,0,1, 0,0, lr, lg, lb, la });
                vertices.push_back({ endPt.x, endPt.y, endPt.z, 0,0,1, 0,0, lr, lg, lb, la });
                indices.push_back(lBase + 0);
                indices.push_back(lBase + 1);

                // If one-way link, draw directional arrow head
                if (!targetHasReverse) {
                    Vector3 fwd = (endPt - startPt).Normalized();
                    Vector3 up(0, 0, 1);
                    Vector3 side = fwd.Cross(up).Normalized();
                    Vector3 mid = (startPt + endPt) * 0.5f;

                    Vector3 a1 = mid - fwd * 8.0f + side * 4.0f;
                    Vector3 a2 = mid - fwd * 8.0f - side * 4.0f;

                    uint32_t arrBase = static_cast<uint32_t>(vertices.size());
                    vertices.push_back({ mid.x, mid.y, mid.z, 0,0,1, 0,0, lr, lg, lb, 1.0f });
                    vertices.push_back({ a1.x, a1.y, a1.z, 0,0,1, 0,0, lr, lg, lb, 1.0f });
                    vertices.push_back({ a2.x, a2.y, a2.z, 0,0,1, 0,0, lr, lg, lb, 1.0f });

                    indices.push_back(arrBase + 0); indices.push_back(arrBase + 1);
                    indices.push_back(arrBase + 0); indices.push_back(arrBase + 2);
                }
            }
        }
    }

    if (indices.empty()) return false;

    glGenVertexArrays(1, &m_lineVAO);
    glGenBuffers(1, &m_lineVBO);
    glGenBuffers(1, &m_lineEBO);

    glBindVertexArray(m_lineVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(WaypointVertex), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_lineEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(uint32_t), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WaypointVertex), (void*)offsetof(WaypointVertex, x));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(WaypointVertex), (void*)offsetof(WaypointVertex, r));
    glEnableVertexAttribArray(1);

    glBindVertexArray(0);
    m_lineIndexCount = indices.size();
    m_loaded = true;

    return true;
}

void WaypointRenderer::Render(const Shader& lineShader, const Matrix4& mvp) {
    if (!m_loaded || !m_showWaypoints || m_lineIndexCount == 0) return;

    lineShader.Bind();
    lineShader.SetMat4("u_MVP", mvp);
    lineShader.SetVec4("u_Color", 1.0f, 1.0f, 1.0f, 1.0f);

    glLineWidth(2.0f);
    glBindVertexArray(m_lineVAO);
    glDrawElements(GL_LINES, static_cast<GLsizei>(m_lineIndexCount), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    lineShader.Unbind();
}
