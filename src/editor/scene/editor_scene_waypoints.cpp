#include "editor/scene/editor_scene.h"
#include "editor/scene/scene_picker.h"
#include "editor/camera/camera.h"
#include "waypoint/waypoint_graph.h"
#include "waypoint/ebot_generator.h"
#include "waypoint/waypoint_nav_converter.h"
#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>

// --- Bot Waypoint System Implementation ---

void EditorScene::SetShowWaypoints(bool show) {
    m_showWaypoints = show;
    m_waypointRenderer.SetShowWaypoints(show);
}

void EditorScene::ToggleShowWaypoints() {
    SetShowWaypoints(!m_showWaypoints);
    ShowToast(m_showWaypoints ? "Waypoints: Visible" : "Waypoints: Hidden");
}

void EditorScene::SetShowWaypointRadii(bool show) {
    m_showWaypointRadii = show;
    m_waypointRenderer.SetShowRadii(show);
    RebuildWaypointRenderer();
}

void EditorScene::ToggleShowWaypointRadii() {
    SetShowWaypointRadii(!m_showWaypointRadii);
    ShowToast(m_showWaypointRadii ? "Waypoint Radii: Visible" : "Waypoint Radii: Hidden");
}

void EditorScene::SetShowWaypointDirection(bool show) {
    m_showWaypointDirection = show;
    m_waypointRenderer.SetShowDirection(show);
    RebuildWaypointRenderer();
}

void EditorScene::ToggleShowWaypointDirection() {
    SetShowWaypointDirection(!m_showWaypointDirection);
    ShowToast(m_showWaypointDirection ? "Waypoint Direction & Frustums: Visible" : "Waypoint Direction: Hidden");
}

void EditorScene::SetShowWaypointConnections(bool show) {
    m_showWaypointConnections = show;
    m_waypointRenderer.SetShowConnections(show);
    RebuildWaypointRenderer();
}

void EditorScene::ToggleShowWaypointConnections() {
    SetShowWaypointConnections(!m_showWaypointConnections);
    ShowToast(m_showWaypointConnections ? "Waypoint Connections: Visible" : "Waypoint Connections: Hidden");
}

void EditorScene::SetShowParkourJumpArcs(bool show) {
    m_showParkourJumpArcs = show;
    m_waypointRenderer.SetShowParkourArcs(show);
    RebuildWaypointRenderer();
}

void EditorScene::ToggleShowParkourJumpArcs() {
    SetShowParkourJumpArcs(!m_showParkourJumpArcs);
    ShowToast(m_showParkourJumpArcs ? "Parkour Jump Arcs: Visible" : "Parkour Jump Arcs: Hidden");
}

void EditorScene::SelectWaypoint(uint32_t id) {
    m_selectedWaypointId = id;
    RebuildWaypointRenderer();
}

WaypointNode* EditorScene::GetSelectedWaypoint() {
    return (m_selectedWaypointId != 0) ? m_waypoints.GetNodeByID(m_selectedWaypointId) : nullptr;
}

const WaypointNode* EditorScene::GetSelectedWaypoint() const {
    return (m_selectedWaypointId != 0) ? m_waypoints.GetNodeByID(m_selectedWaypointId) : nullptr;
}

bool EditorScene::LoadWaypoints(const std::string& path) {
    if (m_waypoints.Load(path)) {
        m_waypointPath = path;
        m_showWaypoints = true;
        m_waypointRenderer.SetShowWaypoints(true);
        RebuildWaypointRenderer();
        AddRecentFile(path);
        ShowToast("Loaded " + std::to_string(m_waypoints.GetNodeCount()) + " waypoints from " + path);
        return true;
    }
    ShowToast("Failed to load waypoints from " + path);
    return false;
}

bool EditorScene::SaveWaypoints(const std::string& path, BotType bot, GameMod mod) {
    if (m_waypoints.Save(path, bot, mod)) {
        m_waypointPath = path;
        AddRecentFile(path);
        ShowToast("Saved " + std::to_string(m_waypoints.GetNodeCount()) + " waypoints to " + path);
        return true;
    }
    ShowToast("Failed to save waypoints to " + path);
    return false;
}

