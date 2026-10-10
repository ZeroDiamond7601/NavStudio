#include "editor/scene/editor_scene.h"
#include "editor/scene/scene_picker.h"
#include "editor/camera/camera.h"
#include "editor/commands/waypoint_commands.h"
#include "editor/commands/nav_commands.h"
#include "waypoint/waypoint_graph.h"
#include "waypoint/ebot_generator.h"
#include "waypoint/waypoint_nav_converter.h"
#include <queue>
#include <unordered_set>
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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

void EditorScene::SelectWaypoint(uint32_t id, bool addToSelection) {
    if (!addToSelection) {
        m_selectedWaypointIds.clear();
        m_selectedWaypointId = id;
        if (id != 0) {
            m_selectedWaypointIds.insert(id);
            m_selectedAreaId = 0;
            m_selectedAreaIds.clear();
            m_selectedLadderId = 0;
            m_selectedWaypointConnection.clear();
        }
    } else {
        if (id != 0) {
            if (m_selectedWaypointIds.find(id) != m_selectedWaypointIds.end()) {
                m_selectedWaypointIds.erase(id);
                if (m_selectedWaypointId == id) {
                    m_selectedWaypointId = m_selectedWaypointIds.empty() ? 0 : *m_selectedWaypointIds.begin();
                }
            } else {
                m_selectedWaypointIds.insert(id);
                m_selectedWaypointId = id;
                m_selectedAreaId = 0;
                m_selectedAreaIds.clear();
                m_selectedLadderId = 0;
                m_selectedWaypointConnection.clear();
            }
        }
    }
    RebuildWaypointRenderer();
}

void EditorScene::DeselectWaypoint(uint32_t id) {
    m_selectedWaypointIds.erase(id);
    if (m_selectedWaypointId == id) {
        m_selectedWaypointId = m_selectedWaypointIds.empty() ? 0 : *m_selectedWaypointIds.begin();
    }
    RebuildWaypointRenderer();
}

void EditorScene::SelectAllWaypoints() {
    m_selectedWaypointIds.clear();
    for (const auto& node : m_waypoints.GetNodes()) {
        m_selectedWaypointIds.insert(node.id);
    }
    if (!m_waypoints.IsEmpty()) {
        m_selectedWaypointId = m_waypoints.GetNodes().front().id;
    }
    m_selectedAreaId = 0;
    m_selectedAreaIds.clear();
    m_selectedLadderId = 0;
    m_selectedWaypointConnection.clear();
    RebuildWaypointRenderer();
    ShowToast("Selected all " + std::to_string(m_selectedWaypointIds.size()) + " waypoints");
}

void EditorScene::ClearWaypointSelection() {
    m_selectedWaypointId = 0;
    m_selectedWaypointIds.clear();
    RebuildWaypointRenderer();
}

void EditorScene::InvertWaypointSelection() {
    std::unordered_set<uint32_t> inverted;
    for (const auto& node : m_waypoints.GetNodes()) {
        if (m_selectedWaypointIds.find(node.id) == m_selectedWaypointIds.end()) {
            inverted.insert(node.id);
        }
    }
    m_selectedWaypointIds = std::move(inverted);
    m_selectedWaypointId = m_selectedWaypointIds.empty() ? 0 : *m_selectedWaypointIds.begin();
    RebuildWaypointRenderer();
    ShowToast("Inverted selection (" + std::to_string(m_selectedWaypointIds.size()) + " selected)");
}

void EditorScene::BoxSelectWaypoints(const std::vector<uint32_t>& pickedIds, bool additive, bool subtractive) {
    if (!additive && !subtractive) {
        m_selectedWaypointIds.clear();
    }
    for (uint32_t id : pickedIds) {
        if (subtractive) {
            m_selectedWaypointIds.erase(id);
        } else {
            m_selectedWaypointIds.insert(id);
        }
    }
    if (m_selectedWaypointIds.find(m_selectedWaypointId) == m_selectedWaypointIds.end()) {
        m_selectedWaypointId = m_selectedWaypointIds.empty() ? 0 : *m_selectedWaypointIds.begin();
    }
    if (!m_selectedWaypointIds.empty()) {
        m_selectedAreaId = 0;
        m_selectedAreaIds.clear();
        m_selectedLadderId = 0;
        m_selectedWaypointConnection.clear();
    }
    RebuildWaypointRenderer();
    ShowToast("Box selected " + std::to_string(m_selectedWaypointIds.size()) + " waypoints");
}

void EditorScene::BatchSetWaypointFlags(uint32_t flag, bool setOrToggle) {
    if (m_selectedWaypointIds.empty()) return;
    for (uint32_t id : m_selectedWaypointIds) {
        WaypointNode* node = m_waypoints.GetNodeByID(id);
        if (node) {
            if (setOrToggle) node->flags |= flag;
            else node->flags &= ~flag;
        }
    }
    m_isModified = true;
    RebuildWaypointRenderer();
    ShowToast("Updated flags for " + std::to_string(m_selectedWaypointIds.size()) + " waypoints");
}

void EditorScene::BatchSetWaypointRadius(float radius) {
    if (m_selectedWaypointIds.empty()) return;
    for (uint32_t id : m_selectedWaypointIds) {
        WaypointNode* node = m_waypoints.GetNodeByID(id);
        if (node) node->radius = radius;
    }
    m_isModified = true;
    RebuildWaypointRenderer();
    ShowToast("Set radius = " + std::to_string((int)radius) + " for " + std::to_string(m_selectedWaypointIds.size()) + " waypoints");
}

void EditorScene::BatchDeleteWaypoints() {
    if (m_selectedWaypointIds.empty()) {
        DeleteSelectedWaypoint();
        return;
    }
    std::vector<uint32_t> ids(m_selectedWaypointIds.begin(), m_selectedWaypointIds.end());
    size_t count = ids.size();
    if (m_cmdMgr) {
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdBatchDeleteWaypoints>(this, ids));
    } else {
        for (uint32_t id : ids) {
            m_waypoints.RemoveNode(id);
        }
        m_selectedWaypointIds.clear();
        m_selectedWaypointId = 0;
        m_isModified = true;
        RebuildWaypointRenderer();
    }
    ShowToast("Deleted " + std::to_string(count) + " waypoints");
}

void EditorScene::BatchConnectSelectedWaypoints(bool bidirectional) {
    if (m_selectedWaypointIds.size() < 2) return;
    std::vector<uint32_t> ids(m_selectedWaypointIds.begin(), m_selectedWaypointIds.end());
    size_t connected = 0;
    for (size_t i = 0; i < ids.size(); ++i) {
        for (size_t j = i + 1; j < ids.size(); ++j) {
            if (m_cmdMgr) {
                m_cmdMgr->ExecuteCommand(std::make_unique<CmdConnectWaypoints>(this, ids[i], ids[j], bidirectional, WPT_CONN_NONE));
                connected++;
            } else {
                if (m_waypoints.ConnectNodes(ids[i], ids[j], bidirectional, WPT_CONN_NONE)) {
                    connected++;
                }
            }
        }
    }
    m_isModified = true;
    RebuildWaypointRenderer();
    ShowToast("Connected " + std::to_string(connected) + " links across selected waypoints");
}

