#ifndef WAYPOINT_RENDERER_H
#define WAYPOINT_RENDERER_H

#include <vector>
#include <unordered_set>
#include <cstdint>
#include "editor/glad/include/glad/glad.h"
#include "waypoint/waypoint_graph.h"
#include "editor/render/shader.h"
#include "editor/math/matrix4.h"

struct WaypointVertex {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
    float r, g, b, a;
};

class WaypointRenderer {
public:
    WaypointRenderer();
    ~WaypointRenderer();

    bool BuildFromGraph(
        const WaypointGraph& graph,
        uint32_t selectedId = 0,
        uint32_t hoveredId = 0,
        const std::unordered_set<uint32_t>* selectedSet = nullptr,
        const Vector3* penPreviewStart = nullptr,
        const Vector3* penPreviewEnd = nullptr,
        bool penPreviewClear = true,
        const std::vector<uint32_t>* ghostBotPath = nullptr,
        const Vector3* ghostBotPos = nullptr,
        float ghostBotYaw = 0.0f,
        uint32_t selectedConnFrom = 0,
        uint32_t selectedConnTo = 0
    );
    void Clear();

    void Render(const Shader& lineShader, const Matrix4& mvp);

    bool IsLoaded() const { return m_loaded; }
    size_t GetNodeCount() const { return m_nodeCount; }

    bool GetShowWaypoints() const { return m_showWaypoints; }
    void SetShowWaypoints(bool show) { m_showWaypoints = show; }

    bool GetShowConnections() const { return m_showConnections; }
    void SetShowConnections(bool show) { m_showConnections = show; }

    bool GetShowRadii() const { return m_showRadii; }
    void SetShowRadii(bool show) { m_showRadii = show; }

    bool GetShowDirection() const { return m_showDirection; }
    void SetShowDirection(bool show) { m_showDirection = show; }

    bool GetShowParkourArcs() const { return m_showParkourArcs; }
    void SetShowParkourArcs(bool show) { m_showParkourArcs = show; }

private:
    GLuint m_lineVAO{0};
    GLuint m_lineVBO{0};
    GLuint m_lineEBO{0};

    size_t m_lineIndexCount{0};
    size_t m_nodeCount{0};
    bool m_loaded{false};

    bool m_showWaypoints{true};
    bool m_showConnections{true};
    bool m_showRadii{true};
    bool m_showDirection{true};
    bool m_showParkourArcs{true};
};

#endif // WAYPOINT_RENDERER_H