void EditorScene::UnloadWaypoints() {
    m_waypoints.Clear();
    m_waypointPath.clear();
    m_selectedWaypointId = 0;
    m_cachedWaypointId = 0;
    m_waypointRenderer.Clear();
    ShowToast("Unloaded bot waypoints");
}

bool EditorScene::ConvertNavToWaypoints(BotType bot, GameMod mod) {
    if (!m_nav || !m_nav->IsLoaded()) {
        ShowToast("No NavMesh loaded to convert!");
        return false;
    }
    auto stats = WaypointNavConverter::NavToWaypoints(*m_nav, m_waypoints, bot, mod);
    m_showWaypoints = true;
    m_waypointRenderer.SetShowWaypoints(true);
    RebuildWaypointRenderer();
    ShowToast("Converted NavMesh: " + std::to_string(stats.waypointsCreated) + " waypoints, " +
              std::to_string(stats.connectionsCreated) + " links!");
    return true;
}

size_t EditorScene::ConvertWaypointsToNav() {
    if (m_waypoints.IsEmpty()) {
        ShowToast("No waypoints loaded to convert!");
        return 0;
    }
    if (!m_nav) {
        m_nav = std::make_unique<NavMesh>();
    }
    size_t created = WaypointNavConverter::WaypointsToNav(m_waypoints, *m_nav);
    if (created > 0) {
        m_isModified = true;
        m_showNAV = true;
        RebuildNavRenderer();
        ShowToast("Generated " + std::to_string(created) + " NavAreas from waypoints!");
    }
    return created;
}

void EditorScene::RebuildWaypointRenderer() {
    if (m_waypoints.IsEmpty()) {
        m_waypointRenderer.Clear();
    } else {
        m_waypointRenderer.SetShowRadii(m_showWaypointRadii);
        m_waypointRenderer.SetShowDirection(m_showWaypointDirection);
        m_waypointRenderer.SetShowConnections(m_showWaypointConnections);
        m_waypointRenderer.SetShowParkourArcs(m_showParkourJumpArcs);
        m_waypointRenderer.BuildFromGraph(m_waypoints, m_selectedWaypointId);
    }
}

WaypointGraph::WaypointAnalysisStats EditorScene::AutoAnalyzeWaypoints() {
    auto stats = m_waypoints.AnalyzeGraph(m_bsp.get(), m_waypoints.GetActiveMod());
    RebuildWaypointRenderer();
    ShowToast("Waypoint Analysis: " + std::to_string(stats.totalModified) + " nodes updated (" +
              std::to_string(stats.crouchAssigned) + " crouch, " +
              std::to_string(stats.jumpAssigned) + " jump, " +
              std::to_string(stats.campAnglesCalculated) + " camp sightlines, " +
              std::to_string(stats.blockedLinksPruned) + " blocked links pruned)!");
    return stats;
}