void EditorScene::BatchSnapWaypointsToFloor() {
    if (m_selectedWaypointIds.empty() || !HasBSP()) return;
    std::vector<CmdBatchMoveWaypoints::MoveEntry> entries;
    for (uint32_t id : m_selectedWaypointIds) {
        WaypointNode* node = m_waypoints.GetNodeByID(id);
        if (node) {
            Vector3 ground;
            if (m_bsp->GetGround(node->origin + Vector3(0, 0, 18.0f), &ground, 500.0f)) {
                Vector3 newPos = ground + Vector3(0, 0, 18.0f);
                entries.push_back({ id, node->origin, newPos });
            }
        }
    }
    if (entries.empty()) return;
    size_t snapped = entries.size();
    if (m_cmdMgr) {
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdBatchMoveWaypoints>(this, entries));
    } else {
        for (const auto& e : entries) {
            auto* node = m_waypoints.GetNode(e.id);
            if (node) {
                node->origin = e.newPos;
                if (m_bsp) m_waypoints.CalculateWayzone(e.id, m_bsp.get());
            }
        }
        m_isModified = true;
        RebuildWaypointRenderer();
    }
    ShowToast("Snapped " + std::to_string(snapped) + " waypoints to floor");
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
    if (m_waypoints.IsEmpty() && !m_penToolActive && !m_ghostBotActive) {
        m_waypointRenderer.Clear();
    } else {
        m_waypointRenderer.SetShowRadii(m_showWaypointRadii);
        m_waypointRenderer.SetShowDirection(m_showWaypointDirection);
        m_waypointRenderer.SetShowConnections(m_showWaypointConnections);
        m_waypointRenderer.SetShowParkourArcs(m_showParkourJumpArcs);

        const Vector3* penStart = nullptr;
        const Vector3* penEnd = nullptr;
        if (m_penToolActive && m_penRubberbandValid && m_penLastWaypointId != 0) {
            const WaypointNode* lastWp = m_waypoints.GetNodeByID(m_penLastWaypointId);
            if (lastWp) {
                penStart = &lastWp->origin;
                penEnd = &m_penRubberbandTarget;
            }
        }

        const Vector3* botPosPtr = m_ghostBotActive ? &m_ghostBotPos : nullptr;
        const std::vector<uint32_t>* botPathPtr = m_ghostBotActive ? &m_ghostBotPath : nullptr;

        m_waypointRenderer.BuildFromGraph(
            m_waypoints,
            m_selectedWaypointId,
            0,
            &m_selectedWaypointIds,
            penStart,
            penEnd,
            m_penRubberbandClear,
            botPathPtr,
            botPosPtr,
            m_ghostBotYaw,
            m_selectedWaypointConnection.fromId,
            m_selectedWaypointConnection.toId
        );
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
            m_waypointGenProgress.parkourLinksCreated = res.parkourLinksCreated;
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

bool EditorScene::StartAsyncOptimizeWaypoints(const WaypointGraph::WaypointOptimizeOptions& options) {
    if (m_waypoints.IsEmpty()) {
        ShowToast("Cannot optimize: No waypoints in graph!");
        return false;
    }
    if (m_waypointTaskProgress.isRunning.load()) {
        ShowToast("Another waypoint task is already running!");
        return false;
    }
    if (m_waypointTaskThread.joinable()) {
        m_waypointTaskThread.join();
    }

    m_waypointTaskProgress.isRunning.store(true);
    m_waypointTaskProgress.progress.store(0.0f);
    m_waypointTaskProgress.taskName = "Optimizing Waypoint Topology & Geometry";
    m_waypointTaskProgress.statusMessage = "Starting optimizer passes...";
    m_waypointTaskProgress.completed = false;
    m_waypointTaskProgress.success = false;
    m_waypointTaskProgress.resultSummary.clear();

    m_waypointTaskThread = std::thread([this, options]() {
        auto progressCb = [this](float p, const std::string& msg) {
            m_waypointTaskProgress.progress.store(p);
            std::lock_guard<std::mutex> lock(m_waypointTaskMutex);
            m_waypointTaskProgress.statusMessage = msg;
        };

        auto stats = m_waypoints.OptimizeGraph(HasBSP() ? &GetBSP() : nullptr, options, progressCb);

        {
            std::lock_guard<std::mutex> lock(m_waypointTaskMutex);
            m_lastOptimizeStats = stats;
            m_waypointTaskProgress.durationSeconds = stats.durationSeconds;
            m_waypointTaskProgress.resultSummary = "Optimized graph: " + std::to_string(stats.totalModified) + " adjustments made (" +
                std::to_string(stats.overlappingMerged) + " merged, " +
                std::to_string(stats.collinearPruned) + " collinear pruned, " +
                std::to_string(stats.blockedLinksPruned) + " blocked pruned).";
            m_waypointTaskProgress.success = true;
            m_waypointTaskProgress.progress.store(1.0f);
            m_waypointTaskProgress.completed = true;
            m_waypointTaskProgress.isRunning.store(false);
        }
    });
    return true;
}

bool EditorScene::StartAsyncGenerateParkour(const WaypointGraph::WaypointParkourOptions& options) {
    if (m_waypoints.IsEmpty()) {
        ShowToast("Cannot generate parkour: No waypoints in graph!");
        return false;
    }
    if (m_waypointTaskProgress.isRunning.load()) {
        ShowToast("Another waypoint task is already running!");
        return false;
    }
    if (m_waypointTaskThread.joinable()) {
        m_waypointTaskThread.join();
    }

    m_waypointTaskProgress.isRunning.store(true);
    m_waypointTaskProgress.progress.store(0.0f);
    m_waypointTaskProgress.taskName = "Generating Parkour Jump Links";
    m_waypointTaskProgress.statusMessage = "Simulating jump trajectories...";
    m_waypointTaskProgress.completed = false;
    m_waypointTaskProgress.success = false;
    m_waypointTaskProgress.resultSummary.clear();

    m_waypointTaskThread = std::thread([this, options]() {
        auto progressCb = [this](float p, const std::string& msg) {
            m_waypointTaskProgress.progress.store(p);
            std::lock_guard<std::mutex> lock(m_waypointTaskMutex);
            m_waypointTaskProgress.statusMessage = msg;
        };

        auto stats = m_waypoints.GenerateParkour(HasBSP() ? &GetBSP() : nullptr, options, progressCb);

        {
            std::lock_guard<std::mutex> lock(m_waypointTaskMutex);
            m_lastParkourStats = stats;
            m_waypointTaskProgress.durationSeconds = stats.durationSeconds;
            m_waypointTaskProgress.resultSummary = "Parkour: Created " + std::to_string(stats.totalParkourLinks) + " jump links (" +
                std::to_string(stats.jumpUpsCreated) + " crate climbs, " +
                std::to_string(stats.gapJumpsCreated) + " chasm leaps, " +
                std::to_string(stats.dropJumpsCreated) + " drops)!";
            m_waypointTaskProgress.success = true;
            m_waypointTaskProgress.progress.store(1.0f);
            m_waypointTaskProgress.completed = true;
            m_waypointTaskProgress.isRunning.store(false);
        }
    });
    return true;
}

bool EditorScene::StartAsyncAutoAnalyzeWaypoints() {
    if (m_waypoints.IsEmpty()) {
        ShowToast("Cannot analyze: No waypoints in graph!");
        return false;
    }
    if (m_waypointTaskProgress.isRunning.load()) {
        ShowToast("Another waypoint task is already running!");
        return false;
    }
    if (m_waypointTaskThread.joinable()) {
        m_waypointTaskThread.join();
    }

    m_waypointTaskProgress.isRunning.store(true);
    m_waypointTaskProgress.progress.store(0.0f);
    m_waypointTaskProgress.taskName = "Automated Waypoint Analysis";
    m_waypointTaskProgress.statusMessage = "Analyzing node sightlines & clearances...";
    m_waypointTaskProgress.completed = false;
    m_waypointTaskProgress.success = false;
    m_waypointTaskProgress.resultSummary.clear();

    m_waypointTaskThread = std::thread([this]() {
        auto progressCb = [this](float p, const std::string& msg) {
            m_waypointTaskProgress.progress.store(p);
            std::lock_guard<std::mutex> lock(m_waypointTaskMutex);
            m_waypointTaskProgress.statusMessage = msg;
        };

        auto stats = m_waypoints.AnalyzeGraph(HasBSP() ? &GetBSP() : nullptr, m_waypoints.GetActiveMod(), progressCb);

        {
            std::lock_guard<std::mutex> lock(m_waypointTaskMutex);
            m_lastAnalysisStats = stats;
            m_waypointTaskProgress.durationSeconds = 0.0;
            m_waypointTaskProgress.resultSummary = "Waypoint Analysis: " + std::to_string(stats.totalModified) + " nodes updated (" +
                std::to_string(stats.crouchAssigned) + " crouch, " +
                std::to_string(stats.jumpAssigned) + " jump, " +
                std::to_string(stats.campAnglesCalculated) + " camp sightlines, " +
                std::to_string(stats.blockedLinksPruned) + " blocked links pruned)!";
            m_waypointTaskProgress.success = true;
            m_waypointTaskProgress.progress.store(1.0f);
            m_waypointTaskProgress.completed = true;
            m_waypointTaskProgress.isRunning.store(false);
        }
    });
    return true;
}

void EditorScene::UpdateWaypointTasks() {
    if (m_waypointTaskProgress.completed) {
        m_waypointTaskProgress.completed = false;
        if (m_waypointTaskThread.joinable()) {
            m_waypointTaskThread.join();
        }

        if (m_waypointTaskProgress.success) {
            m_isModified = true;
            RebuildWaypointRenderer();
            ShowToast(m_waypointTaskProgress.resultSummary);
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
                // Enforce orthogonal connections (1.25x snapDist) to avoid diagonal cross-link clutter
                if (dsq <= (snapDist * 1.25f) * (snapDist * 1.25f) && std::abs(diff.z) <= 45.0f) {
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

    uint32_t createdId = node->id;
    if (m_cmdMgr) {
        std::vector<WaypointIncomingLink> inc;
        for (const auto& other : m_waypoints.GetNodes()) {
            if (other.id == createdId) continue;
            for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                if (other.connections[c] == static_cast<int16_t>(createdId)) {
                    inc.push_back({ other.id, other.connectionFlags[c] });
                }
            }
        }
        WaypointNode copy = *node;
        m_waypoints.RemoveNode(createdId);
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdAddWaypoint>(this, copy, inc));
    } else {
        SelectWaypoint(createdId);
        m_showWaypoints = true;
        m_waypointRenderer.SetShowWaypoints(true);
        RebuildWaypointRenderer();
        m_isModified = true;
    }
    ShowToast("Added Waypoint #" + std::to_string(createdId));
    return createdId;
}

void EditorScene::SnapSelectedWaypointToFloor() {
    if (m_selectedWaypointId == 0 || !HasBSP()) return;
    WaypointNode* node = m_waypoints.GetNodeByID(m_selectedWaypointId);
    if (!node) return;

    Vector3 start(node->origin.x, node->origin.y, node->origin.z + 32.0f);
    Vector3 end(node->origin.x, node->origin.y, node->origin.z - 2048.0f);
    BSPTraceResult tr;
    if (m_bsp->TraceWorld(start, end, HULL_POINT, &tr) && !tr.startsolid && !tr.allsolid) {
        Vector3 newPos(node->origin.x, node->origin.y, tr.endpos.z + 18.0f);
        if (m_cmdMgr) {
            m_cmdMgr->ExecuteCommand(std::make_unique<CmdMoveWaypoint>(this, node->id, node->origin, newPos));
        } else {
            node->origin = newPos;
            m_waypoints.CalculateWayzone(node->id, m_bsp.get());
            RebuildWaypointRenderer();
            m_isModified = true;
        }
        ShowToast("Snapped Waypoint #" + std::to_string(node->id) + " to floor.");
    }
}

bool EditorScene::ConnectSelectedWaypointTo(uint32_t targetId, uint16_t connFlags, bool bidirectional) {
    if (m_selectedWaypointId == 0 || targetId == 0 || m_selectedWaypointId == targetId) return false;
    if (m_cmdMgr) {
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdConnectWaypoints>(this, m_selectedWaypointId, targetId, bidirectional, connFlags));
        ShowToast("Connected Waypoint #" + std::to_string(m_selectedWaypointId) + " to #" + std::to_string(targetId));
        return true;
    }
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
    if (m_cmdMgr) {
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdDeleteWaypoint>(this, id));
    } else {
        SelectWaypoint(0);
        m_waypoints.DeleteNode(id);
        RebuildWaypointRenderer();
        m_isModified = true;
    }
    ShowToast("Deleted Waypoint #" + std::to_string(id));
}

