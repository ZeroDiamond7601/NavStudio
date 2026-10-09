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

        // Determine node color according to active GameMod and tactical flags
        float r = 0.85f, g = 0.9f, b = 0.95f, a = 0.9f;

        if (mod == GameMod::ZombiePlague) {
            if (node.flags & WPT_FLAG_HMCAMPMESH) {
                r = 0.1f; g = 1.0f; b = 0.2f; // Neon Green (Human Camp)
            } else if (node.flags & WPT_FLAG_ZMHMCAMP) {
                r = 0.9f; g = 0.2f; b = 0.95f; // Neon Purple (Zombie Camp)
            } else if (node.flags & WPT_FLAG_DJUMP) {
                r = 1.0f; g = 0.65f; b = 0.0f; // Orange (Double Jump / Boost)
            } else if (node.flags & WPT_FLAG_HELICOPTER) {
                r = 1.0f; g = 1.0f; b = 0.2f; // Yellow (Helicopter Escape)
            } else if (node.flags & WPT_FLAG_ZOMBIEONLY) {
                r = 1.0f; g = 0.25f; b = 0.15f; // Red (Zombie Only)
            } else if (node.flags & WPT_FLAG_HUMANONLY) {
                r = 0.2f; g = 0.9f; b = 1.0f; // Cyan (Human Only)
            } else if (node.flags & WPT_FLAG_LADDER) {
                r = 0.2f; g = 0.85f; b = 0.95f;
            } else {
                r = 0.4f; g = 0.85f; b = 0.5f;
            }
        } else {
            // Standard CS
            if (node.flags & WPT_FLAG_TERRORIST) {
                r = 0.95f; g = 0.2f; b = 0.2f; // Red (T)
            } else if (node.flags & WPT_FLAG_COUNTER) {
                r = 0.2f; g = 0.5f; b = 1.0f; // Blue (CT)
            } else if (node.flags & WPT_FLAG_GOAL) {
                r = 0.2f; g = 1.0f; b = 0.3f; // Green (Bomb/Hostage Goal)
            } else if (node.flags & WPT_FLAG_SNIPER) {
                r = 0.95f; g = 0.2f; b = 0.85f; // Magenta (Sniper Spot)
            } else if (node.flags & WPT_FLAG_CAMP) {
                r = 1.0f; g = 0.85f; b = 0.2f; // Amber (Camp Point)
            } else if (node.flags & WPT_FLAG_LADDER) {
                r = 0.2f; g = 0.85f; b = 0.95f; // Cyan (Ladder)
            } else if (node.flags & WPT_FLAG_RESCUE) {
                r = 0.2f; g = 0.9f; b = 0.5f;
            }
        }

        if (isSelected) {
            r = 1.0f; g = 0.95f; b = 0.1f; a = 1.0f; // Highlight Yellow
        } else if (isHovered) {
            r = std::min(1.0f, r + 0.3f);
            g = std::min(1.0f, g + 0.3f);
            b = std::min(1.0f, b + 0.3f);
        }

        // 1. Draw 3D Cross at Waypoint Center (+12 units Z elevation for floor visibility)
        Vector3 pos = node.origin + Vector3(0.0f, 0.0f, 12.0f);
        float sz = isSelected ? 12.0f : 7.0f;

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

        // Ground peg line down to exact floor origin
        uint32_t pegBase = static_cast<uint32_t>(vertices.size());
        vertices.push_back({ node.origin.x, node.origin.y, node.origin.z, 0,0,1, 0,0, r, g, b, 0.5f });
        indices.push_back(cBase + 4);
        indices.push_back(pegBase);

        // 2. Ground Projection Circle (Tolerance radius)
        float rad = (m_showRadii || isSelected) ? std::max(12.0f, node.radius) : 12.0f;
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

        // 3. Camp / Sniper Aim Direction Vector
        if (node.flags & (WPT_FLAG_CAMP | WPT_FLAG_SNIPER | WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH)) {
            float yawRad = node.campYaw * static_cast<float>(M_PI) / 180.0f;
            float pitchRad = node.campPitch * static_cast<float>(M_PI) / 180.0f;

            Vector3 aimDir(
                std::cos(pitchRad) * std::cos(yawRad),
                std::cos(pitchRad) * std::sin(yawRad),
                std::sin(pitchRad)
            );

            Vector3 aimEnd = pos + aimDir * 28.0f;
            uint32_t aimBase = static_cast<uint32_t>(vertices.size());
            vertices.push_back({ pos.x, pos.y, pos.z, 0,0,1, 0,0, 1.0f, 1.0f, 0.2f, 1.0f });
            vertices.push_back({ aimEnd.x, aimEnd.y, aimEnd.z, 0,0,1, 0,0, 1.0f, 0.2f, 0.2f, 1.0f });
            indices.push_back(aimBase + 0);
            indices.push_back(aimBase + 1);
        }

        // 4. Outgoing Path Connection Links
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
                Vector3 endPt = target->origin + Vector3(0.0f, 0.0f, 10.0f);

                float lr = 0.2f, lg = 0.85f, lb = 1.0f, la = 0.85f; // Default Cyan
                if (node.connectionFlags[c] & WPT_CONN_JUMP) {
                    lr = 1.0f; lg = 0.8f; lb = 0.1f; // Yellow (Jump Link)
                } else if (node.flags & WPT_FLAG_ZOMBIEONLY) {
                    lr = 1.0f; lg = 0.2f; lb = 0.1f; // Red (Zombie Link)
                } else if (node.flags & WPT_FLAG_HUMANONLY) {
                    lr = 0.1f; lg = 1.0f; lb = 0.9f; // Cyan (Human Link)
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