bool EditorScene::StartEBotWaypointGeneration(const EBotGenerateOptions& options) {
    if (!m_bsp || !m_bsp->IsLoaded()) {
        ShowToast("Cannot generate waypoints: No BSP map loaded!");
        return false;
    }
    if (m_waypointGenProgress.isGenerating.load()) {
        ShowToast("Waypoint generation is already in progress!");
        return false;
    }

    if (m_waypointGenThread.joinable()) {
        m_waypointGenThread.join();
    }

    m_waypointGenProgress.isGenerating.store(true);
    m_waypointGenProgress.progress.store(0.0f);
    m_waypointGenProgress.statusMessage = "Starting automated waypoint generation...";
    m_waypointGenProgress.completed = false;
    m_waypointGenProgress.success = false;
    m_waypointGenProgress.errorMessage.clear();

    m_pendingGeneratedGraph.Clear();

    m_waypointGenThread = std::thread([this, options]() {
        auto progressCb = [this](float p, const std::string& msg) {
            m_waypointGenProgress.progress.store(p);
            std::lock_guard<std::mutex> lock(m_waypointGenMutex);
            m_waypointGenProgress.statusMessage = msg;
        };

        EBotGenerateResult res = EBotGenerator::Generate(*m_bsp, m_pendingGeneratedGraph, options, progressCb);

        {
            std::lock_guard<std::mutex> lock(m_waypointGenMutex);
            m_pendingGenResult = res;
            m_waypointGenProgress.success = res.success;
            m_waypointGenProgress.waypointsCreated = res.waypointsCreated;
            m_waypointGenProgress.connectionsCreated = res.connectionsCreated;
            m_waypointGenProgress.laddersCreated = res.laddersCreated;
            m_waypointGenProgress.campPointsCreated = res.campPointsCreated;
            m_waypointGenProgress.sniperPointsCreated = res.sniperPointsCreated;
            m_waypointGenProgress.zombieCampsCreated = res.zombieCampsCreated;
            m_waypointGenProgress.durationSeconds = res.durationSeconds;
            m_waypointGenProgress.errorMessage = res.errorMessage;
            m_waypointGenProgress.progress.store(1.0f);
            m_waypointGenProgress.completed = true;
            m_waypointGenProgress.isGenerating.store(false);
        }
    });

    return true;
}

void EditorScene::UpdateWaypointGeneration() {
    if (m_waypointGenProgress.completed) {
        m_waypointGenProgress.completed = false;
        if (m_waypointGenThread.joinable()) {
            m_waypointGenThread.join();
        }

        if (m_waypointGenProgress.success) {
            {
                std::lock_guard<std::mutex> lock(m_waypointGenMutex);
                m_waypoints = std::move(m_pendingGeneratedGraph);
            }
            m_showWaypoints = true;
            m_waypointRenderer.SetShowWaypoints(true);
            RebuildWaypointRenderer();
            ShowToast("Generated " + std::to_string(m_waypoints.GetNodeCount()) + " waypoints in " +
                      std::to_string(m_pendingGenResult.durationSeconds).substr(0, 4) + "s!");
        } else {
            ShowToast("Waypoint generation failed: " + m_waypointGenProgress.errorMessage);
        }
    }
}

// --- Interactive Bot Waypoint Editing Tools ---