void EditorScene::SelectWaypointConnection(uint32_t fromId, uint32_t toId) {
    m_selectedWaypointConnection.fromId = fromId;
    m_selectedWaypointConnection.toId = toId;
    m_selectedWaypointId = 0;
    m_selectedWaypointIds.clear();
    m_selectedAreaId = 0;
    m_selectedAreaIds.clear();
    m_selectedEntityIndex = -1;
    m_selectedLadderId = 0;
    m_selectedConnection.clear();
    RebuildWaypointRenderer();
    ShowToast("Selected Waypoint Link #" + std::to_string(fromId) + " -> #" + std::to_string(toId));
}

void EditorScene::ClearSelectedWaypointConnection() {
    m_selectedWaypointConnection.clear();
    RebuildWaypointRenderer();
}

bool EditorScene::DeleteSelectedWaypointConnection() {
    if (!m_selectedWaypointConnection.valid()) return false;
    uint32_t from = m_selectedWaypointConnection.fromId;
    uint32_t to = m_selectedWaypointConnection.toId;
    ClearSelectedWaypointConnection();
    if (m_cmdMgr) {
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdDisconnectWaypoints>(this, from, to, false));
    } else {
        m_waypoints.DisconnectNodes(from, to, false);
        SetModified(true);
        RebuildWaypointRenderer();
    }
    ShowToast("Deleted Waypoint Link #" + std::to_string(from) + " -> #" + std::to_string(to));
    return true;
}

bool EditorScene::ReverseSelectedWaypointConnection() {
    if (!m_selectedWaypointConnection.valid()) return false;
    uint32_t from = m_selectedWaypointConnection.fromId;
    uint32_t to = m_selectedWaypointConnection.toId;
    auto* fromNode = m_waypoints.GetNode(from);
    if (!fromNode) return false;
    uint16_t flags = WPT_CONN_NONE;
    for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
        if (fromNode->connections[c] == static_cast<int16_t>(to)) {
            flags = fromNode->connectionFlags[c];
            break;
        }
    }
    m_waypoints.DisconnectNodes(from, to, false);
    m_waypoints.ConnectNodes(to, from, false, flags);
    m_selectedWaypointConnection.fromId = to;
    m_selectedWaypointConnection.toId = from;
    SetModified(true);
    RebuildWaypointRenderer();
    ShowToast("Reversed link: now #" + std::to_string(to) + " -> #" + std::to_string(from));
    return true;
}

bool EditorScene::ToggleSelectedWaypointConnectionBidirectional() {
    if (!m_selectedWaypointConnection.valid()) return false;
    uint32_t from = m_selectedWaypointConnection.fromId;
    uint32_t to = m_selectedWaypointConnection.toId;
    auto* fromNode = m_waypoints.GetNode(from);
    auto* toNode = m_waypoints.GetNode(to);
    if (!fromNode || !toNode) return false;

    bool toHasFrom = toNode->HasConnectionTo(static_cast<int16_t>(from));
    if (toHasFrom) {
        if (m_cmdMgr) {
            m_cmdMgr->ExecuteCommand(std::make_unique<CmdDisconnectWaypoints>(this, to, from, false));
        } else {
            m_waypoints.DisconnectNodes(to, from, false);
            SetModified(true);
            RebuildWaypointRenderer();
        }
        ShowToast("Converted to 1-Way (Unidirectional): #" + std::to_string(from) + " -> #" + std::to_string(to));
    } else {
        uint16_t flags = WPT_CONN_NONE;
        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            if (fromNode->connections[c] == static_cast<int16_t>(to)) {
                flags = fromNode->connectionFlags[c];
                break;
            }
        }
        if (m_cmdMgr) {
            m_cmdMgr->ExecuteCommand(std::make_unique<CmdConnectWaypoints>(this, to, from, false, flags));
        } else {
            m_waypoints.ConnectNodes(to, from, false, flags);
            SetModified(true);
            RebuildWaypointRenderer();
        }
        ShowToast("Converted to 2-Way (Bidirectional): #" + std::to_string(from) + " <-> #" + std::to_string(to));
    }
    return true;
}

bool EditorScene::SetSelectedWaypointConnectionFlags(uint16_t flags) {
    if (!m_selectedWaypointConnection.valid()) return false;
    uint32_t from = m_selectedWaypointConnection.fromId;
    uint32_t to = m_selectedWaypointConnection.toId;
    auto* fromNode = m_waypoints.GetNode(from);
    if (!fromNode) return false;
    for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
        if (fromNode->connections[c] == static_cast<int16_t>(to)) {
            fromNode->connectionFlags[c] = flags;
            break;
        }
    }
    SetModified(true);
    RebuildWaypointRenderer();
    return true;
}

void EditorScene::SetWaypointConnectMode(bool active) {
    m_waypointConnectMode = active;
    if (active) {
        ShowToast("Waypoint Connect Mode: Click another waypoint to connect (Shift: 1-way, Esc: cancel)");
    }
}

void EditorScene::ToggleWaypointConnectMode() {
    SetWaypointConnectMode(!m_waypointConnectMode);
}

void EditorScene::DuplicateSelectedWaypoints() {
    if (m_selectedWaypointIds.empty() && m_selectedWaypointId == 0) return;
    std::vector<uint32_t> sourceIds;
    if (m_selectedWaypointIds.empty()) sourceIds.push_back(m_selectedWaypointId);
    else sourceIds.assign(m_selectedWaypointIds.begin(), m_selectedWaypointIds.end());

    Vector3 offset(m_gridSize, 0.0f, 0.0f);
    std::unordered_map<uint32_t, uint32_t> idMap;
    std::vector<WaypointNode> clonedNodes;

    uint32_t curNext = m_waypoints.GetNextId();
    for (uint32_t id : sourceIds) {
        const auto* src = m_waypoints.GetNode(id);
        if (!src) continue;
        WaypointNode clone = *src;
        clone.id = curNext++;
        clone.origin += offset;
        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            clone.connections[c] = -1;
            clone.connectionFlags[c] = 0;
        }
        idMap[id] = clone.id;
        clonedNodes.push_back(clone);
    }
    m_waypoints.SetNextId(curNext);

    for (size_t i = 0; i < clonedNodes.size(); ++i) {
        uint32_t origId = sourceIds[i];
        const auto* origNode = m_waypoints.GetNode(origId);
        if (!origNode) continue;
        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            int16_t tgt = origNode->connections[c];
            if (tgt > 0 && idMap.find(static_cast<uint32_t>(tgt)) != idMap.end()) {
                clonedNodes[i].AddConnection(static_cast<int16_t>(idMap[tgt]), origNode->connectionFlags[c]);
            }
        }
    }

    if (clonedNodes.empty()) return;

    if (m_cmdMgr) {
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdDuplicateWaypoints>(this, clonedNodes));
    } else {
        std::vector<uint32_t> newIds;
        for (const auto& n : clonedNodes) {
            m_waypoints.InsertNode(n);
            newIds.push_back(n.id);
        }
        BoxSelectWaypoints(newIds, false, false);
        SetModified(true);
        RebuildWaypointRenderer();
    }
    ShowToast("Duplicated " + std::to_string(clonedNodes.size()) + " waypoint(s) [Offset: " + std::to_string((int)m_gridSize) + "u]");
}