size_t EditorScene::FloodFillWaypointsAt(const Ray& ray) {
    if (!HasBSP()) return 0;

    Vector3 seedFloor;
    if (!ScenePicker::PickBSPFloor(*this, ray, &seedFloor)) {
        return 0;
    }

    float spacing = (m_gridSize >= 80.0f && m_gridSize <= 256.0f) ? m_gridSize : 135.0f;
    float snapDist = spacing;

    // Snapped seed coordinate
    float startX = std::round(seedFloor.x / snapDist) * snapDist;
    float startY = std::round(seedFloor.y / snapDist) * snapDist;

    struct GridCoord {
        int gx, gy;
        float floorZ;
        float srcX, srcY;
        bool operator==(const GridCoord& o) const { return gx == o.gx && gy == o.gy; }
    };
    struct GridHash {
        size_t operator()(const GridCoord& c) const {
            return std::hash<int>()(c.gx) ^ (std::hash<int>()(c.gy) << 16);
        }
    };

    std::unordered_set<GridCoord, GridHash> visited;
    std::queue<GridCoord> queue;

    GridCoord startCoord{
        static_cast<int>(std::round(startX / snapDist)),
        static_cast<int>(std::round(startY / snapDist)),
        seedFloor.z,
        seedFloor.x,
        seedFloor.y
    };
    visited.insert(startCoord);
    queue.push(startCoord);

    std::vector<uint32_t> newWaypointIds;
    const int maxPoints = 800; // room limit per flood click

    while (!queue.empty() && newWaypointIds.size() < maxPoints) {
        GridCoord cur = queue.front();
        queue.pop();

        float wx = (newWaypointIds.empty()) ? seedFloor.x : (cur.gx * snapDist);
        float wy = (newWaypointIds.empty()) ? seedFloor.y : (cur.gy * snapDist);

        // Trace down to floor relative to current cell's elevation
        Vector3 traceTop(wx, wy, cur.floorZ + 45.0f);
        Vector3 traceBottom(wx, wy, cur.floorZ - 150.0f);
        BSPTraceResult floorTr;
        if (!m_bsp->TraceWorld(traceTop, traceBottom, HULL_POINT, &floorTr) || floorTr.fraction >= 1.0f || floorTr.startsolid || floorTr.allsolid) {
            continue;
        }

        // Walkable surface slope check
        if (floorTr.planeNormal.z < 0.7071f) {
            continue;
        }

        Vector3 groundPos = floorTr.endpos;

        // Check vertical headroom clearance upward from floor using HULL_POINT
        BSPTraceResult headTr;
        m_bsp->TraceWorld(groundPos + Vector3(0.0f, 0.0f, 2.0f), groundPos + Vector3(0.0f, 0.0f, 74.0f), HULL_POINT, &headTr);
        float clearance = (headTr.fraction < 1.0f && !headTr.startsolid && !headTr.allsolid)
            ? (headTr.endpos.z - groundPos.z)
            : 74.0f;

        if (clearance < 36.0f) {
            continue; // Cannot fit even crouching
        }
        bool canStand = (clearance >= 68.0f);
        bool canCrouch = (clearance >= 36.0f);

        // Traversal line of sight from previous coordinate (if not first seed node)
        if (!newWaypointIds.empty()) {
            float stepZ = std::max(cur.floorZ, groundPos.z);
            BSPTraceResult stepTr;
            m_bsp->TraceWorld(Vector3(cur.srcX, cur.srcY, stepZ + 18.0f),
                              Vector3(wx, wy, stepZ + 18.0f),
                              HULL_POINT, &stepTr);
            if (stepTr.fraction < 0.95f || stepTr.startsolid || stepTr.allsolid) {
                continue; // Blocked by wall, column, or door frame
            }
        }

        Vector3 candOrigin = groundPos + Vector3(0.0f, 0.0f, 18.0f);

        // Radius-aware spatial suppression: if candidate location is already covered by a nearby node's wayzone with clear LOS, skip
        bool coveredByExisting = false;
        for (uint32_t exId : newWaypointIds) {
            const WaypointNode* ex = m_waypoints.GetNodeByID(exId);
            if (!ex) continue;
            float d = (ex->origin - candOrigin).Length();
            float checkDist = std::max(snapDist * 0.75f, ex->radius * 1.10f);
            if (d < checkDist && std::abs(ex->origin.z - candOrigin.z) <= 36.0f) {
                BSPTraceResult losTr;
                m_bsp->TraceWorld(candOrigin, ex->origin, HULL_POINT, &losTr);
                if (losTr.fraction >= 0.95f && !losTr.startsolid) {
                    coveredByExisting = true;
                    break;
                }
            }
        }
        if (coveredByExisting) {
            continue;
        }

        uint32_t flags = m_activeWaypointAddFlags;
        if (!canStand && canCrouch) {
            flags |= WPT_FLAG_CROUCH;
        }

        WaypointNode* node = m_waypoints.AddNode(candOrigin, flags, 32.0f);
        if (node) {
            newWaypointIds.push_back(node->id);

            // Connect to neighboring existing waypoints within link distance
            for (uint32_t otherId : newWaypointIds) {
                if (otherId == node->id) continue;
                WaypointNode* other = m_waypoints.GetNodeByID(otherId);
                if (!other) continue;

                Vector3 diff = other->origin - node->origin;
                float dsq = diff.Dot(diff);
                if (dsq <= (snapDist * 1.65f) * (snapDist * 1.65f) && std::abs(diff.z) <= 45.0f) {
                    // Check line of sight
                    BSPTraceResult losTr;
                    m_bsp->TraceWorld(node->origin + Vector3(0.0f, 0.0f, 10.0f),
                                      other->origin + Vector3(0.0f, 0.0f, 10.0f),
                                      HULL_POINT, &losTr);
                    if (losTr.fraction >= 0.95f && !losTr.startsolid && !losTr.allsolid) {
                        uint16_t cflags = WPT_CONN_NONE;
                        if (std::abs(diff.z) > 18.0f) cflags |= WPT_CONN_JUMP;
                        m_waypoints.ConnectNodes(node->id, other->id, true, cflags);
                    }
                }
            }

            // Calculate optimal wayzone radius immediately based on radial world geometry
            m_waypoints.CalculateWayzone(node->id, m_bsp.get());
        }

        // Expand in 4 cardinal directions
        const int dx[4] = { 1, -1, 0, 0 };
        const int dy[4] = { 0, 0, 1, -1 };
        for (int d = 0; d < 4; ++d) {
            GridCoord nextCoord{ cur.gx + dx[d], cur.gy + dy[d], groundPos.z, wx, wy };
            if (visited.find(nextCoord) == visited.end()) {
                visited.insert(nextCoord);
                float nwx = nextCoord.gx * snapDist;
                float nwy = nextCoord.gy * snapDist;
                float distFromSeed = std::hypot(nwx - seedFloor.x, nwy - seedFloor.y);
                if (distFromSeed <= 2048.0f) {
                    queue.push(nextCoord);
                }
            }
        }
    }

    if (!newWaypointIds.empty()) {
        // Run Parkour detection on newly flooded waypoints
        m_waypoints.GenerateParkour(m_bsp.get());

        // Optimize graph to prune redundant straight corridor nodes and merge overlaps
        WaypointGraph::WaypointOptimizeOptions opt;
        opt.mergeOverlapping = true;
        opt.mergeDistance = 35.0f;
        opt.pruneCollinear = true;
        opt.collinearMaxAngle = 16.0f;
        opt.recalculateWayzones = true;
        m_waypoints.OptimizeGraph(m_bsp.get(), opt);

        m_showWaypoints = true;
        m_waypointRenderer.SetShowWaypoints(true);
        RebuildWaypointRenderer();
        m_isModified = true;
        ShowToast("Waypoint Flood-Fill created " + std::to_string(newWaypointIds.size()) + " waypoints with wide wayzones!");
    }

    return newWaypointIds.size();
}