void EditorScene::BridgeSelectedWaypoints() {
    if (m_selectedWaypointIds.size() != 2) {
        ShowToast("Bridge Tool: Select exactly 2 waypoints to interpolate path");
        return;
    }
    auto it = m_selectedWaypointIds.begin();
    uint32_t idA = *it++;
    uint32_t idB = *it;
    const auto* nodeA = m_waypoints.GetNode(idA);
    const auto* nodeB = m_waypoints.GetNode(idB);
    if (!nodeA || !nodeB) return;

    Vector3 pA = nodeA->origin;
    Vector3 pB = nodeB->origin;
    float dist = (pB - pA).Length();
    float stepDist = std::max(64.0f, m_gridSize);
    int numSteps = static_cast<int>(std::round(dist / stepDist));

    if (numSteps < 2) {
        ConnectSelectedWaypointTo(idB, WPT_CONN_NONE, true);
        ShowToast("Directly connected waypoint #" + std::to_string(idA) + " <-> #" + std::to_string(idB));
        return;
    }

    std::vector<WaypointNode> bridgeNodes;
    uint32_t curNext = m_waypoints.GetNextId();

    for (int i = 1; i < numSteps; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(numSteps);
        Vector3 pos = pA + (pB - pA) * t;
        if (HasBSP()) {
            Vector3 ground;
            if (m_bsp->GetGround(pos + Vector3(0.0f, 0.0f, 18.0f), &ground, 500.0f)) {
                pos = ground + Vector3(0.0f, 0.0f, 18.0f);
            }
        }
        WaypointNode bn;
        bn.id = curNext++;
        bn.origin = pos;
        bn.flags = nodeA->flags;
        bn.radius = (nodeA->radius + nodeB->radius) * 0.5f;
        bridgeNodes.push_back(bn);
    }
    m_waypoints.SetNextId(curNext);

    for (size_t i = 0; i < bridgeNodes.size(); ++i) {
        if (i > 0) {
            bridgeNodes[i - 1].AddConnection(static_cast<int16_t>(bridgeNodes[i].id), WPT_CONN_NONE);
            bridgeNodes[i].AddConnection(static_cast<int16_t>(bridgeNodes[i - 1].id), WPT_CONN_NONE);
        }
    }

    if (m_cmdMgr) {
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdBridgeWaypoints>(this, idA, idB, bridgeNodes));
    } else {
        for (size_t i = 0; i < bridgeNodes.size(); ++i) {
            m_waypoints.InsertNode(bridgeNodes[i]);
            if (i == 0) {
                m_waypoints.ConnectNodes(idA, bridgeNodes[i].id, true, WPT_CONN_NONE);
            } else {
                m_waypoints.ConnectNodes(bridgeNodes[i - 1].id, bridgeNodes[i].id, true, WPT_CONN_NONE);
            }
        }
        m_waypoints.ConnectNodes(bridgeNodes.back().id, idB, true, WPT_CONN_NONE);

        std::vector<uint32_t> allBridgedIds;
        for (const auto& b : bridgeNodes) allBridgedIds.push_back(b.id);
        BoxSelectWaypoints(allBridgedIds, false, false);
        SetModified(true);
        RebuildWaypointRenderer();
    }
    ShowToast("Bridged " + std::to_string(bridgeNodes.size()) + " intermediate waypoints between #" +
              std::to_string(idA) + " and #" + std::to_string(idB));
}

void EditorScene::AlignSelectedWaypoints(WaypointAlignMode mode) {
    if (m_selectedWaypointIds.size() < 2) return;
    std::vector<uint32_t> ids(m_selectedWaypointIds.begin(), m_selectedWaypointIds.end());
    float minX = 1e9f, maxX = -1e9f, sumX = 0.0f;
    float minY = 1e9f, maxY = -1e9f, sumY = 0.0f;
    float minZ = 1e9f, maxZ = -1e9f, sumZ = 0.0f;

    for (uint32_t id : ids) {
        const auto* n = m_waypoints.GetNode(id);
        if (!n) continue;
        minX = std::min(minX, n->origin.x); maxX = std::max(maxX, n->origin.x); sumX += n->origin.x;
        minY = std::min(minY, n->origin.y); maxY = std::max(maxY, n->origin.y); sumY += n->origin.y;
        minZ = std::min(minZ, n->origin.z); maxZ = std::max(maxZ, n->origin.z); sumZ += n->origin.z;
    }
    float avgX = sumX / ids.size();
    float avgY = sumY / ids.size();
    float avgZ = sumZ / ids.size();

    std::vector<CmdBatchMoveWaypoints::MoveEntry> entries;
    for (uint32_t id : ids) {
        auto* n = m_waypoints.GetNode(id);
        if (!n) continue;
        Vector3 newPos = n->origin;
        switch (mode) {
            case WaypointAlignMode::MinX:     newPos.x = minX; break;
            case WaypointAlignMode::CenterX:  newPos.x = avgX; break;
            case WaypointAlignMode::MaxX:     newPos.x = maxX; break;
            case WaypointAlignMode::MinY:     newPos.y = minY; break;
            case WaypointAlignMode::CenterY:  newPos.y = avgY; break;
            case WaypointAlignMode::MaxY:     newPos.y = maxY; break;
            case WaypointAlignMode::FloorZ:   newPos.z = minZ; break;
            case WaypointAlignMode::AverageZ: newPos.z = avgZ; break;
        }
        if (newPos != n->origin) {
            entries.push_back({ id, n->origin, newPos });
        }
    }

    if (entries.empty()) return;
    if (m_cmdMgr) {
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdBatchMoveWaypoints>(this, entries));
    } else {
        for (const auto& e : entries) {
            auto* n = m_waypoints.GetNode(e.id);
            if (n) {
                n->origin = e.newPos;
                if (m_bsp) m_waypoints.CalculateWayzone(e.id, m_bsp.get());
            }
        }
        SetModified(true);
        RebuildWaypointRenderer();
    }
    ShowToast("Aligned " + std::to_string(entries.size()) + " waypoints");
}