WaypointGraph::WaypointParkourStats EditorScene::GenerateParkour(const WaypointGraph::WaypointParkourOptions& options) {
    if (m_waypoints.IsEmpty()) {
        ShowToast("No waypoints loaded to generate parkour!");
        return WaypointGraph::WaypointParkourStats();
    }
    auto stats = m_waypoints.GenerateParkour(m_bsp.get(), options);
    RebuildWaypointRenderer();
    m_isModified = true;
    ShowToast("Parkour: Created " + std::to_string(stats.totalParkourLinks) + " jump links (" +
              std::to_string(stats.jumpUpsCreated) + " crate climbs, " +
              std::to_string(stats.gapJumpsCreated) + " chasm leaps, " +
              std::to_string(stats.dropJumpsCreated) + " drops)!");
    return stats;
}

uint32_t EditorScene::OnAddWaypointClick(const Ray& ray) {
    if (!HasBSP()) return 0;
    Vector3 floorPos;
    if (!ScenePicker::PickBSPFloor(*this, ray, &floorPos)) {
        return 0;
    }
    return AddWaypointAt(floorPos + Vector3(0.0f, 0.0f, 18.0f));
}

uint32_t EditorScene::AddWaypointAt(const Vector3& pos) {
    uint32_t prevId = m_selectedWaypointId;
    WaypointNode* node = m_waypoints.AddNode(pos, m_activeWaypointAddFlags, m_activeWaypointAddRadius);
    if (!node) return 0;

    // If auto-connect is enabled, connect to previously selected or closest visible waypoint within 300u
    if (m_autoConnectWaypoints) {
        if (prevId != 0 && prevId != node->id) {
            WaypointNode* prev = m_waypoints.GetNodeByID(prevId);
            if (prev) {
                float dist = (prev->origin - node->origin).Length();
                if (dist <= 300.0f) {
                    m_waypoints.ConnectNodes(node->id, prevId, true, WPT_CONN_NONE);
                }
            }
        } else {
            // Find closest visible node
            int nearestIdx = m_waypoints.FindNearestNode(node->origin, 250.0f);
            if (nearestIdx >= 0) {
                const auto& nodes = m_waypoints.GetNodes();
                if (static_cast<size_t>(nearestIdx) < nodes.size() && nodes[nearestIdx].id != node->id) {
                    m_waypoints.ConnectNodes(node->id, nodes[nearestIdx].id, true, WPT_CONN_NONE);
                }
            }
        }
    }

    if (HasBSP()) {
        m_waypoints.CalculateWayzone(node->id, m_bsp.get());
    }

    SelectWaypoint(node->id);
    m_showWaypoints = true;
    m_waypointRenderer.SetShowWaypoints(true);
    RebuildWaypointRenderer();
    m_isModified = true;
    ShowToast("Added Waypoint #" + std::to_string(node->id));
    return node->id;
}

void EditorScene::SnapSelectedWaypointToFloor() {
    if (m_selectedWaypointId == 0 || !HasBSP()) return;
    WaypointNode* node = m_waypoints.GetNodeByID(m_selectedWaypointId);
    if (!node) return;

    Vector3 start(node->origin.x, node->origin.y, node->origin.z + 32.0f);
    Vector3 end(node->origin.x, node->origin.y, node->origin.z - 2048.0f);
    BSPTraceResult tr;
    if (m_bsp->TraceWorld(start, end, HULL_POINT, &tr) && !tr.startsolid && !tr.allsolid) {
        node->origin.z = tr.endpos.z + 18.0f;
        m_waypoints.CalculateWayzone(node->id, m_bsp.get());
        RebuildWaypointRenderer();
        m_isModified = true;
        ShowToast("Snapped Waypoint #" + std::to_string(node->id) + " to floor.");
    }
}

bool EditorScene::ConnectSelectedWaypointTo(uint32_t targetId, uint16_t connFlags, bool bidirectional) {
    if (m_selectedWaypointId == 0 || targetId == 0 || m_selectedWaypointId == targetId) return false;
    bool ok = m_waypoints.ConnectNodes(m_selectedWaypointId, targetId, bidirectional, connFlags);
    if (ok) {
        RebuildWaypointRenderer();
        m_isModified = true;
        ShowToast("Connected Waypoint #" + std::to_string(m_selectedWaypointId) + " to #" + std::to_string(targetId));
    }
    return ok;
}

void EditorScene::DeleteSelectedWaypoint() {
    if (m_selectedWaypointId == 0) return;
    uint32_t id = m_selectedWaypointId;
    SelectWaypoint(0);
    m_waypoints.DeleteNode(id);
    RebuildWaypointRenderer();
    m_isModified = true;
    ShowToast("Deleted Waypoint #" + std::to_string(id));
}

void EditorScene::CacheWaypoint(uint32_t id) {
    if (id == 0) id = m_selectedWaypointId;
    m_cachedWaypointId = id;
    if (id != 0) {
        ShowToast("Cached Waypoint #" + std::to_string(id));
    }
}