void EditorScene::NudgeSelection(float dx, float dy, float dz) {
    Vector3 delta(dx, dy, dz);
    if (m_selectedWaypointIds.size() > 1) {
        std::vector<CmdBatchMoveWaypoints::MoveEntry> entries;
        for (uint32_t id : m_selectedWaypointIds) {
            auto* node = m_waypoints.GetNode(id);
            if (node) {
                entries.push_back({ id, node->origin, node->origin + delta });
            }
        }
        if (!entries.empty()) {
            if (m_cmdMgr) {
                m_cmdMgr->ExecuteCommand(std::make_unique<CmdBatchMoveWaypoints>(this, entries));
            } else {
                for (const auto& e : entries) {
                    auto* node = m_waypoints.GetNode(e.id);
                    if (node) node->origin = e.newPos;
                }
                SetModified(true);
                RebuildWaypointRenderer();
            }
            ShowToast("Nudged " + std::to_string(entries.size()) + " waypoints by grid size (" + std::to_string((int)m_gridSize) + "u)");
        }
    } else if (m_selectedWaypointId != 0) {
        auto* node = m_waypoints.GetNode(m_selectedWaypointId);
        if (node) {
            Vector3 oldPos = node->origin;
            Vector3 newPos = oldPos + delta;
            if (m_cmdMgr) {
                m_cmdMgr->ExecuteCommand(std::make_unique<CmdMoveWaypoint>(this, node->id, oldPos, newPos));
            } else {
                node->origin = newPos;
                SetModified(true);
                RebuildWaypointRenderer();
            }
            ShowToast("Nudged Waypoint #" + std::to_string(node->id) + " by grid size (" + std::to_string((int)m_gridSize) + "u)");
        }
    } else if (m_selectedAreaIds.size() > 1 && m_nav && m_nav->IsLoaded()) {
        std::vector<std::unique_ptr<IEditCommand>> cmds;
        for (uint32_t aid : m_selectedAreaIds) {
            NavArea* a = m_nav->GetAreaByID(aid);
            if (!a) continue;
            NavExtent oldExt = a->GetExtent();
            float oldNeZ = a->GetNEZ();
            float oldSwZ = a->GetSWZ();
            NavExtent newExt = oldExt;
            newExt.lo += delta;
            newExt.hi += delta;
            float newNeZ = oldNeZ + delta.z;
            float newSwZ = oldSwZ + delta.z;
            cmds.push_back(std::make_unique<CmdTransformArea>(this, aid, oldExt, oldNeZ, oldSwZ, newExt, newNeZ, newSwZ, "Nudge Area"));
        }
        if (!cmds.empty()) {
            if (m_cmdMgr) {
                m_cmdMgr->ExecuteCommand(std::make_unique<CmdCompound>(std::move(cmds), "Nudge Areas"));
            } else {
                for (uint32_t aid : m_selectedAreaIds) {
                    NavArea* a = m_nav->GetAreaByID(aid);
                    if (a) {
                        NavExtent ext = a->GetExtent();
                        ext.lo += delta; ext.hi += delta;
                        a->SetExtent(ext);
                        a->SetCornerHeights(a->GetNEZ() + delta.z, a->GetSWZ() + delta.z);
                    }
                }
                SetModified(true);
                RebuildNavRenderer();
            }
            ShowToast("Nudged " + std::to_string(m_selectedAreaIds.size()) + " areas by grid size (" + std::to_string((int)m_gridSize) + "u)");
        }
    } else if (m_selectedAreaId != 0 && m_nav && m_nav->IsLoaded()) {
        NavArea* a = m_nav->GetAreaByID(m_selectedAreaId);
        if (a) {
            NavExtent oldExt = a->GetExtent();
            float oldNeZ = a->GetNEZ();
            float oldSwZ = a->GetSWZ();
            NavExtent newExt = oldExt;
            newExt.lo += delta;
            newExt.hi += delta;
            float newNeZ = oldNeZ + delta.z;
            float newSwZ = oldSwZ + delta.z;
            if (m_cmdMgr) {
                m_cmdMgr->ExecuteCommand(std::make_unique<CmdTransformArea>(this, a->GetID(), oldExt, oldNeZ, oldSwZ, newExt, newNeZ, newSwZ, "Nudge Area"));
            } else {
                a->SetExtent(newExt);
                a->SetCornerHeights(newNeZ, newSwZ);
                SetModified(true);
                RebuildNavRenderer();
            }
            ShowToast("Nudged Area #" + std::to_string(a->GetID()) + " by grid size (" + std::to_string((int)m_gridSize) + "u)");
        }
    } else if (m_selectedEntityIndex >= 0) {
        EditorEntity* ent = GetSelectedEntity();
        if (ent) {
            ent->origin += delta;
            ent->worldMins += delta;
            ent->worldMaxs += delta;
            SetModified(true);
            ShowToast("Nudged Entity #" + std::to_string(m_selectedEntityIndex) + " by grid size (" + std::to_string((int)m_gridSize) + "u)");
        }
    }
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

// --- Continuous Pen / Breadcrumb Path Tool ---

void EditorScene::SetPenToolActive(bool active) {
    m_penToolActive = active;
    if (!active) {
        EndPenStroke();
    } else {
        m_penLastWaypointId = m_selectedWaypointId;
        m_penRubberbandValid = false;
        ShowToast("Pen / Breadcrumb Tool Active: Click floor to place & auto-connect chain");
    }
    RebuildWaypointRenderer();
}

void EditorScene::EndPenStroke() {
    m_penLastWaypointId = 0;
    m_penRubberbandValid = false;
    RebuildWaypointRenderer();
}

void EditorScene::UpdatePenRubberband(const Ray& ray) {
    if (!m_penToolActive) {
        m_penRubberbandValid = false;
        return;
    }
    Vector3 hitPos;
    if (ScenePicker::PickBSPFloor(*this, ray, &hitPos)) {
        m_penRubberbandValid = true;
        m_penRubberbandTarget = hitPos + Vector3(0.0f, 0.0f, 18.0f);
        if (m_penLastWaypointId != 0) {
            const WaypointNode* lastWp = m_waypoints.GetNodeByID(m_penLastWaypointId);
            if (lastWp) {
                m_penRubberbandDist = (m_penRubberbandTarget - lastWp->origin).Length();
                if (m_bsp && m_bsp->IsLoaded()) {
                    BSPTraceResult tr;
                    m_bsp->TraceWorld(lastWp->origin + Vector3(0,0,36), m_penRubberbandTarget + Vector3(0,0,36), HULL_HUMAN, &tr);
                    m_penRubberbandClear = (tr.fraction >= 0.95f && !tr.startsolid);
                } else {
                    m_penRubberbandClear = true;
                }
            }
        }
        RebuildWaypointRenderer();
    } else {
        if (m_penRubberbandValid) {
            m_penRubberbandValid = false;
            RebuildWaypointRenderer();
        }
    }
}

uint32_t EditorScene::OnPenClick(const Ray& ray) {
    if (!m_penToolActive) return 0;

    // Check if clicked an existing waypoint
    float dist = 0.0f;
    uint32_t hitWpt = ScenePicker::PickWaypoint(*this, ray, &dist);
    if (hitWpt != 0) {
        if (m_penLastWaypointId != 0 && m_penLastWaypointId != hitWpt) {
            ConnectSelectedWaypointTo(hitWpt, WPT_CONN_NONE, true);
        }
        m_penLastWaypointId = hitWpt;
        m_selectedWaypointId = hitWpt;
        m_selectedWaypointIds.clear();
        m_selectedWaypointIds.insert(hitWpt);
        RebuildWaypointRenderer();
        return hitWpt;
    }

    // Otherwise place new waypoint on floor
    Vector3 floorHit;
    if (!ScenePicker::PickBSPFloor(*this, ray, &floorHit)) {
        return 0;
    }

    Vector3 spawnPos = floorHit + Vector3(0.0f, 0.0f, 18.0f);
    WaypointNode* newNode = m_waypoints.AddNode(spawnPos, m_activeWaypointAddFlags, m_activeWaypointAddRadius);
    if (!newNode) return 0;

    uint32_t newId = newNode->id;
    if (m_penLastWaypointId != 0) {
        m_waypoints.ConnectNodes(m_penLastWaypointId, newId, true, WPT_CONN_NONE);
    }

    if (m_cmdMgr) {
        WaypointNode copy = *newNode;
        std::vector<WaypointIncomingLink> inc;
        if (m_penLastWaypointId != 0) {
            inc.push_back({ m_penLastWaypointId, WPT_CONN_NONE });
        }
        m_waypoints.RemoveNode(newId);
        m_cmdMgr->ExecuteCommand(std::make_unique<CmdAddWaypoint>(this, copy, inc));
    } else {
        m_isModified = true;
        RebuildWaypointRenderer();
    }

    m_penLastWaypointId = newId;
    m_selectedWaypointId = newId;
    m_selectedWaypointIds.clear();
    m_selectedWaypointIds.insert(newId);
    ShowToast("Pen placed Waypoint #" + std::to_string(newId));
    return newId;
}

// --- Automatic Goal / Objective Snapping ---

size_t EditorScene::SnapObjectivesFromEntities() {
    if (!HasBSP()) {
        ShowToast("No BSP loaded for objective snapping!");
        return 0;
    }

    size_t objectivesHandled = 0;
    const auto& entities = m_bsp->GetEntities();

    auto FindOrCreateObjectiveWp = [&](const Vector3& origin, uint32_t flag) {
        Vector3 floorPos = origin;
        Vector3 ground;
        if (m_bsp->GetGround(origin + Vector3(0, 0, 18.0f), &ground, 500.0f)) {
            floorPos = ground + Vector3(0, 0, 18.0f);
        }

        int nearestIdx = m_waypoints.FindNearestNode(floorPos, 80.0f);
        if (nearestIdx >= 0) {
            WaypointNode& node = m_waypoints.GetNodes()[nearestIdx];
            node.origin = floorPos;
            node.flags |= flag;
            objectivesHandled++;
        } else {
            WaypointNode* node = m_waypoints.AddNode(floorPos, flag, 48.0f);
            if (node) {
                int nearNeighbor = m_waypoints.FindNearestNode(floorPos, 250.0f);
                if (nearNeighbor >= 0 && m_waypoints.GetNodes()[nearNeighbor].id != node->id) {
                    m_waypoints.ConnectNodes(node->id, m_waypoints.GetNodes()[nearNeighbor].id, true, WPT_CONN_NONE);
                }
                objectivesHandled++;
            }
        }
    };

    for (const auto& ent : entities) {
        std::string cls = ent.classname;
        Vector3 org;
        if (!ent.GetOrigin(org)) {
            std::string modelStr = ent.GetString("model");
            if (!modelStr.empty() && modelStr[0] == '*') {
                int mIdx = std::atoi(modelStr.c_str() + 1);
                const dmodel_t* mod = m_bsp->GetModel(mIdx);
                if (mod) {
                    org = Vector3((mod->mins.x + mod->maxs.x) * 0.5f,
                                  (mod->mins.y + mod->maxs.y) * 0.5f,
                                  (mod->mins.z + mod->maxs.z) * 0.5f);
                } else {
                    continue;
                }
            } else {
                continue;
            }
        }

        if (cls == "info_player_start") {
            FindOrCreateObjectiveWp(org, WPT_FLAG_TEAM_CT);
        } else if (cls == "info_player_deathmatch") {
            FindOrCreateObjectiveWp(org, WPT_FLAG_TEAM_T);
        } else if (cls == "func_bomb_target" || cls == "info_bomb_target") {
            FindOrCreateObjectiveWp(org, WPT_FLAG_GOAL);
        } else if (cls == "hostage_entity" || cls == "info_hostage_goal" || cls == "func_hostage_rescue") {
            FindOrCreateObjectiveWp(org, WPT_FLAG_RESCUE | WPT_FLAG_GOAL);
        } else if (cls == "func_vip_safetyzone" || cls == "info_vip_start") {
            FindOrCreateObjectiveWp(org, WPT_FLAG_GOAL | WPT_FLAG_TEAM_CT);
        } else if (cls == "armoury_entity") {
            FindOrCreateObjectiveWp(org, WPT_FLAG_GOAL);
        }
    }

    if (objectivesHandled > 0) {
        m_isModified = true;
        RebuildWaypointRenderer();
        ShowToast("Snapped / Created " + std::to_string(objectivesHandled) + " Map Objectives from BSP Entities!");
    } else {
        ShowToast("No objective entities found to snap");
    }

    return objectivesHandled;
}

// --- Ladder Waypoint Assigning Tools ---

size_t EditorScene::AssignLaddersFromBSP() {
    if (!HasBSP()) {
        ShowToast("No BSP loaded for ladder extraction!");
        return 0;
    }

    auto ladderEnts = m_bsp->FindEntities("func_ladder");
    if (ladderEnts.empty()) {
        ShowToast("No func_ladder entities found in map");
        return 0;
    }

    size_t created = 0;
    for (const BSPEntity* ent : ladderEnts) {
        std::string modelStr = ent->GetString("model");
        if (modelStr.empty() || modelStr[0] != '*') continue;
        int mIdx = std::atoi(modelStr.c_str() + 1);
        const dmodel_t* mod = m_bsp->GetModel(mIdx);
        if (!mod) continue;

        Vector3 btm((mod->mins.x + mod->maxs.x) * 0.5f, (mod->mins.y + mod->maxs.y) * 0.5f, mod->mins.z + 18.0f);
        Vector3 top((mod->mins.x + mod->maxs.x) * 0.5f, (mod->mins.y + mod->maxs.y) * 0.5f, mod->maxs.z);

        WaypointNode* nodeBtm = m_waypoints.AddNode(btm, WPT_FLAG_LADDER, 0.0f);
        WaypointNode* nodeTop = m_waypoints.AddNode(top, WPT_FLAG_LADDER, 0.0f);

        if (nodeBtm && nodeTop) {
            m_waypoints.ConnectNodes(nodeBtm->id, nodeTop->id, true, WPT_CONN_NONE);
            created += 2;

            int nearBtm = m_waypoints.FindNearestNode(btm, 180.0f);
            if (nearBtm >= 0) {
                uint32_t nbId = m_waypoints.GetNodes()[nearBtm].id;
                if (nbId != nodeBtm->id && nbId != nodeTop->id) {
                    m_waypoints.ConnectNodes(nodeBtm->id, nbId, true, WPT_CONN_NONE);
                }
            }

            int nearTop = m_waypoints.FindNearestNode(top, 180.0f);
            if (nearTop >= 0) {
                uint32_t ntId = m_waypoints.GetNodes()[nearTop].id;
                if (ntId != nodeBtm->id && ntId != nodeTop->id) {
                    m_waypoints.ConnectNodes(nodeTop->id, ntId, true, WPT_CONN_NONE);
                }
            }
        }
    }

    if (created > 0) {
        m_isModified = true;
        RebuildWaypointRenderer();
        ShowToast("Created " + std::to_string(created) + " ladder waypoints from BSP func_ladder!");
    }
    return created;
}

bool EditorScene::CreateLadderPairFromSelected() {
    if (m_selectedWaypointIds.size() != 2) {
        ShowToast("Please select exactly 2 waypoints to form a ladder pair");
        return false;
    }
    auto it = m_selectedWaypointIds.begin();
    uint32_t idA = *it++;
    uint32_t idB = *it;

    WaypointNode* nA = m_waypoints.GetNodeByID(idA);
    WaypointNode* nB = m_waypoints.GetNodeByID(idB);
    if (!nA || !nB) return false;

    nA->flags |= WPT_FLAG_LADDER;
    nA->radius = 0.0f;
    nB->flags |= WPT_FLAG_LADDER;
    nB->radius = 0.0f;

    m_waypoints.ConnectNodes(idA, idB, true, WPT_CONN_NONE);
    m_isModified = true;
    RebuildWaypointRenderer();
    ShowToast("Created bidirectional Ladder Pair between #" + std::to_string(idA) + " and #" + std::to_string(idB));
    return true;
}

// --- Ghost Bot Simulation & Path Auditing ---

bool EditorScene::StartGhostBotSimulation(uint32_t startId, uint32_t goalId) {
    if (startId == 0 || goalId == 0 || startId == goalId) {
        ShowToast("Invalid Start / Goal waypoint IDs for simulation");
        return false;
    }

    m_ghostBotStartId = startId;
    m_ghostBotGoalId = goalId;
    float cost = 0.0f;
    if (!m_waypoints.FindPath(startId, goalId, m_ghostBotPath, &cost)) {
        ShowToast("No reachable path found between Waypoint #" + std::to_string(startId) + " and #" + std::to_string(goalId));
        m_ghostBotActive = false;
        m_ghostBotPath.clear();
        return false;
    }

    m_ghostBotAudit = m_waypoints.AuditPath(m_ghostBotPath, m_bsp.get());
    m_ghostBotActive = true;
    m_ghostBotPaused = false;
    m_ghostBotCurrentStep = 0;
    m_ghostBotStepProgress = 0.0f;

    const WaypointNode* startNode = m_waypoints.GetNodeByID(startId);
    if (startNode) {
        m_ghostBotPos = startNode->origin;
        if (m_ghostBotPath.size() >= 2) {
            const WaypointNode* nextNode = m_waypoints.GetNodeByID(m_ghostBotPath[1]);
            if (nextNode) {
                Vector3 d = nextNode->origin - startNode->origin;
                m_ghostBotYaw = std::atan2(d.y, d.x) * 180.0f / (float)M_PI;
            }
        }
    }

    RebuildWaypointRenderer();
    ShowToast("Ghost Bot Simulation Started: " + std::to_string(m_ghostBotPath.size()) + " nodes (" +
              std::to_string((int)m_ghostBotAudit.totalDistance) + "u distance)");
    return true;
}

void EditorScene::StopGhostBotSimulation() {
    m_ghostBotActive = false;
    m_ghostBotPaused = false;
    m_ghostBotPath.clear();
    RebuildWaypointRenderer();
    ShowToast("Ghost Bot Simulation Stopped");
}

void EditorScene::ResetGhostBotSimulation() {
    m_ghostBotCurrentStep = 0;
    m_ghostBotStepProgress = 0.0f;
    if (!m_ghostBotPath.empty()) {
        const WaypointNode* startNode = m_waypoints.GetNodeByID(m_ghostBotPath[0]);
        if (startNode) m_ghostBotPos = startNode->origin;
    }
    RebuildWaypointRenderer();
}

void EditorScene::StepGhostBotSimulation() {
    if (m_ghostBotPath.size() < 2) return;
    m_ghostBotCurrentStep++;
    if (m_ghostBotCurrentStep + 1 >= m_ghostBotPath.size()) {
        if (m_ghostBotLoop) m_ghostBotCurrentStep = 0;
        else m_ghostBotCurrentStep = m_ghostBotPath.size() - 2;
    }
    const WaypointNode* n = m_waypoints.GetNodeByID(m_ghostBotPath[m_ghostBotCurrentStep]);
    if (n) m_ghostBotPos = n->origin;
    RebuildWaypointRenderer();
}

void EditorScene::UpdateGhostBot(float dt) {
    if (!m_ghostBotActive || m_ghostBotPaused || m_ghostBotPath.size() < 2) return;

    if (m_ghostBotCurrentStep + 1 >= m_ghostBotPath.size()) {
        if (m_ghostBotLoop) {
            m_ghostBotCurrentStep = 0;
            m_ghostBotStepProgress = 0.0f;
            const WaypointNode* n0 = m_waypoints.GetNodeByID(m_ghostBotPath[0]);
            if (n0) m_ghostBotPos = n0->origin;
        } else {
            m_ghostBotPaused = true;
            return;
        }
    }

    const WaypointNode* nCur = m_waypoints.GetNodeByID(m_ghostBotPath[m_ghostBotCurrentStep]);
    const WaypointNode* nNext = m_waypoints.GetNodeByID(m_ghostBotPath[m_ghostBotCurrentStep + 1]);
    if (!nCur || !nNext) return;

    Vector3 segVec = nNext->origin - nCur->origin;
    float segLen = segVec.Length();
    if (segLen <= 1.0f) {
        m_ghostBotCurrentStep++;
        return;
    }

    float baseSpeed = 250.0f;
    if (nNext->flags & WPT_FLAG_CROUCH) baseSpeed = 90.0f;
    else if (nNext->flags & WPT_FLAG_LADDER) baseSpeed = 150.0f;

    float moveSpeed = baseSpeed * m_ghostBotSpeedMultiplier;
    float distToMove = moveSpeed * dt;

    float currentDistAlongSeg = m_ghostBotStepProgress * segLen;
    float newDist = currentDistAlongSeg + distToMove;

    if (newDist >= segLen) {
        m_ghostBotCurrentStep++;
        m_ghostBotStepProgress = 0.0f;
        m_ghostBotPos = nNext->origin;
    } else {
        m_ghostBotStepProgress = newDist / segLen;
        m_ghostBotPos = nCur->origin + segVec * m_ghostBotStepProgress;
    }

    if (segVec.Length() > 0.1f) {
        m_ghostBotYaw = std::atan2(segVec.y, segVec.x) * 180.0f / (float)M_PI;
    }

    RebuildWaypointRenderer();
}