bool EditorScene::CreateConnectionToCached(int conType) {
    if (m_cachedWaypointId == 0 || m_selectedWaypointId == 0 || m_cachedWaypointId == m_selectedWaypointId) {
        ShowToast("Select a waypoint to connect with cached #" + std::to_string(m_cachedWaypointId));
        return false;
    }

    bool ok = false;
    switch (conType) {
        case 0: // Outgoing (Cached -> Selected)
            ok = m_waypoints.ConnectNodes(m_cachedWaypointId, m_selectedWaypointId, false, WPT_CONN_NONE);
            break;
        case 1: // Incoming (Selected -> Cached)
            ok = m_waypoints.ConnectNodes(m_selectedWaypointId, m_cachedWaypointId, false, WPT_CONN_NONE);
            break;
        case 2: // Bothways
            ok = m_waypoints.ConnectNodes(m_cachedWaypointId, m_selectedWaypointId, true, WPT_CONN_NONE);
            break;
        case 3: // Jumping
            ok = m_waypoints.ConnectNodes(m_cachedWaypointId, m_selectedWaypointId, true, WPT_CONN_JUMP);
            break;
        case 4: // Crouch
            ok = m_waypoints.ConnectNodes(m_cachedWaypointId, m_selectedWaypointId, true, WPT_CONN_CROUCH);
            break;
        case 5: // Boosting / Double jump
            ok = m_waypoints.ConnectNodes(m_cachedWaypointId, m_selectedWaypointId, true, WPT_CONN_DOUBLE);
            break;
    }

    if (ok) {
        RebuildWaypointRenderer();
        m_isModified = true;
        ShowToast("Connected cached #" + std::to_string(m_cachedWaypointId) + " to #" + std::to_string(m_selectedWaypointId));
    }
    return ok;
}

void EditorScene::DeleteConnectionToCached() {
    if (m_cachedWaypointId != 0 && m_selectedWaypointId != 0) {
        m_waypoints.DisconnectNodes(m_cachedWaypointId, m_selectedWaypointId, true);
        RebuildWaypointRenderer();
        m_isModified = true;
        ShowToast("Deleted link between #" + std::to_string(m_cachedWaypointId) + " and #" + std::to_string(m_selectedWaypointId));
    }
}

void EditorScene::TeleportCameraToWaypoint(uint32_t id, Camera& camera) {
    WaypointNode* node = m_waypoints.GetNodeByID(id);
    if (node) {
        camera.SetPosition(node->origin + Vector3(0.0f, 0.0f, 40.0f));
        ShowToast("Teleported camera to Waypoint #" + std::to_string(id));
    }
}

WaypointGraph::WaypointOptimizeStats EditorScene::OptimizeWaypoints(const WaypointGraph::WaypointOptimizeOptions& options) {
    if (m_waypoints.IsEmpty()) {
        ShowToast("Cannot optimize: No waypoints in graph!");
        return WaypointGraph::WaypointOptimizeStats();
    }

    auto stats = m_waypoints.OptimizeGraph(HasBSP() ? &GetBSP() : nullptr, options);
    RebuildWaypointRenderer();
    m_isModified = true;

    std::string msg = "Optimized graph: " + std::to_string(stats.totalModified) + " adjustments made (" +
                      std::to_string(stats.overlappingMerged) + " merged, " +
                      std::to_string(stats.collinearPruned) + " collinear pruned, " +
                      std::to_string(stats.blockedLinksPruned) + " blocked pruned).";
    ShowToast(msg);
    return stats;
}

void EditorScene::ChooseLoadNavOnly() {
    m_showNavOrWptPrompt = false;
    SetTargetMode(TARGET_NAVMESH);
    SetShowWaypoints(false);
    ShowToast("Active Mode: Valve Navigation Mesh (.nav)");
}

void EditorScene::ChooseLoadWptOnly() {
    m_showNavOrWptPrompt = false;
    SetTargetMode(TARGET_WAYPOINTS);
    SetShowWaypoints(true);
    ShowToast("Active Mode: Bot Waypoints Graph");
}

void EditorScene::ChooseLoadBoth() {
    m_showNavOrWptPrompt = false;
    SetShowWaypoints(true);
    ShowToast("Dual Navigation Layers Loaded (NavMesh + Waypoints)");
}

void EditorScene::DismissNavOrWptChoice() {
    m_showNavOrWptPrompt = false;
}
