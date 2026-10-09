#include "editor/scene/editor_scene.h"
#include "editor/scene/scene_picker.h"
#include "editor/commands/nav_commands.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <imgui.h>

EditorScene::EditorScene()
    : m_bsp(std::make_unique<BSPFile>())
    , m_nav(std::make_unique<NavMesh>())
    , m_selectedAreaId(0)
    , m_hoveredAreaId(0)
    , m_bspMode(BSP_RENDER_TEXTURED)
    , m_showBSP(true)
    , m_showNAV(true)
    , m_showConnections(true)
{
    LoadRecentFiles();
    m_prefs.Load("navstudio_prefs.ini");
    ApplyPreferences();

    // Auto-detect standard Half-Life / Counter-Strike game directory
    static const char* kDefaultPaths[] = {
        "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Half-Life",
        "C:\\Program Files\\Steam\\steamapps\\common\\Half-Life"
    };
    for (const char* p : kDefaultPaths) {
        std::string fgdCheck = std::string(p) + "\\cstrike\\halflife-cs.fgd";
        FILE* f = std::fopen(fgdCheck.c_str(), "r");
        if (f) {
            std::fclose(f);
            m_gameDirectory = p;
            break;
        }
    }
}

EditorScene::~EditorScene() {
    if (m_loadThread.joinable()) {
        m_loadThread.join();
    }
}

void EditorScene::LoadRecentFiles() {
    m_recentFiles.clear();
    std::ifstream file("navstudio_recent.txt");
    if (!file.is_open()) return;
    std::string line;
    while (std::getline(file, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        if (!line.empty()) {
            m_recentFiles.push_back(line);
        }
    }
}

void EditorScene::SaveRecentFiles() {
    std::ofstream file("navstudio_recent.txt");
    if (!file.is_open()) return;
    for (const auto& path : m_recentFiles) {
        file << path << "\n";
    }
}

void EditorScene::ApplyPreferences() {
    m_gridSize = m_prefs.defaultGridSize;
    m_gridSnap = m_prefs.defaultGridSnap;
    m_meshSnap = m_prefs.defaultMeshSnap;
    m_meshSnapTolerance = m_prefs.meshSnapTolerance;
    m_cornerSnapTolerance = m_prefs.cornerSnapTolerance;
    SetShowSkybox(m_prefs.show3DSkybox);
    m_navRenderer.SetShowConnectionValidity(m_prefs.showConnectionValidity);
    m_navRenderer.SetMaxStepHeight(m_prefs.maxStepHeight);
}

void EditorScene::AddRecentFile(const std::string& path) {
    if (path.empty()) return;
    auto it = std::find(m_recentFiles.begin(), m_recentFiles.end(), path);
    if (it != m_recentFiles.end()) {
        m_recentFiles.erase(it);
    }
    m_recentFiles.insert(m_recentFiles.begin(), path);
    if (m_recentFiles.size() > 10) {
        m_recentFiles.resize(10);
    }
    SaveRecentFiles();
}

void EditorScene::ClearRecentFiles() {
    m_recentFiles.clear();
    SaveRecentFiles();
}

void EditorScene::SetGridSize(float size) {
    m_gridSize = std::max(1.0f, std::min(512.0f, size));
}

void EditorScene::IncreaseGridSize() {
    m_gridSize = std::min(512.0f, m_gridSize * 2.0f);
}

void EditorScene::DecreaseGridSize() {
    m_gridSize = std::max(1.0f, m_gridSize * 0.5f);
}

void EditorScene::ToggleGridSnap() {
    m_gridSnap = !m_gridSnap;
}

float EditorScene::SnapValue(float val) const {
    if (!m_gridSnap || m_gridSize < 1.0f) return val;
    return std::round(val / m_gridSize) * m_gridSize;
}

Vector3 EditorScene::SnapVector(const Vector3& v) const {
    return Vector3(SnapValue(v.x), SnapValue(v.y), SnapValue(v.z));
}

float EditorScene::SnapToNeighborEdge(uint32_t currentAreaId, float candidateVal, bool isXAxis,
                                     float refMinOtherAxis, float refMaxOtherAxis) const {
    if (!m_meshSnap || !m_nav || !m_nav->IsLoaded()) {
        return SnapValue(candidateVal);
    }

    float bestDist = m_meshSnapTolerance;
    float bestSnap = candidateVal;
    bool found = false;

    for (const auto* other : m_nav->GetAreas()) {
        if (!other || other->GetID() == currentAreaId) continue;
        const NavExtent& ext = other->GetExtent();

        if (isXAxis) {
            // Snapping an X coordinate: check if other area overlaps in Y
            if (ext.hi.y < refMinOtherAxis - m_meshSnapTolerance || ext.lo.y > refMaxOtherAxis + m_meshSnapTolerance) {
                continue;
            }
            float dLo = std::abs(candidateVal - ext.lo.x);
            if (dLo < bestDist) {
                bestDist = dLo;
                bestSnap = ext.lo.x;
                found = true;
            }
            float dHi = std::abs(candidateVal - ext.hi.x);
            if (dHi < bestDist) {
                bestDist = dHi;
                bestSnap = ext.hi.x;
                found = true;
            }
        } else {
            // Snapping a Y coordinate: check if other area overlaps in X
            if (ext.hi.x < refMinOtherAxis - m_meshSnapTolerance || ext.lo.x > refMaxOtherAxis + m_meshSnapTolerance) {
                continue;
            }
            float dLo = std::abs(candidateVal - ext.lo.y);
            if (dLo < bestDist) {
                bestDist = dLo;
                bestSnap = ext.lo.y;
                found = true;
            }
            float dHi = std::abs(candidateVal - ext.hi.y);
            if (dHi < bestDist) {
                bestDist = dHi;
                bestSnap = ext.hi.y;
                found = true;
            }
        }
    }

    if (found) {
        return bestSnap;
    }

    // Secondary Pass: Magnetic Collinear Edge Alignment
    if (m_prefs.enableCollinearSnap) {
        float colTol = m_prefs.collinearSnapTolerance;
        float bestColDist = colTol;
        float bestColSnap = candidateVal;
        bool colFound = false;

        for (const auto* other : m_nav->GetAreas()) {
            if (!other || other->GetID() == currentAreaId) continue;
            const NavExtent& ext = other->GetExtent();

            if (isXAxis) {
                float otherCenterY = (ext.lo.y + ext.hi.y) * 0.5f;
                float refCenterY = (refMinOtherAxis + refMaxOtherAxis) * 0.5f;
                if (std::abs(otherCenterY - refCenterY) > 800.0f) continue;

                float dLo = std::abs(candidateVal - ext.lo.x);
                if (dLo < bestColDist) { bestColDist = dLo; bestColSnap = ext.lo.x; colFound = true; }
                float dHi = std::abs(candidateVal - ext.hi.x);
                if (dHi < bestColDist) { bestColDist = dHi; bestColSnap = ext.hi.x; colFound = true; }
            } else {
                float otherCenterX = (ext.lo.x + ext.hi.x) * 0.5f;
                float refCenterX = (refMinOtherAxis + refMaxOtherAxis) * 0.5f;
                if (std::abs(otherCenterX - refCenterX) > 800.0f) continue;

                float dLo = std::abs(candidateVal - ext.lo.y);
                if (dLo < bestColDist) { bestColDist = dLo; bestColSnap = ext.lo.y; colFound = true; }
                float dHi = std::abs(candidateVal - ext.hi.y);
                if (dHi < bestColDist) { bestColDist = dHi; bestColSnap = ext.hi.y; colFound = true; }
            }
        }

        if (colFound) {
            return bestColSnap;
        }
    }

    return SnapValue(candidateVal);
}

bool EditorScene::SnapToAreaCorner(Vector3& pos, float tolerance) {
    if (!m_nav || !m_nav->IsLoaded()) return false;
    float bestDistSq = tolerance * tolerance;
    Vector3 bestCorner;
    bool found = false;

    for (const NavArea* a : m_nav->GetAreas()) {
        if (!a) continue;
        const NavExtent& ext = a->GetExtent();
        if (pos.x < ext.lo.x - tolerance || pos.x > ext.hi.x + tolerance ||
            pos.y < ext.lo.y - tolerance || pos.y > ext.hi.y + tolerance) {
            continue;
        }

        Vector3 corners[4] = {
            Vector3(ext.lo.x, ext.lo.y, ext.lo.z),
            Vector3(ext.hi.x, ext.lo.y, a->GetNEZ()),
            Vector3(ext.hi.x, ext.hi.y, ext.hi.z),
            Vector3(ext.lo.x, ext.hi.y, a->GetSWZ())
        };

        for (int i = 0; i < 4; ++i) {
            float dx = pos.x - corners[i].x;
            float dy = pos.y - corners[i].y;
            float dz = pos.z - corners[i].z;
            float d2 = dx * dx + dy * dy;
            if (d2 <= bestDistSq && std::abs(dz) <= 32.0f) {
                bestDistSq = d2;
                bestCorner = corners[i];
                found = true;
            }
        }
    }

    if (found) {
        pos = bestCorner;
        return true;
    }
    return false;
}

void EditorScene::SnapSelectedAreaToNeighbors(CommandManager& cmdMgr) {
    if (m_selectedAreaIds.size() > 1) {
        BatchSnapToNeighbors(cmdMgr);
        return;
    }
    NavArea* area = GetSelectedArea();
    if (!area || !m_nav || !m_nav->IsLoaded()) return;
    cmdMgr.ExecuteCommand(std::make_unique<CmdSnapAreaToNeighbors>(this, area->GetID(), m_meshSnapTolerance * 1.5f));
}

void EditorScene::StartBridgeMode() {
    m_isBridgeMode = true;
    m_bridgeFirstAreaId = 0;
    m_bridgeFirstEdge = HANDLE_NONE;
    m_bridgeHoverAreaId = 0;
    m_bridgeHoverEdge = HANDLE_NONE;

    NavArea* sel = GetSelectedArea();
    if (sel && m_selectedHandle >= HANDLE_EDGE_NORTH && m_selectedHandle <= HANDLE_EDGE_WEST) {
        m_bridgeFirstAreaId = sel->GetID();
        m_bridgeFirstEdge = m_selectedHandle;
    }
}

void EditorScene::CancelBridgeMode() {
    m_isBridgeMode = false;
    m_bridgeFirstAreaId = 0;
    m_bridgeFirstEdge = HANDLE_NONE;
    m_bridgeHoverAreaId = 0;
    m_bridgeHoverEdge = HANDLE_NONE;
}

void EditorScene::ToggleBridgeMode() {
    if (m_isBridgeMode) {
        CancelBridgeMode();
    } else {
        StartBridgeMode();
    }
}

void EditorScene::SetBridgeHoverEdge(uint32_t areaId, SelectedHandleType edge) {
    m_bridgeHoverAreaId = areaId;
    m_bridgeHoverEdge = edge;
}

void EditorScene::OnBridgeClick(uint32_t areaId, SelectedHandleType edge, CommandManager& cmdMgr) {
    if (!m_isBridgeMode || areaId == 0 || edge == HANDLE_NONE) return;

    if (m_bridgeFirstAreaId == 0 || m_bridgeFirstEdge == HANDLE_NONE) {
        m_bridgeFirstAreaId = areaId;
        m_bridgeFirstEdge = edge;
    } else {
        if (m_bridgeFirstAreaId == areaId) {
            m_bridgeFirstEdge = edge;
            return;
        }

        cmdMgr.ExecuteCommand(std::make_unique<CmdBridgeEdges>(
            this, m_bridgeFirstAreaId, m_bridgeFirstEdge, areaId, edge
        ));

        CancelBridgeMode();
    }
}

bool EditorScene::LoadBSP(const std::string& bspPath) {
    auto newBsp = std::make_unique<BSPFile>();
    if (!newBsp->Load(bspPath)) {
        std::fprintf(stderr, "[EditorScene] Failed to load BSP: %s\n", bspPath.c_str());
        return false;
    }

    m_bsp = std::move(newBsp);
    m_bspPath = bspPath;
    m_textureManager.LoadForBSP(*m_bsp, bspPath, m_gameDirectory);
    m_bspRenderer.BuildFromBSP(*m_bsp, &m_textureManager);
    m_entityRenderer.BuildFromBSP(*m_bsp);
    m_selectedEntityIndex = -1;
    AddRecentFile(bspPath);

    // Auto-detect corresponding .nav file in the same directory
    std::string candidateNav = bspPath;
    size_t dotPos = candidateNav.find_last_of('.');
    if (dotPos != std::string::npos) {
        candidateNav = candidateNav.substr(0, dotPos) + ".nav";
        LoadNAV(candidateNav);
    }

    return true;
}

bool EditorScene::LoadNAV(const std::string& navPath) {
    auto newNav = std::make_unique<NavMesh>();
    if (!newNav->Load(navPath)) {
        std::fprintf(stderr, "[EditorScene] Failed to load NAV: %s\n", navPath.c_str());
        return false;
    }

    m_nav = std::move(newNav);
    m_navPath = navPath;
    m_selectedAreaId = 0;
    m_hoveredAreaId = 0;
    m_isModified = false;
    AddRecentFile(navPath);
    RebuildNavRenderer();
    return true;
}

bool EditorScene::SaveNAV(const std::string& navPath) {
    std::string path = navPath.empty() ? m_navPath : navPath;
    if (path.empty() && !m_bspPath.empty()) {
        size_t dotPos = m_bspPath.find_last_of('.');
        if (dotPos != std::string::npos) {
            path = m_bspPath.substr(0, dotPos) + ".nav";
        } else {
            path = m_bspPath + ".nav";
        }
    }
    if (path.empty()) {
        std::fprintf(stderr, "[EditorScene] Cannot save NAV: no target path specified\n");
        return false;
    }

    if (!m_nav || !m_nav->Save(path)) {
        std::fprintf(stderr, "[EditorScene] Failed to save NAV to %s\n", path.c_str());
        return false;
    }

    m_navPath = path;
    m_isModified = false;
    return true;
}

void EditorScene::UpdateAutosave(float deltaTime) {
    if (!m_prefs.enableAutosave || m_prefs.autosaveIntervalMinutes <= 0) return;
    if (!m_isModified || !m_nav || !m_nav->IsLoaded() || m_nav->GetAreaCount() == 0) {
        m_autosaveTimer = 0.0f;
        return;
    }

    m_autosaveTimer += deltaTime;
    float intervalSec = static_cast<float>(m_prefs.autosaveIntervalMinutes) * 60.0f;
    if (m_autosaveTimer >= intervalSec) {
        m_autosaveTimer = 0.0f;
        AutosaveNAV();
    }
}

bool EditorScene::AutosaveNAV() {
    if (!m_nav || !m_nav->IsLoaded() || m_nav->GetAreaCount() == 0) return false;

    std::string backupPath = m_navPath;
    if (!backupPath.empty()) {
        backupPath += ".bak";
    } else if (!m_bspPath.empty()) {
        size_t dotPos = m_bspPath.find_last_of('.');
        backupPath = (dotPos != std::string::npos ? m_bspPath.substr(0, dotPos) : m_bspPath) + ".nav.bak";
    } else {
        backupPath = "navstudio_autosave.nav.bak";
    }

    if (m_nav->Save(backupPath)) {
        m_lastAutosavePath = backupPath;
        std::printf("[NavStudio] Autosaved backup snapshot to: %s\n", backupPath.c_str());
        return true;
    }
    return false;
}

bool EditorScene::GenerateNavMesh(const NavGenerateOptions& options) {
    if (!m_bsp || !m_bsp->IsLoaded()) {
        std::fprintf(stderr, "[EditorScene] Cannot generate NavMesh: no BSP loaded\n");
        return false;
    }

    auto newNav = std::make_unique<NavMesh>();
    NavGenerateResult res = NavGenerator::Generate(*m_bsp, *newNav, options, nullptr);
    if (!res.success) {
        std::fprintf(stderr, "[EditorScene] Generation failed: %s\n", res.errorMessage.c_str());
        return false;
    }

    m_nav = std::move(newNav);
    if (m_navPath.empty() && !m_bspPath.empty()) {
        size_t dotPos = m_bspPath.find_last_of('.');
        if (dotPos != std::string::npos) {
            m_navPath = m_bspPath.substr(0, dotPos) + ".nav";
        }
    }
    m_selectedAreaId = 0;
    m_hoveredAreaId = 0;
    m_isModified = true;
    PostGenerateOptimize();
    RebuildNavRenderer();
    return true;
}

void EditorScene::PostGenerateOptimize() {
    if (!m_nav || !m_nav->IsLoaded()) return;

    bool mergedAny;
    do {
        mergedAny = false;
        std::vector<NavArea*> areas = m_nav->GetAreas();
        for (size_t i = 0; i < areas.size() && !mergedAny; ++i) {
            NavArea* a = areas[i];
            if (!a) continue;
            for (size_t j = i + 1; j < areas.size() && !mergedAny; ++j) {
                NavArea* b = areas[j];
                if (!b) continue;
                if (a->GetAttributes() != b->GetAttributes()) continue;
                if (a->GetPlace() != b->GetPlace() || a->GetPlaceName() != b->GetPlaceName()) continue;

                NavArea* first = a;
                NavArea* second = b;
                bool isXMerge = false;
                bool isYMerge = false;

                // Horizontal (along X): first is west of second
                if (first->GetExtent().lo.x > second->GetExtent().lo.x) std::swap(first, second);
                {
                    const NavExtent& ext1 = first->GetExtent();
                    const NavExtent& ext2 = second->GetExtent();
                    if (std::fabs(ext1.hi.x - ext2.lo.x) <= 0.25f &&
                        std::fabs(ext1.lo.y - ext2.lo.y) <= 0.25f &&
                        std::fabs(ext1.hi.y - ext2.hi.y) <= 0.25f) {
                        float w1 = ext1.hi.x - ext1.lo.x;
                        float w2 = ext2.hi.x - ext2.lo.x;
                        float wTot = w1 + w2;
                        if (wTot > 0.001f) {
                            float r = w1 / wTot;
                            float expN = ext1.lo.z + (second->GetNEZ() - ext1.lo.z) * r;
                            float expS = first->GetSWZ() + (ext2.hi.z - first->GetSWZ()) * r;
                            if (std::fabs(first->GetNEZ() - expN) <= 2.5f &&
                                std::fabs(ext1.hi.z - expS) <= 2.5f) {
                                isXMerge = true;
                            }
                        }
                    }
                }

                if (!isXMerge) {
                    // Vertical (along Y): first is north of second
                    first = a; second = b;
                    if (first->GetExtent().lo.y > second->GetExtent().lo.y) std::swap(first, second);
                    const NavExtent& yExt1 = first->GetExtent();
                    const NavExtent& yExt2 = second->GetExtent();
                    if (std::fabs(yExt1.hi.y - yExt2.lo.y) <= 0.25f &&
                        std::fabs(yExt1.lo.x - yExt2.lo.x) <= 0.25f &&
                        std::fabs(yExt1.hi.x - yExt2.hi.x) <= 0.25f) {
                        float l1 = yExt1.hi.y - yExt1.lo.y;
                        float l2 = yExt2.hi.y - yExt2.lo.y;
                        float lTot = l1 + l2;
                        if (lTot > 0.001f) {
                            float r = l1 / lTot;
                            float expW = yExt1.lo.z + (second->GetSWZ() - yExt1.lo.z) * r;
                            float expE = first->GetNEZ() + (yExt2.hi.z - first->GetNEZ()) * r;
                            if (std::fabs(first->GetSWZ() - expW) <= 2.5f &&
                                std::fabs(yExt1.hi.z - expE) <= 2.5f) {
                                isYMerge = true;
                            }
                        }
                    }
                }

                if (isXMerge || isYMerge) {
                    NavExtent merged = first->GetExtent();
                    if (isXMerge) {
                        merged.hi.x = second->GetExtent().hi.x;
                        merged.hi.y = second->GetExtent().hi.y;
                        merged.hi.z = second->GetExtent().hi.z;
                    } else {
                        merged.hi.x = second->GetExtent().hi.x;
                        merged.hi.y = second->GetExtent().hi.y;
                        merged.hi.z = second->GetExtent().hi.z;
                    }
                    float mergedNeZ = isXMerge ? second->GetNEZ() : first->GetNEZ();
                    float mergedSwZ = isYMerge ? second->GetSWZ() : first->GetSWZ();

                    uint32_t keepId  = first->GetID();
                    uint32_t removeId = second->GetID();

                    // Transfer second's connections to first
                    for (int d = 0; d < 4; ++d) {
                        for (const auto& conn : second->GetAdjacentList(static_cast<NavDirType>(d))) {
                            if (conn.area && conn.area->GetID() != keepId && conn.area->GetID() != removeId) {
                                m_nav->ConnectAreas(keepId, conn.area->GetID(), false, static_cast<NavDirType>(d));
                            }
                        }
                    }
                    for (NavArea* other : m_nav->GetAreas()) {
                        if (!other || other->GetID() == removeId || other->GetID() == keepId) continue;
                        for (int d = 0; d < 4; ++d) {
                            if (other->IsConnected(second, d)) {
                                m_nav->ConnectAreas(other->GetID(), keepId, false, static_cast<NavDirType>(d));
                            }
                        }
                    }

                    m_nav->GetGrid().RemoveArea(first);
                    first->SetExtent(merged);
                    first->SetCornerHeights(mergedNeZ, mergedSwZ);
                    m_nav->GetGrid().AddArea(first);
                    m_nav->RemoveArea(removeId);
                    mergedAny = true;
                }
            }
        }
    } while (mergedAny);
}

void EditorScene::UnloadNAV() {
    ClearSelection();
    if (m_isBridgeMode) CancelBridgeMode();
    if (m_isDrawAreaMode) CancelDrawArea();
    if (m_isFillAreaMode) CancelFillAreaMode();
    if (m_transformMode != TRANSFORM_NONE) CancelTransform();
    if (m_isDraggingHandle) m_isDraggingHandle = false;

    if (m_nav) {
        m_nav->Unload();
    }
    m_navPath.clear();
    m_selectedAreaId = 0;
    m_hoveredAreaId = 0;
    m_selectedAreaIds.clear();
    m_navRenderer.Clear();
    m_isModified = false;
}

void EditorScene::UnloadBSP() {
    UnloadNAV();

    if (m_bsp) {
        m_bsp->Unload();
    }
    m_bspPath.clear();
    m_bspRenderer.Clear();
    m_skyboxRenderer.Clear();
    m_entityRenderer.Clear();
    m_selectedEntityIndex = -1;
}

void EditorScene::StartAsyncLoad(const std::string& bspOrNavPath, const std::string& explicitNavPath) {
    if (m_loadThread.joinable()) {
        m_loadThread.join();
    }

    m_errorMessage.clear();

    m_loadCtx.inProgress = true;
    m_loadCtx.finished = false;
    m_loadCtx.success = false;
    m_loadCtx.progress = 0.05f;
    m_loadCtx.displayProgress = 0.05f;
    m_loadCtx.minDisplayTimer = 0.45f;
    m_loadCtx.targetBspPath.clear();
    m_loadCtx.targetNavPath.clear();
    m_loadCtx.errorMessage.clear();
    m_loadCtx.loadedBsp.reset();
    m_loadCtx.loadedNav.reset();

    std::string filename = bspOrNavPath;
    size_t lastSlash = filename.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        filename = filename.substr(lastSlash + 1);
    }
    m_loadCtx.filename = filename;
    m_loadCtx.statusText = "Initializing...";

    m_loadThread = std::thread([this, bspOrNavPath, explicitNavPath]() {
        std::string lowerPath = bspOrNavPath;
        std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        bool isBsp = (lowerPath.length() >= 4 && lowerPath.compare(lowerPath.length() - 4, 4, ".bsp") == 0);
        bool isNav = (lowerPath.length() >= 4 && lowerPath.compare(lowerPath.length() - 4, 4, ".nav") == 0);

        if (isBsp) {
            {
                std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                m_loadCtx.statusText = "Reading BSP header and lumps...";
                m_loadCtx.progress = 0.20f;
            }

            auto newBsp = std::make_unique<BSPFile>();
            if (!newBsp->Load(bspOrNavPath)) {
                std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                m_loadCtx.errorMessage = "Failed to load BSP map: " + bspOrNavPath;
                m_loadCtx.finished = true;
                m_loadCtx.success = false;
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                m_loadCtx.statusText = "Parsing planes, faces, and " + std::to_string(newBsp->GetEntityCount()) + " entities...";
                m_loadCtx.progress = 0.55f;
                m_loadCtx.loadedBsp = std::move(newBsp);
                m_loadCtx.targetBspPath = bspOrNavPath;
            }

            // Check for matching .nav file
            std::string navPath = explicitNavPath;
            if (navPath.empty()) {
                size_t dotPos = bspOrNavPath.find_last_of('.');
                if (dotPos != std::string::npos) {
                    navPath = bspOrNavPath.substr(0, dotPos) + ".nav";
                }
            }

            if (!navPath.empty()) {
                std::ifstream testFile(navPath.c_str(), std::ios::binary);
                if (testFile.good()) {
                    testFile.close();
                    {
                        std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                        m_loadCtx.statusText = "Loading matching navigation mesh (.nav)...";
                        m_loadCtx.progress = 0.80f;
                    }

                    auto newNav = std::make_unique<NavMesh>();
                    if (newNav->Load(navPath)) {
                        std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                        m_loadCtx.statusText = "Loaded " + std::to_string(newNav->GetAreaCount()) + " navigation areas...";
                        m_loadCtx.loadedNav = std::move(newNav);
                        m_loadCtx.targetNavPath = navPath;
                    }
                }
            }

            {
                std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                m_loadCtx.statusText = "Building GPU mesh buffers...";
                m_loadCtx.progress = 1.0f;
                m_loadCtx.success = true;
                m_loadCtx.finished = true;
            }
        } else if (isNav) {
            {
                std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                m_loadCtx.statusText = "Reading navigation mesh header...";
                m_loadCtx.progress = 0.25f;
            }

            auto newNav = std::make_unique<NavMesh>();
            if (!newNav->Load(bspOrNavPath)) {
                std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                m_loadCtx.errorMessage = "Failed to load NAV file: " + bspOrNavPath;
                m_loadCtx.finished = true;
                m_loadCtx.success = false;
                return;
            }

            {
                std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                m_loadCtx.statusText = "Parsing " + std::to_string(newNav->GetAreaCount()) + " navigation areas...";
                m_loadCtx.progress = 0.85f;
                m_loadCtx.loadedNav = std::move(newNav);
                m_loadCtx.targetNavPath = bspOrNavPath;
                m_loadCtx.progress = 1.0f;
                m_loadCtx.success = true;
                m_loadCtx.finished = true;
            }
        } else {
            // Unrecognized extension: attempt BSP first, then NAV
            auto newBsp = std::make_unique<BSPFile>();
            if (newBsp->Load(bspOrNavPath)) {
                std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                m_loadCtx.loadedBsp = std::move(newBsp);
                m_loadCtx.targetBspPath = bspOrNavPath;
                m_loadCtx.progress = 1.0f;
                m_loadCtx.success = true;
                m_loadCtx.finished = true;
            } else {
                auto newNav = std::make_unique<NavMesh>();
                if (newNav->Load(bspOrNavPath)) {
                    std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                    m_loadCtx.loadedNav = std::move(newNav);
                    m_loadCtx.targetNavPath = bspOrNavPath;
                    m_loadCtx.progress = 1.0f;
                    m_loadCtx.success = true;
                    m_loadCtx.finished = true;
                } else {
                    std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
                    m_loadCtx.errorMessage = "Unrecognized file format or corrupted file: " + bspOrNavPath;
                    m_loadCtx.finished = true;
                    m_loadCtx.success = false;
                }
            }
        }
    });
}

void EditorScene::UpdateAsyncLoading(float deltaTime) {
    if (!m_loadCtx.inProgress.load()) {
        return;
    }

    if (m_loadCtx.minDisplayTimer > 0.0f) {
        m_loadCtx.minDisplayTimer -= deltaTime;
    }

    float targetProgress = m_loadCtx.progress.load();
    m_loadCtx.displayProgress += (targetProgress - m_loadCtx.displayProgress) * std::min(1.0f, deltaTime * 12.0f);
    if (m_loadCtx.displayProgress < targetProgress * 0.3f) {
        m_loadCtx.displayProgress = targetProgress * 0.3f;
    }

    if (m_loadCtx.finished.load() && m_loadCtx.minDisplayTimer <= 0.0f) {
        if (m_loadThread.joinable()) {
            m_loadThread.join();
        }

        std::lock_guard<std::mutex> lock(m_loadCtx.mutex);
        if (m_loadCtx.success.load()) {
            if (m_loadCtx.loadedBsp) {
                m_bsp = std::move(m_loadCtx.loadedBsp);
                m_bspPath = m_loadCtx.targetBspPath;
                m_textureManager.LoadForBSP(*m_bsp, m_bspPath, m_gameDirectory);
                m_skyboxRenderer.LoadFromBSP(*m_bsp, m_bspPath, m_gameDirectory);
                m_bspRenderer.BuildFromBSP(*m_bsp, &m_textureManager);
                m_entityRenderer.BuildFromBSP(*m_bsp);
                m_selectedEntityIndex = -1;
                AddRecentFile(m_bspPath);

                if (m_navPath.empty()) {
                    size_t dotPos = m_bspPath.find_last_of('.');
                    if (dotPos != std::string::npos) {
                        m_navPath = m_bspPath.substr(0, dotPos) + ".nav";
                    } else {
                        m_navPath = m_bspPath + ".nav";
                    }
                }
            }

            if (m_loadCtx.loadedNav) {
                m_nav = std::move(m_loadCtx.loadedNav);
                m_navPath = m_loadCtx.targetNavPath;
                m_selectedAreaId = 0;
                m_hoveredAreaId = 0;
                AddRecentFile(m_navPath);
                RebuildNavRenderer();
            }

            m_errorMessage.clear();
        } else {
            m_errorMessage = m_loadCtx.errorMessage;
        }

        m_loadCtx.displayProgress = 1.0f;
        m_loadCtx.inProgress = false;
    }
}

void EditorScene::SelectArea(uint32_t id, bool additive, bool toggle) {
    if (additive) {
        if (id != 0) {
            auto it = std::find(m_selectedAreaIds.begin(), m_selectedAreaIds.end(), id);
            if (it == m_selectedAreaIds.end()) {
                m_selectedAreaIds.push_back(id);
            }
            m_selectedAreaId = id;
        }
    } else if (toggle) {
        if (id != 0) {
            auto it = std::find(m_selectedAreaIds.begin(), m_selectedAreaIds.end(), id);
            if (it != m_selectedAreaIds.end()) {
                m_selectedAreaIds.erase(it);
                m_selectedAreaId = m_selectedAreaIds.empty() ? 0 : m_selectedAreaIds.back();
            } else {
                m_selectedAreaIds.push_back(id);
                m_selectedAreaId = id;
            }
        }
    } else {
        m_selectedAreaIds.clear();
        if (id != 0) {
            m_selectedAreaIds.push_back(id);
        }
        m_selectedAreaId = id;
    }

    m_selectedHandle = HANDLE_NONE;
    m_hoveredHandle = HANDLE_NONE;
    RebuildNavRenderer();
}

bool EditorScene::IsAreaSelected(uint32_t id) const {
    if (id == 0) return false;
    for (uint32_t selId : m_selectedAreaIds) {
        if (selId == id) return true;
    }
    return (m_selectedAreaId == id);
}

void EditorScene::ClearSelection() {
    m_selectedAreaIds.clear();
    m_selectedAreaId = 0;
    m_selectedHandle = HANDLE_NONE;
    m_hoveredHandle = HANDLE_NONE;
    RebuildNavRenderer();
}

void EditorScene::SelectAllAreas() {
    m_selectedAreaIds.clear();
    if (m_nav && m_nav->IsLoaded()) {
        for (const auto* area : m_nav->GetAreas()) {
            if (area) m_selectedAreaIds.push_back(area->GetID());
        }
    }
    m_selectedAreaId = m_selectedAreaIds.empty() ? 0 : m_selectedAreaIds.front();
    m_selectedHandle = HANDLE_NONE;
    m_hoveredHandle = HANDLE_NONE;
    RebuildNavRenderer();
}

void EditorScene::SetHoveredArea(uint32_t id) {
    if (m_hoveredAreaId == id) return;
    m_hoveredAreaId = id;
    RebuildNavRenderer();
}

void EditorScene::SetHoveredHandle(SelectedHandleType h) {
    if (m_hoveredHandle == h) return;
    m_hoveredHandle = h;
    RebuildNavRenderer();
}

void EditorScene::SetSelectedHandle(SelectedHandleType h) {
    if (m_selectedHandle == h) return;
    m_selectedHandle = h;
    RebuildNavRenderer();
}

NavArea* EditorScene::GetSelectedArea() {
    if (!m_nav || !m_nav->IsLoaded()) return nullptr;
    if (m_selectedAreaId == 0) {
        if (!m_selectedAreaIds.empty()) {
            m_selectedAreaId = m_selectedAreaIds.front();
        } else {
            return nullptr;
        }
    }
    return m_nav->GetAreaByID(m_selectedAreaId);
}

const NavArea* EditorScene::GetSelectedArea() const {
    if (!m_nav || !m_nav->IsLoaded()) return nullptr;
    uint32_t id = m_selectedAreaId;
    if (id == 0 && !m_selectedAreaIds.empty()) {
        id = m_selectedAreaIds.front();
    }
    if (id == 0) return nullptr;
    return m_nav->GetAreaByID(id);
}

void EditorScene::RebuildNavRenderer() {
    if (m_nav && m_nav->IsLoaded()) {
        int axis = static_cast<int>(m_transformAxis);
        m_navRenderer.BuildFromNav(
            *m_nav,
            m_selectedAreaId,
            m_hoveredAreaId,
            (m_transformMode == TRANSFORM_CONNECT) ? m_connectHoverAreaId : 0,
            axis,
            m_hoveredHandle,
            (m_isDraggingHandle ? m_draggedHandle : m_selectedHandle),
            &m_selectedAreaIds
        );
    }
}

static bool IntersectRayWithPlane(const Ray& ray, const Vector3& planePoint, const Vector3& planeNormal, Vector3& outHit) {
    float denom = ray.direction.Dot(planeNormal);
    if (std::abs(denom) < 1e-5f) return false;
    float t = (planePoint - ray.origin).Dot(planeNormal) / denom;
    if (t < 0.0f) return false;
    outHit = ray.origin + ray.direction * t;
    return true;
}

static bool ProjectRayToAxis(const Ray& ray, const Vector3& axisOrigin, const Vector3& axisDir, float& outT) {
    Vector3 U = axisDir.Normalized();
    Vector3 camToAxis = ray.origin - axisOrigin;
    Vector3 planeNorm = camToAxis - U * camToAxis.Dot(U);
    float lenSq = planeNorm.LengthSquared();
    if (lenSq < 1e-4f) {
        if (std::abs(U.z) < 0.9f) {
            planeNorm = Vector3(-U.y, U.x, 0.0f);
        } else {
            planeNorm = Vector3(1.0f, 0.0f, 0.0f);
        }
    } else {
        planeNorm = planeNorm * (1.0f / std::sqrt(lenSq));
    }

    float denom = ray.direction.Dot(planeNorm);
    if (std::abs(denom) < 1e-4f) {
        return false;
    }

    float s = (axisOrigin - ray.origin).Dot(planeNorm) / denom;
    if (s < 0.0f) {
        return false;
    }

    Vector3 hitPoint = ray.origin + ray.direction * s;
    outT = (hitPoint - axisOrigin).Dot(U);
    return true;
}

void EditorScene::Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& view, const Matrix4& proj, const Vector3& camPos) {
    if (m_showBSP && m_bspRenderer.GetShowSkybox() && m_skyboxRenderer.IsEnabled()) {
        m_skyboxRenderer.Render(view, proj);
    }

    Matrix4 mvp = proj * view;
    Render(meshShader, lineShader, mvp, camPos);
}

void EditorScene::Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp, const Vector3& camPos) {
    if (m_showBSP && m_bspRenderer.IsLoaded()) {
        m_bspRenderer.Render(meshShader, lineShader, mvp, m_bspMode, camPos);
    }

    if (m_entityRenderer.IsLoaded()) {
        m_entityRenderer.Render(meshShader, lineShader, mvp, m_selectedEntityIndex, camPos);
    }

    if (m_showNAV && m_navRenderer.IsLoaded()) {
        m_navRenderer.Render(meshShader, lineShader, mvp);
    }

    // 3D Transform Gizmo (Blender / Hammer style cones, rings, boxes)
    SelectedHandleType activeHandle = m_isDraggingHandle ? m_draggedHandle : m_selectedHandle;
    if (m_nav && m_nav->IsLoaded() && (!m_selectedAreaIds.empty() || m_selectedAreaId != 0)) {
        Vector3 center(0.0f, 0.0f, 0.0f);
        if (m_selectedAreaIds.size() > 1) {
            float count = 0.0f;
            for (uint32_t id : m_selectedAreaIds) {
                const NavArea* a = m_nav->GetAreaByID(id);
                if (a) {
                    center += a->GetCenter();
                    count += 1.0f;
                }
            }
            if (count > 0.0f) {
                center *= (1.0f / count);
            }
        } else {
            uint32_t aid = (m_selectedAreaId != 0) ? m_selectedAreaId : m_selectedAreaIds[0];
            const NavArea* sel = m_nav->GetAreaByID(aid);
            if (sel) {
                center = sel->GetCenter();
            }
        }
        center.z += 4.0f;
        m_gizmoRenderer.Render(lineShader, mvp, center, camPos, m_gizmoMode, m_hoveredHandle, activeHandle);
    } else if (m_selectedEntityIndex >= 0 && m_entityRenderer.IsLoaded()) {
        const EditorEntity* selEnt = m_entityRenderer.GetEntity(m_selectedEntityIndex);
        if (selEnt) {
            m_gizmoRenderer.Render(lineShader, mvp, selEnt->origin, camPos, m_gizmoMode, m_hoveredHandle, activeHandle);
        }
    }

    // Selected Connection Visual Highlight
    if (m_selectedConnection.valid() && m_nav && m_nav->IsLoaded()) {
        const NavArea* a1 = m_nav->GetAreaByID(m_selectedConnection.fromId);
        const NavArea* a2 = m_nav->GetAreaByID(m_selectedConnection.toId);
        if (a1 && a2) {
            Vector3 c1 = a1->GetCenter() + Vector3(0.0f, 0.0f, 6.0f);
            Vector3 c2 = a2->GetCenter() + Vector3(0.0f, 0.0f, 6.0f);
            m_gizmoRenderer.RenderLineSegment(lineShader, mvp, c1, c2, 1.0f, 0.88f, 0.10f, 1.0f, 4.5f);
            m_gizmoRenderer.RenderRectMarquee(lineShader, mvp, c1 - Vector3(6, 6, 0), c1 + Vector3(6, 6, 0), 1.0f, 0.88f, 0.10f, 1.0f);
            m_gizmoRenderer.RenderRectMarquee(lineShader, mvp, c2 - Vector3(6, 6, 0), c2 + Vector3(6, 6, 0), 0.15f, 1.0f, 0.60f, 1.0f);
        }
    }

    // Bridge Mode Visual Highlights
    if (m_isBridgeMode && m_nav && m_nav->IsLoaded()) {
        auto GetEdgePts = [](const NavArea* a, SelectedHandleType e, Vector3& p0, Vector3& p1) {
            switch (e) {
                case HANDLE_EDGE_NORTH:
                    p0 = a->GetCorner(NAV_CORNER_NORTH_WEST);
                    p1 = a->GetCorner(NAV_CORNER_NORTH_EAST);
                    break;
                case HANDLE_EDGE_EAST:
                    p0 = a->GetCorner(NAV_CORNER_NORTH_EAST);
                    p1 = a->GetCorner(NAV_CORNER_SOUTH_EAST);
                    break;
                case HANDLE_EDGE_SOUTH:
                    p0 = a->GetCorner(NAV_CORNER_SOUTH_WEST);
                    p1 = a->GetCorner(NAV_CORNER_SOUTH_EAST);
                    break;
                case HANDLE_EDGE_WEST:
                    p0 = a->GetCorner(NAV_CORNER_NORTH_WEST);
                    p1 = a->GetCorner(NAV_CORNER_SOUTH_WEST);
                    break;
                default:
                    p0 = p1 = a->GetCenter();
                    break;
            }
            p0.z += 2.0f;
            p1.z += 2.0f;
        };

        // First selected edge: Glowing Gold
        if (m_bridgeFirstAreaId != 0 && m_bridgeFirstEdge != HANDLE_NONE) {
            const NavArea* a1 = m_nav->GetAreaByID(m_bridgeFirstAreaId);
            if (a1) {
                Vector3 p0, p1;
                GetEdgePts(a1, m_bridgeFirstEdge, p0, p1);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, p0, p1, 1.0f, 0.85f, 0.1f, 1.0f, 4.0f);
            }
        }

        // Hovered edge: Glowing Cyan
        if (m_bridgeHoverAreaId != 0 && m_bridgeHoverEdge != HANDLE_NONE) {
            const NavArea* aH = m_nav->GetAreaByID(m_bridgeHoverAreaId);
            if (aH) {
                Vector3 p0, p1;
                GetEdgePts(aH, m_bridgeHoverEdge, p0, p1);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, p0, p1, 0.2f, 1.0f, 1.0f, 1.0f, 4.0f);

                // Connecting preview guideline from first edge to hovered edge
                if (m_bridgeFirstAreaId != 0 && m_bridgeFirstEdge != HANDLE_NONE) {
                    const NavArea* a1 = m_nav->GetAreaByID(m_bridgeFirstAreaId);
                    if (a1) {
                        Vector3 p1_0, p1_1;
                        GetEdgePts(a1, m_bridgeFirstEdge, p1_0, p1_1);
                        Vector3 mid1 = (p1_0 + p1_1) * 0.5f;
                        Vector3 mid2 = (p0 + p1) * 0.5f;
                        m_gizmoRenderer.RenderLineSegment(lineShader, mvp, mid1, mid2, 1.0f, 0.9f, 0.2f, 0.8f, 2.5f);
                    }
                }
            }
        }
    }

    // Draw Area Mode Visual Marquee Box
    if (m_isDrawAreaMode && m_drawAreaActive) {
        float minX = std::min(m_drawAreaStart.x, m_drawAreaCurrent.x);
        float maxX = std::max(m_drawAreaStart.x, m_drawAreaCurrent.x);
        float minY = std::min(m_drawAreaStart.y, m_drawAreaCurrent.y);
        float maxY = std::max(m_drawAreaStart.y, m_drawAreaCurrent.y);
        m_gizmoRenderer.RenderRectMarquee4(lineShader, mvp, minX, maxX, minY, maxY,
                                          m_drawAreaNwZ, m_drawAreaNeZ, m_drawAreaSeZ, m_drawAreaSwZ,
                                          0.0f, 0.9f, 1.0f, 1.0f);
    }

    if (m_isDrawAreaMode && m_drawAreaSnappedCorner) {
        Vector3 cPos = m_drawAreaCornerPos;
        m_gizmoRenderer.RenderRectMarquee(lineShader, mvp, cPos - Vector3(6, 6, 0), cPos + Vector3(6, 6, 0), 1.0f, 0.95f, 0.1f, 1.0f);
        m_gizmoRenderer.RenderLineSegment(lineShader, mvp, cPos - Vector3(0, 0, 8), cPos + Vector3(0, 0, 8), 1.0f, 0.95f, 0.1f, 1.0f, 3.0f);
    }

    // Knife Tool Mode Slicing Guideline Preview
    if (m_isKnifeMode && m_knifeHoverAreaId != 0 && m_nav && m_nav->IsLoaded()) {
        const NavArea* knifeArea = m_nav->GetAreaByID(m_knifeHoverAreaId);
        if (knifeArea) {
            const NavExtent& ext = knifeArea->GetExtent();
            if (m_knifeAngle == KNIFE_ANGLE_0) {
                Vector3 p0(ext.lo.x, m_knifeSplitCoordY, knifeArea->GetZ(ext.lo.x, m_knifeSplitCoordY) + 1.5f);
                Vector3 p1(ext.hi.x, m_knifeSplitCoordY, knifeArea->GetZ(ext.hi.x, m_knifeSplitCoordY) + 1.5f);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, p0, p1, 1.0f, 0.45f, 0.1f, 1.0f, 4.0f);
            } else if (m_knifeAngle == KNIFE_ANGLE_90) {
                Vector3 p0(m_knifeSplitCoordX, ext.lo.y, knifeArea->GetZ(m_knifeSplitCoordX, ext.lo.y) + 1.5f);
                Vector3 p1(m_knifeSplitCoordX, ext.hi.y, knifeArea->GetZ(m_knifeSplitCoordX, ext.hi.y) + 1.5f);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, p0, p1, 1.0f, 0.45f, 0.1f, 1.0f, 4.0f);
            } else if (m_knifeAngle == KNIFE_ANGLE_45) {
                // Diagonal 45° line from SW to NE
                Vector3 p0(ext.lo.x, ext.hi.y, knifeArea->GetZ(ext.lo.x, ext.hi.y) + 1.5f);
                Vector3 p1(ext.hi.x, ext.lo.y, knifeArea->GetZ(ext.hi.x, ext.lo.y) + 1.5f);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, p0, p1, 1.0f, 0.35f, 0.1f, 1.0f, 4.0f);

                Vector3 h0(ext.lo.x, m_knifeSplitCoordY, knifeArea->GetZ(ext.lo.x, m_knifeSplitCoordY) + 1.0f);
                Vector3 h1(ext.hi.x, m_knifeSplitCoordY, knifeArea->GetZ(ext.hi.x, m_knifeSplitCoordY) + 1.0f);
                Vector3 v0(m_knifeSplitCoordX, ext.lo.y, knifeArea->GetZ(m_knifeSplitCoordX, ext.lo.y) + 1.0f);
                Vector3 v1(m_knifeSplitCoordX, ext.hi.y, knifeArea->GetZ(m_knifeSplitCoordX, ext.hi.y) + 1.0f);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, h0, h1, 1.0f, 0.6f, 0.2f, 0.5f, 2.0f);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, v0, v1, 1.0f, 0.6f, 0.2f, 0.5f, 2.0f);
            } else if (m_knifeAngle == KNIFE_ANGLE_135) {
                // Diagonal 135° line from NW to SE
                Vector3 p0(ext.lo.x, ext.lo.y, knifeArea->GetZ(ext.lo.x, ext.lo.y) + 1.5f);
                Vector3 p1(ext.hi.x, ext.hi.y, knifeArea->GetZ(ext.hi.x, ext.hi.y) + 1.5f);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, p0, p1, 1.0f, 0.35f, 0.1f, 1.0f, 4.0f);

                Vector3 h0(ext.lo.x, m_knifeSplitCoordY, knifeArea->GetZ(ext.lo.x, m_knifeSplitCoordY) + 1.0f);
                Vector3 h1(ext.hi.x, m_knifeSplitCoordY, knifeArea->GetZ(ext.hi.x, m_knifeSplitCoordY) + 1.0f);
                Vector3 v0(m_knifeSplitCoordX, ext.lo.y, knifeArea->GetZ(m_knifeSplitCoordX, ext.lo.y) + 1.0f);
                Vector3 v1(m_knifeSplitCoordX, ext.hi.y, knifeArea->GetZ(m_knifeSplitCoordX, ext.hi.y) + 1.0f);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, h0, h1, 1.0f, 0.6f, 0.2f, 0.5f, 2.0f);
                m_gizmoRenderer.RenderLineSegment(lineShader, mvp, v0, v1, 1.0f, 0.6f, 0.2f, 0.5f, 2.0f);
            }
        }
    }
}

void EditorScene::StartDragHandle(SelectedHandleType handle, float screenX, float screenY, const Ray& ray) {
    NavArea* area = GetSelectedArea();
    EditorEntity* ent = GetSelectedEntity();
    if ((!area && !ent) || handle == HANDLE_NONE) return;

    m_isDraggingHandle = true;
    m_draggedHandle = handle;
    m_selectedHandle = handle;
    m_dragStartScreenX = screenX;
    m_dragStartScreenY = screenY;

    m_multiDragStates.clear();
    if (m_selectedAreaIds.size() > 1 && m_nav && m_nav->IsLoaded()) {
        Vector3 sumCenter(0.0f, 0.0f, 0.0f);
        float count = 0.0f;
        for (uint32_t aid : m_selectedAreaIds) {
            NavArea* a = m_nav->GetAreaByID(aid);
            if (a) {
                MultiDragState s;
                s.areaId = aid;
                s.startExtent = a->GetExtent();
                s.startNeZ = a->GetNEZ();
                s.startSwZ = a->GetSWZ();
                m_multiDragStates.push_back(s);
                sumCenter += a->GetCenter();
                count += 1.0f;
            }
        }
        if (count > 0.0f) {
            m_dragStartCenter = sumCenter * (1.0f / count);
            m_dragStartCenter.z += 4.0f;
        }
        if (area) {
            m_dragStartExtent = area->GetExtent();
            m_dragStartNeZ = area->GetNEZ();
            m_dragStartSwZ = area->GetSWZ();
        }
    } else if (area) {
        m_dragStartExtent = area->GetExtent();
        m_dragStartNeZ = area->GetNEZ();
        m_dragStartSwZ = area->GetSWZ();
        m_dragStartCenter = area->GetCenter();
        m_dragStartCenter.z += 4.0f;
    } else if (ent) {
        m_dragStartCenter = ent->origin;
        m_dragStartEntityOrigin = ent->origin;
        m_dragStartEntityAngles = ent->angles;
        m_dragStartEntityMins = ent->mins;
        m_dragStartEntityMaxs = ent->maxs;
    }

    // Ground plane hit for center or edge dragging
    float centerZ = m_dragStartCenter.z;
    if (std::abs(ray.direction.z) > 1e-4f) {
        float t = (centerZ - ray.origin.z) / ray.direction.z;
        m_dragStartGroundHit = ray.origin + ray.direction * t;
    } else {
        m_dragStartGroundHit = m_dragStartCenter;
    }

    // Axis coordinate along 1D axis for X, Y, Z translation or scaling
    Vector3 axisDir(0.0f, 0.0f, 0.0f);
    if (handle == HANDLE_GIZMO_X || handle == HANDLE_SCALE_X) {
        axisDir = Vector3(1.0f, 0.0f, 0.0f);
    } else if (handle == HANDLE_GIZMO_Y || handle == HANDLE_SCALE_Y) {
        axisDir = Vector3(0.0f, 1.0f, 0.0f);
    } else if (handle == HANDLE_GIZMO_Z || handle == HANDLE_SCALE_Z) {
        axisDir = Vector3(0.0f, 0.0f, 1.0f);
    }

    if (axisDir.LengthSquared() > 0.5f) {
        float tStart = 0.0f;
        if (ProjectRayToAxis(ray, m_dragStartCenter, axisDir, tStart)) {
            m_dragStartAxisT = tStart;
        } else {
            m_dragStartAxisT = 0.0f;
        }
    }

    // Planar translation or scaling plane hit
    if (handle == HANDLE_PLANE_XY || handle == HANDLE_SCALE_PLANE_XY) {
        Vector3 hit;
        if (IntersectRayWithPlane(ray, m_dragStartCenter, Vector3(0.0f, 0.0f, 1.0f), hit)) {
            m_dragStartPlaneHit = hit;
        } else {
            m_dragStartPlaneHit = m_dragStartCenter;
        }
    } else if (handle == HANDLE_PLANE_XZ || handle == HANDLE_SCALE_PLANE_XZ) {
        Vector3 hit;
        if (IntersectRayWithPlane(ray, m_dragStartCenter, Vector3(0.0f, 1.0f, 0.0f), hit)) {
            m_dragStartPlaneHit = hit;
        } else {
            m_dragStartPlaneHit = m_dragStartCenter;
        }
    } else if (handle == HANDLE_PLANE_YZ || handle == HANDLE_SCALE_PLANE_YZ) {
        Vector3 hit;
        if (IntersectRayWithPlane(ray, m_dragStartCenter, Vector3(1.0f, 0.0f, 0.0f), hit)) {
            m_dragStartPlaneHit = hit;
        } else {
            m_dragStartPlaneHit = m_dragStartCenter;
        }
    }

    m_dragStartScaleDist = 1.0f;

    // Angle for rotation handles
    if (handle == HANDLE_ROTATE_Z) {
        if (std::abs(ray.direction.z) > 1e-4f) {
            float t = (m_dragStartCenter.z - ray.origin.z) / ray.direction.z;
            Vector3 hit = ray.origin + ray.direction * t;
            m_dragStartAngle = std::atan2(hit.y - m_dragStartCenter.y, hit.x - m_dragStartCenter.x);
        } else {
            m_dragStartAngle = 0.0f;
        }
    } else if (handle == HANDLE_ROTATE_X) {
        if (std::abs(ray.direction.x) > 1e-4f) {
            float t = (m_dragStartCenter.x - ray.origin.x) / ray.direction.x;
            Vector3 hit = ray.origin + ray.direction * t;
            m_dragStartAngle = std::atan2(hit.z - m_dragStartCenter.z, hit.y - m_dragStartCenter.y);
        } else {
            m_dragStartAngle = 0.0f;
        }
    } else if (handle == HANDLE_ROTATE_Y) {
        if (std::abs(ray.direction.y) > 1e-4f) {
            float t = (m_dragStartCenter.y - ray.origin.y) / ray.direction.y;
            Vector3 hit = ray.origin + ray.direction * t;
            m_dragStartAngle = std::atan2(hit.z - m_dragStartCenter.z, hit.x - m_dragStartCenter.x);
        } else {
            m_dragStartAngle = 0.0f;
        }
    } else if (handle == HANDLE_ROTATE_SCREEN) {
        m_dragStartAngle = std::atan2(screenY - m_dragStartScreenY, screenX - m_dragStartScreenX);
    }

    RebuildNavRenderer();
}

void EditorScene::UpdateDragHandle(float screenX, float screenY, const Ray& ray, float /*deltaY*/) {
    NavArea* area = GetSelectedArea();
    EditorEntity* ent = GetSelectedEntity();
    if ((!area && !ent) || !m_isDraggingHandle) return;

    Vector3 curGroundHit = m_dragStartGroundHit;
    float centerZ = m_dragStartCenter.z;
    if (std::abs(ray.direction.z) > 1e-4f) {
        float t = (centerZ - ray.origin.z) / ray.direction.z;
        curGroundHit = ray.origin + ray.direction * t;
    }

    Vector3 groundDelta = curGroundHit - m_dragStartGroundHit;

    if (m_draggedHandle == HANDLE_GIZMO_X) {
        float tCur = 0.0f;
        if (ProjectRayToAxis(ray, m_dragStartCenter, Vector3(1.0f, 0.0f, 0.0f), tCur)) {
            float deltaX = tCur - m_dragStartAxisT;
            if (m_multiDragStates.size() > 1 && m_nav && m_nav->IsLoaded()) {
                float shiftX = SnapValue(deltaX);
                for (const auto& s : m_multiDragStates) {
                    NavArea* a = m_nav->GetAreaByID(s.areaId);
                    if (!a) continue;
                    NavExtent nExt = s.startExtent;
                    nExt.lo.x += shiftX; nExt.hi.x += shiftX;
                    m_nav->GetGrid().RemoveArea(a);
                    a->SetExtent(nExt);
                    m_nav->GetGrid().AddArea(a);
                }
            } else if (area) {
                float origCenterX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
                float targetCenterX = SnapValue(origCenterX + deltaX);
                float shiftX = targetCenterX - origCenterX;
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.x += shiftX;
                nextExt.hi.x += shiftX;
                area->SetExtent(nextExt);
            } else if (ent) {
                float targetX = SnapValue(m_dragStartEntityOrigin.x + deltaX);
                ent->origin.x = targetX;
                ent->worldMins.x = ent->origin.x + ent->mins.x;
                ent->worldMaxs.x = ent->origin.x + ent->maxs.x;
            }
        }
    } else if (m_draggedHandle == HANDLE_GIZMO_Y) {
        float tCur = 0.0f;
        if (ProjectRayToAxis(ray, m_dragStartCenter, Vector3(0.0f, 1.0f, 0.0f), tCur)) {
            float deltaYAxis = tCur - m_dragStartAxisT;
            if (m_multiDragStates.size() > 1 && m_nav && m_nav->IsLoaded()) {
                float shiftY = SnapValue(deltaYAxis);
                for (const auto& s : m_multiDragStates) {
                    NavArea* a = m_nav->GetAreaByID(s.areaId);
                    if (!a) continue;
                    NavExtent nExt = s.startExtent;
                    nExt.lo.y += shiftY; nExt.hi.y += shiftY;
                    m_nav->GetGrid().RemoveArea(a);
                    a->SetExtent(nExt);
                    m_nav->GetGrid().AddArea(a);
                }
            } else if (area) {
                float origCenterY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
                float targetCenterY = SnapValue(origCenterY + deltaYAxis);
                float shiftY = targetCenterY - origCenterY;
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.y += shiftY;
                nextExt.hi.y += shiftY;
                area->SetExtent(nextExt);
            } else if (ent) {
                float targetY = SnapValue(m_dragStartEntityOrigin.y + deltaYAxis);
                ent->origin.y = targetY;
                ent->worldMins.y = ent->origin.y + ent->mins.y;
                ent->worldMaxs.y = ent->origin.y + ent->maxs.y;
            }
        }
    } else if (m_draggedHandle == HANDLE_GIZMO_Z) {
        float tCur = 0.0f;
        if (ProjectRayToAxis(ray, m_dragStartCenter, Vector3(0.0f, 0.0f, 1.0f), tCur)) {
            float deltaZ = tCur - m_dragStartAxisT;
            if (m_multiDragStates.size() > 1 && m_nav && m_nav->IsLoaded()) {
                float shiftZ = SnapValue(deltaZ);
                for (const auto& s : m_multiDragStates) {
                    NavArea* a = m_nav->GetAreaByID(s.areaId);
                    if (!a) continue;
                    NavExtent nExt = s.startExtent;
                    nExt.lo.z += shiftZ; nExt.hi.z += shiftZ;
                    m_nav->GetGrid().RemoveArea(a);
                    a->SetExtent(nExt);
                    a->SetCornerHeights(s.startNeZ + shiftZ, s.startSwZ + shiftZ);
                    m_nav->GetGrid().AddArea(a);
                }
            } else if (area) {
                float origZ = (m_dragStartExtent.lo.z + m_dragStartExtent.hi.z + m_dragStartNeZ + m_dragStartSwZ) * 0.25f;
                float targetZ = SnapValue(origZ + deltaZ);
                float shiftZ = targetZ - origZ;
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.z += shiftZ;
                nextExt.hi.z += shiftZ;
                area->SetExtent(nextExt);
                area->SetCornerHeights(m_dragStartNeZ + shiftZ, m_dragStartSwZ + shiftZ);
            } else if (ent) {
                float targetZ = SnapValue(m_dragStartEntityOrigin.z + deltaZ);
                ent->origin.z = targetZ;
                ent->worldMins.z = ent->origin.z + ent->mins.z;
                ent->worldMaxs.z = ent->origin.z + ent->maxs.z;
            }
        }
    } else if (m_draggedHandle == HANDLE_GIZMO_CENTER) {
        if (m_multiDragStates.size() > 1 && m_nav && m_nav->IsLoaded()) {
            float shiftX = SnapValue(groundDelta.x);
            float shiftY = SnapValue(groundDelta.y);
            for (const auto& s : m_multiDragStates) {
                NavArea* a = m_nav->GetAreaByID(s.areaId);
                if (!a) continue;
                NavExtent nExt = s.startExtent;
                nExt.lo.x += shiftX; nExt.hi.x += shiftX;
                nExt.lo.y += shiftY; nExt.hi.y += shiftY;
                m_nav->GetGrid().RemoveArea(a);
                a->SetExtent(nExt);
                m_nav->GetGrid().AddArea(a);
            }
        } else if (area) {
            float origCenterX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
            float origCenterY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
            float targetCenterX = SnapValue(origCenterX + groundDelta.x);
            float targetCenterY = SnapValue(origCenterY + groundDelta.y);
            float shiftX = targetCenterX - origCenterX;
            float shiftY = targetCenterY - origCenterY;
            NavExtent nextExt = m_dragStartExtent;
            nextExt.lo.x += shiftX; nextExt.hi.x += shiftX;
            nextExt.lo.y += shiftY; nextExt.hi.y += shiftY;
            area->SetExtent(nextExt);
        } else if (ent) {
            ent->origin.x = SnapValue(m_dragStartEntityOrigin.x + groundDelta.x);
            ent->origin.y = SnapValue(m_dragStartEntityOrigin.y + groundDelta.y);
            ent->worldMins.x = ent->origin.x + ent->mins.x;
            ent->worldMaxs.x = ent->origin.x + ent->maxs.x;
            ent->worldMins.y = ent->origin.y + ent->mins.y;
            ent->worldMaxs.y = ent->origin.y + ent->maxs.y;
        }
    } else if (m_draggedHandle == HANDLE_PLANE_XY) {
        Vector3 curHit = m_dragStartPlaneHit;
        if (IntersectRayWithPlane(ray, m_dragStartCenter, Vector3(0.0f, 0.0f, 1.0f), curHit)) {
            Vector3 delta = curHit - m_dragStartPlaneHit;
            if (m_multiDragStates.size() > 1 && m_nav && m_nav->IsLoaded()) {
                float shiftX = SnapValue(delta.x);
                float shiftY = SnapValue(delta.y);
                for (const auto& s : m_multiDragStates) {
                    NavArea* a = m_nav->GetAreaByID(s.areaId);
                    if (!a) continue;
                    NavExtent nExt = s.startExtent;
                    nExt.lo.x += shiftX; nExt.hi.x += shiftX;
                    nExt.lo.y += shiftY; nExt.hi.y += shiftY;
                    m_nav->GetGrid().RemoveArea(a);
                    a->SetExtent(nExt);
                    m_nav->GetGrid().AddArea(a);
                }
            } else if (area) {
                float origCenterX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
                float origCenterY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
                float targetCenterX = SnapValue(origCenterX + delta.x);
                float targetCenterY = SnapValue(origCenterY + delta.y);
                float shiftX = targetCenterX - origCenterX;
                float shiftY = targetCenterY - origCenterY;
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.x += shiftX; nextExt.hi.x += shiftX;
                nextExt.lo.y += shiftY; nextExt.hi.y += shiftY;
                area->SetExtent(nextExt);
            } else if (ent) {
                ent->origin.x = SnapValue(m_dragStartEntityOrigin.x + delta.x);
                ent->origin.y = SnapValue(m_dragStartEntityOrigin.y + delta.y);
                ent->worldMins.x = ent->origin.x + ent->mins.x;
                ent->worldMaxs.x = ent->origin.x + ent->maxs.x;
                ent->worldMins.y = ent->origin.y + ent->mins.y;
                ent->worldMaxs.y = ent->origin.y + ent->maxs.y;
            }
        }
    } else if (m_draggedHandle == HANDLE_PLANE_XZ) {
        Vector3 curHit = m_dragStartPlaneHit;
        if (IntersectRayWithPlane(ray, m_dragStartCenter, Vector3(0.0f, 1.0f, 0.0f), curHit)) {
            Vector3 delta = curHit - m_dragStartPlaneHit;
            if (m_multiDragStates.size() > 1 && m_nav && m_nav->IsLoaded()) {
                float shiftX = SnapValue(delta.x);
                float shiftZ = SnapValue(delta.z);
                for (const auto& s : m_multiDragStates) {
                    NavArea* a = m_nav->GetAreaByID(s.areaId);
                    if (!a) continue;
                    NavExtent nExt = s.startExtent;
                    nExt.lo.x += shiftX; nExt.hi.x += shiftX;
                    nExt.lo.z += shiftZ; nExt.hi.z += shiftZ;
                    m_nav->GetGrid().RemoveArea(a);
                    a->SetExtent(nExt);
                    a->SetCornerHeights(s.startNeZ + shiftZ, s.startSwZ + shiftZ);
                    m_nav->GetGrid().AddArea(a);
                }
            } else if (area) {
                float origCenterX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
                float origMidZ = (m_dragStartExtent.lo.z + m_dragStartExtent.hi.z + m_dragStartNeZ + m_dragStartSwZ) * 0.25f;
                float targetCenterX = SnapValue(origCenterX + delta.x);
                float targetMidZ = SnapValue(origMidZ + delta.z);
                float shiftX = targetCenterX - origCenterX;
                float shiftZ = targetMidZ - origMidZ;
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.x += shiftX; nextExt.hi.x += shiftX;
                nextExt.lo.z += shiftZ; nextExt.hi.z += shiftZ;
                area->SetExtent(nextExt);
                area->SetCornerHeights(m_dragStartNeZ + shiftZ, m_dragStartSwZ + shiftZ);
            } else if (ent) {
                ent->origin.x = SnapValue(m_dragStartEntityOrigin.x + delta.x);
                ent->origin.z = SnapValue(m_dragStartEntityOrigin.z + delta.z);
                ent->worldMins.x = ent->origin.x + ent->mins.x;
                ent->worldMaxs.x = ent->origin.x + ent->maxs.x;
                ent->worldMins.z = ent->origin.z + ent->mins.z;
                ent->worldMaxs.z = ent->origin.z + ent->maxs.z;
            }
        }
    } else if (m_draggedHandle == HANDLE_PLANE_YZ) {
        Vector3 curHit = m_dragStartPlaneHit;
        if (IntersectRayWithPlane(ray, m_dragStartCenter, Vector3(1.0f, 0.0f, 0.0f), curHit)) {
            Vector3 delta = curHit - m_dragStartPlaneHit;
            if (m_multiDragStates.size() > 1 && m_nav && m_nav->IsLoaded()) {
                float shiftY = SnapValue(delta.y);
                float shiftZ = SnapValue(delta.z);
                for (const auto& s : m_multiDragStates) {
                    NavArea* a = m_nav->GetAreaByID(s.areaId);
                    if (!a) continue;
                    NavExtent nExt = s.startExtent;
                    nExt.lo.y += shiftY; nExt.hi.y += shiftY;
                    nExt.lo.z += shiftZ; nExt.hi.z += shiftZ;
                    m_nav->GetGrid().RemoveArea(a);
                    a->SetExtent(nExt);
                    a->SetCornerHeights(s.startNeZ + shiftZ, s.startSwZ + shiftZ);
                    m_nav->GetGrid().AddArea(a);
                }
            } else if (area) {
                float origCenterY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
                float origMidZ = (m_dragStartExtent.lo.z + m_dragStartExtent.hi.z + m_dragStartNeZ + m_dragStartSwZ) * 0.25f;
                float targetCenterY = SnapValue(origCenterY + delta.y);
                float targetMidZ = SnapValue(origMidZ + delta.z);
                float shiftY = targetCenterY - origCenterY;
                float shiftZ = targetMidZ - origMidZ;
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.y += shiftY; nextExt.hi.y += shiftY;
                nextExt.lo.z += shiftZ; nextExt.hi.z += shiftZ;
                area->SetExtent(nextExt);
                area->SetCornerHeights(m_dragStartNeZ + shiftZ, m_dragStartSwZ + shiftZ);
            } else if (ent) {
                ent->origin.y = SnapValue(m_dragStartEntityOrigin.y + delta.y);
                ent->origin.z = SnapValue(m_dragStartEntityOrigin.z + delta.z);
                ent->worldMins.y = ent->origin.y + ent->mins.y;
                ent->worldMaxs.y = ent->origin.y + ent->maxs.y;
                ent->worldMins.z = ent->origin.z + ent->mins.z;
                ent->worldMaxs.z = ent->origin.z + ent->maxs.z;
            }
        }
    } else if (m_draggedHandle == HANDLE_SCALE_X) {
        float tCur = 0.0f;
        if (ProjectRayToAxis(ray, m_dragStartCenter, Vector3(1.0f, 0.0f, 0.0f), tCur)) {
            float deltaX = tCur - m_dragStartAxisT;
            if (area) {
                float origHalfW = (m_dragStartExtent.hi.x - m_dragStartExtent.lo.x) * 0.5f;
                float centerX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
                float newHalfW = std::max(8.0f, origHalfW + deltaX);
                float targetHiX = SnapValue(centerX + newHalfW);
                float targetLoX = SnapValue(centerX - newHalfW);
                if (targetHiX - targetLoX < 16.0f) targetHiX = targetLoX + 16.0f;
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.x = targetLoX;
                nextExt.hi.x = targetHiX;
                area->SetExtent(nextExt);
            } else if (ent) {
                float origHalfW = (m_dragStartEntityMaxs.x - m_dragStartEntityMins.x) * 0.5f;
                float scale = std::max(0.1f, 1.0f + deltaX / std::max(origHalfW, 8.0f));
                ent->mins.x = m_dragStartEntityMins.x * scale;
                ent->maxs.x = m_dragStartEntityMaxs.x * scale;
                ent->worldMins.x = ent->origin.x + ent->mins.x;
                ent->worldMaxs.x = ent->origin.x + ent->maxs.x;
            }
        }
    } else if (m_draggedHandle == HANDLE_SCALE_Y) {
        float tCur = 0.0f;
        if (ProjectRayToAxis(ray, m_dragStartCenter, Vector3(0.0f, 1.0f, 0.0f), tCur)) {
            float deltaYAxis = tCur - m_dragStartAxisT;
            if (area) {
                float origHalfL = (m_dragStartExtent.hi.y - m_dragStartExtent.lo.y) * 0.5f;
                float centerY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
                float newHalfL = std::max(8.0f, origHalfL + deltaYAxis);
                float targetHiY = SnapValue(centerY + newHalfL);
                float targetLoY = SnapValue(centerY - newHalfL);
                if (targetHiY - targetLoY < 16.0f) targetHiY = targetLoY + 16.0f;
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.y = targetLoY;
                nextExt.hi.y = targetHiY;
                area->SetExtent(nextExt);
            } else if (ent) {
                float origHalfL = (m_dragStartEntityMaxs.y - m_dragStartEntityMins.y) * 0.5f;
                float scale = std::max(0.1f, 1.0f + deltaYAxis / std::max(origHalfL, 8.0f));
                ent->mins.y = m_dragStartEntityMins.y * scale;
                ent->maxs.y = m_dragStartEntityMaxs.y * scale;
                ent->worldMins.y = ent->origin.y + ent->mins.y;
                ent->worldMaxs.y = ent->origin.y + ent->maxs.y;
            }
        }
    } else if (m_draggedHandle == HANDLE_SCALE_Z) {
        float tCur = 0.0f;
        if (ProjectRayToAxis(ray, m_dragStartCenter, Vector3(0.0f, 0.0f, 1.0f), tCur)) {
            float deltaZ = tCur - m_dragStartAxisT;
            if (area) {
                float midZ = (m_dragStartExtent.lo.z + m_dragStartExtent.hi.z + m_dragStartNeZ + m_dragStartSwZ) * 0.25f;
                float nwOff = m_dragStartExtent.lo.z - midZ;
                float seOff = m_dragStartExtent.hi.z - midZ;
                float neOff = m_dragStartNeZ - midZ;
                float swOff = m_dragStartSwZ - midZ;
                float maxSpan = std::max({std::abs(nwOff), std::abs(seOff), std::abs(neOff), std::abs(swOff), 8.0f});
                float scale = std::max(0.0f, 1.0f + deltaZ / maxSpan);
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.z = midZ + nwOff * scale;
                nextExt.hi.z = midZ + seOff * scale;
                area->SetExtent(nextExt);
                area->SetCornerHeights(midZ + neOff * scale, midZ + swOff * scale);
            } else if (ent) {
                float origHalfH = (m_dragStartEntityMaxs.z - m_dragStartEntityMins.z) * 0.5f;
                float scale = std::max(0.1f, 1.0f + deltaZ / std::max(origHalfH, 8.0f));
                ent->mins.z = m_dragStartEntityMins.z * scale;
                ent->maxs.z = m_dragStartEntityMaxs.z * scale;
                ent->worldMins.z = ent->origin.z + ent->mins.z;
                ent->worldMaxs.z = ent->origin.z + ent->maxs.z;
            }
        }
    } else if (m_draggedHandle == HANDLE_SCALE_PLANE_XY) {
        float deltaScreen = (screenX - m_dragStartScreenX) + (m_dragStartScreenY - screenY);
        float scale = std::max(0.05f, 1.0f + deltaScreen / 100.0f);
        if (area) {
            float cX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
            float cY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
            float halfW = (m_dragStartExtent.hi.x - m_dragStartExtent.lo.x) * 0.5f;
            float halfL = (m_dragStartExtent.hi.y - m_dragStartExtent.lo.y) * 0.5f;
            float newHalfW = std::max(8.0f, SnapValue(halfW * scale));
            float newHalfL = std::max(8.0f, SnapValue(halfL * scale));
            NavExtent nextExt = m_dragStartExtent;
            nextExt.lo.x = cX - newHalfW; nextExt.hi.x = cX + newHalfW;
            nextExt.lo.y = cY - newHalfL; nextExt.hi.y = cY + newHalfL;
            area->SetExtent(nextExt);
        } else if (ent) {
            ent->mins.x = m_dragStartEntityMins.x * scale;
            ent->maxs.x = m_dragStartEntityMaxs.x * scale;
            ent->mins.y = m_dragStartEntityMins.y * scale;
            ent->maxs.y = m_dragStartEntityMaxs.y * scale;
            ent->worldMins.x = ent->origin.x + ent->mins.x;
            ent->worldMaxs.x = ent->origin.x + ent->maxs.x;
            ent->worldMins.y = ent->origin.y + ent->mins.y;
            ent->worldMaxs.y = ent->origin.y + ent->maxs.y;
        }
    } else if (m_draggedHandle == HANDLE_SCALE_PLANE_XZ) {
        float deltaScreen = (screenX - m_dragStartScreenX) + (m_dragStartScreenY - screenY);
        float scale = std::max(0.05f, 1.0f + deltaScreen / 100.0f);
        if (area) {
            float cX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
            float halfW = (m_dragStartExtent.hi.x - m_dragStartExtent.lo.x) * 0.5f;
            float newHalfW = std::max(8.0f, SnapValue(halfW * scale));
            float midZ = (m_dragStartExtent.lo.z + m_dragStartExtent.hi.z + m_dragStartNeZ + m_dragStartSwZ) * 0.25f;
            float nwOff = m_dragStartExtent.lo.z - midZ;
            float seOff = m_dragStartExtent.hi.z - midZ;
            float neOff = m_dragStartNeZ - midZ;
            float swOff = m_dragStartSwZ - midZ;
            NavExtent nextExt = m_dragStartExtent;
            nextExt.lo.x = cX - newHalfW; nextExt.hi.x = cX + newHalfW;
            nextExt.lo.z = midZ + nwOff * scale; nextExt.hi.z = midZ + seOff * scale;
            area->SetExtent(nextExt);
            area->SetCornerHeights(midZ + neOff * scale, midZ + swOff * scale);
        } else if (ent) {
            ent->mins.x = m_dragStartEntityMins.x * scale;
            ent->maxs.x = m_dragStartEntityMaxs.x * scale;
            ent->mins.z = m_dragStartEntityMins.z * scale;
            ent->maxs.z = m_dragStartEntityMaxs.z * scale;
            ent->worldMins.x = ent->origin.x + ent->mins.x;
            ent->worldMaxs.x = ent->origin.x + ent->maxs.x;
            ent->worldMins.z = ent->origin.z + ent->mins.z;
            ent->worldMaxs.z = ent->origin.z + ent->maxs.z;
        }
    } else if (m_draggedHandle == HANDLE_SCALE_PLANE_YZ) {
        float deltaScreen = (screenX - m_dragStartScreenX) + (m_dragStartScreenY - screenY);
        float scale = std::max(0.05f, 1.0f + deltaScreen / 100.0f);
        if (area) {
            float cY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
            float halfL = (m_dragStartExtent.hi.y - m_dragStartExtent.lo.y) * 0.5f;
            float newHalfL = std::max(8.0f, SnapValue(halfL * scale));
            float midZ = (m_dragStartExtent.lo.z + m_dragStartExtent.hi.z + m_dragStartNeZ + m_dragStartSwZ) * 0.25f;
            float nwOff = m_dragStartExtent.lo.z - midZ;
            float seOff = m_dragStartExtent.hi.z - midZ;
            float neOff = m_dragStartNeZ - midZ;
            float swOff = m_dragStartSwZ - midZ;
            NavExtent nextExt = m_dragStartExtent;
            nextExt.lo.y = cY - newHalfL; nextExt.hi.y = cY + newHalfL;
            nextExt.lo.z = midZ + nwOff * scale; nextExt.hi.z = midZ + seOff * scale;
            area->SetExtent(nextExt);
            area->SetCornerHeights(midZ + neOff * scale, midZ + swOff * scale);
        } else if (ent) {
            ent->mins.y = m_dragStartEntityMins.y * scale;
            ent->maxs.y = m_dragStartEntityMaxs.y * scale;
            ent->mins.z = m_dragStartEntityMins.z * scale;
            ent->maxs.z = m_dragStartEntityMaxs.z * scale;
            ent->worldMins.y = ent->origin.y + ent->mins.y;
            ent->worldMaxs.y = ent->origin.y + ent->maxs.y;
            ent->worldMins.z = ent->origin.z + ent->mins.z;
            ent->worldMaxs.z = ent->origin.z + ent->maxs.z;
        }
    } else if (m_draggedHandle == HANDLE_SCALE_UNIFORM) {
        float deltaScreen = (screenX - m_dragStartScreenX) + (m_dragStartScreenY - screenY);
        float scale = std::max(0.05f, 1.0f + deltaScreen / 100.0f);
        if (area) {
            float cX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
            float cY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
            float halfW = (m_dragStartExtent.hi.x - m_dragStartExtent.lo.x) * 0.5f;
            float halfL = (m_dragStartExtent.hi.y - m_dragStartExtent.lo.y) * 0.5f;
            float newHalfW = std::max(8.0f, SnapValue(halfW * scale));
            float newHalfL = std::max(8.0f, SnapValue(halfL * scale));
            float midZ = (m_dragStartExtent.lo.z + m_dragStartExtent.hi.z + m_dragStartNeZ + m_dragStartSwZ) * 0.25f;
            float nwOff = m_dragStartExtent.lo.z - midZ;
            float seOff = m_dragStartExtent.hi.z - midZ;
            float neOff = m_dragStartNeZ - midZ;
            float swOff = m_dragStartSwZ - midZ;
            NavExtent nextExt = m_dragStartExtent;
            nextExt.lo.x = cX - newHalfW; nextExt.hi.x = cX + newHalfW;
            nextExt.lo.y = cY - newHalfL; nextExt.hi.y = cY + newHalfL;
            nextExt.lo.z = midZ + nwOff * scale; nextExt.hi.z = midZ + seOff * scale;
            area->SetExtent(nextExt);
            area->SetCornerHeights(midZ + neOff * scale, midZ + swOff * scale);
        } else if (ent) {
            ent->mins.x = m_dragStartEntityMins.x * scale;
            ent->maxs.x = m_dragStartEntityMaxs.x * scale;
            ent->mins.y = m_dragStartEntityMins.y * scale;
            ent->maxs.y = m_dragStartEntityMaxs.y * scale;
            ent->mins.z = m_dragStartEntityMins.z * scale;
            ent->maxs.z = m_dragStartEntityMaxs.z * scale;
            ent->worldMins.x = ent->origin.x + ent->mins.x;
            ent->worldMaxs.x = ent->origin.x + ent->maxs.x;
            ent->worldMins.y = ent->origin.y + ent->mins.y;
            ent->worldMaxs.y = ent->origin.y + ent->maxs.y;
            ent->worldMins.z = ent->origin.z + ent->mins.z;
            ent->worldMaxs.z = ent->origin.z + ent->maxs.z;
        }
    } else if (m_draggedHandle == HANDLE_ROTATE_Z) {
        if (std::abs(ray.direction.z) > 1e-4f) {
            float t = (m_dragStartCenter.z - ray.origin.z) / ray.direction.z;
            Vector3 hit = ray.origin + ray.direction * t;
            float curAng = std::atan2(hit.y - m_dragStartCenter.y, hit.x - m_dragStartCenter.x);
            float deltaRad = curAng - m_dragStartAngle;
            float deltaDeg = deltaRad * (180.0f / 3.1415926535f);
            if (m_gridSnap) {
                deltaDeg = std::round(deltaDeg / 15.0f) * 15.0f;
            }
            if (ent) {
                float newYaw = std::fmod(m_dragStartEntityAngles.y + deltaDeg, 360.0f);
                if (newYaw < 0.0f) newYaw += 360.0f;
                ent->yaw = newYaw;
                ent->angles.y = newYaw;
            } else if (area) {
                int steps = static_cast<int>(std::round(deltaDeg / 90.0f));
                if (std::abs(steps) % 2 == 1) {
                    float cX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
                    float cY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
                    float halfW = (m_dragStartExtent.hi.x - m_dragStartExtent.lo.x) * 0.5f;
                    float halfL = (m_dragStartExtent.hi.y - m_dragStartExtent.lo.y) * 0.5f;
                    NavExtent rotExt = m_dragStartExtent;
                    rotExt.lo.x = cX - halfL; rotExt.hi.x = cX + halfL;
                    rotExt.lo.y = cY - halfW; rotExt.hi.y = cY + halfW;
                    area->SetExtent(rotExt);
                } else {
                    area->SetExtent(m_dragStartExtent);
                }
            }
        }
    } else if (m_draggedHandle == HANDLE_ROTATE_X) {
        if (std::abs(ray.direction.x) > 1e-4f) {
            float t = (m_dragStartCenter.x - ray.origin.x) / ray.direction.x;
            Vector3 hit = ray.origin + ray.direction * t;
            float curAng = std::atan2(hit.z - m_dragStartCenter.z, hit.y - m_dragStartCenter.y);
            float deltaRad = curAng - m_dragStartAngle;
            float deltaDeg = deltaRad * (180.0f / 3.1415926535f);
            if (m_gridSnap) {
                deltaDeg = std::round(deltaDeg / 15.0f) * 15.0f;
            }
            if (ent) {
                float newPitch = std::fmod(m_dragStartEntityAngles.x + deltaDeg, 360.0f);
                if (newPitch < 0.0f) newPitch += 360.0f;
                ent->angles.x = newPitch;
            } else if (area) {
                float midZ = (m_dragStartExtent.lo.z + m_dragStartExtent.hi.z + m_dragStartNeZ + m_dragStartSwZ) * 0.25f;
                float tilt = deltaDeg * 1.5f;
                if (m_gridSnap) tilt = SnapValue(tilt);
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.z = midZ + tilt;
                nextExt.hi.z = midZ - tilt;
                area->SetExtent(nextExt);
                area->SetCornerHeights(midZ + tilt, midZ - tilt);
            }
        }
    } else if (m_draggedHandle == HANDLE_ROTATE_Y) {
        if (std::abs(ray.direction.y) > 1e-4f) {
            float t = (m_dragStartCenter.y - ray.origin.y) / ray.direction.y;
            Vector3 hit = ray.origin + ray.direction * t;
            float curAng = std::atan2(hit.z - m_dragStartCenter.z, hit.x - m_dragStartCenter.x);
            float deltaRad = curAng - m_dragStartAngle;
            float deltaDeg = deltaRad * (180.0f / 3.1415926535f);
            if (m_gridSnap) {
                deltaDeg = std::round(deltaDeg / 15.0f) * 15.0f;
            }
            if (ent) {
                float newRoll = std::fmod(m_dragStartEntityAngles.z + deltaDeg, 360.0f);
                if (newRoll < 0.0f) newRoll += 360.0f;
                ent->angles.z = newRoll;
            } else if (area) {
                float midZ = (m_dragStartExtent.lo.z + m_dragStartExtent.hi.z + m_dragStartNeZ + m_dragStartSwZ) * 0.25f;
                float tilt = deltaDeg * 1.5f;
                if (m_gridSnap) tilt = SnapValue(tilt);
                NavExtent nextExt = m_dragStartExtent;
                nextExt.lo.z = midZ - tilt;
                nextExt.hi.z = midZ + tilt;
                area->SetExtent(nextExt);
                area->SetCornerHeights(midZ + tilt, midZ - tilt);
            }
        }
    } else if (m_draggedHandle == HANDLE_ROTATE_SCREEN) {
        float curAng = std::atan2(screenY - m_dragStartScreenY, screenX - m_dragStartScreenX);
        float deltaRad = curAng - m_dragStartAngle;
        float deltaDeg = deltaRad * (180.0f / 3.1415926535f);
        if (m_gridSnap) {
            deltaDeg = std::round(deltaDeg / 15.0f) * 15.0f;
        }
        if (ent) {
            float newYaw = std::fmod(m_dragStartEntityAngles.y + deltaDeg, 360.0f);
            if (newYaw < 0.0f) newYaw += 360.0f;
            ent->yaw = newYaw;
            ent->angles.y = newYaw;
        } else if (area) {
            int steps = static_cast<int>(std::round(deltaDeg / 90.0f));
            if (std::abs(steps) % 2 == 1) {
                float cX = (m_dragStartExtent.lo.x + m_dragStartExtent.hi.x) * 0.5f;
                float cY = (m_dragStartExtent.lo.y + m_dragStartExtent.hi.y) * 0.5f;
                float halfW = (m_dragStartExtent.hi.x - m_dragStartExtent.lo.x) * 0.5f;
                float halfL = (m_dragStartExtent.hi.y - m_dragStartExtent.lo.y) * 0.5f;
                NavExtent rotExt = m_dragStartExtent;
                rotExt.lo.x = cX - halfL; rotExt.hi.x = cX + halfL;
                rotExt.lo.y = cY - halfW; rotExt.hi.y = cY + halfW;
                area->SetExtent(rotExt);
            } else {
                area->SetExtent(m_dragStartExtent);
            }
        }
    } else if (area && m_draggedHandle == HANDLE_EDGE_NORTH) {
        float candidateHiY = m_dragStartExtent.hi.y + groundDelta.y;
        float targetHiY = SnapToNeighborEdge(area->GetID(), candidateHiY, false, m_dragStartExtent.lo.x, m_dragStartExtent.hi.x);
        targetHiY = std::max(m_dragStartExtent.lo.y + 8.0f, targetHiY);
        NavExtent nextExt = m_dragStartExtent;
        nextExt.hi.y = targetHiY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_EDGE_SOUTH) {
        float candidateLoY = m_dragStartExtent.lo.y + groundDelta.y;
        float targetLoY = SnapToNeighborEdge(area->GetID(), candidateLoY, false, m_dragStartExtent.lo.x, m_dragStartExtent.hi.x);
        targetLoY = std::min(m_dragStartExtent.hi.y - 8.0f, targetLoY);
        NavExtent nextExt = m_dragStartExtent;
        nextExt.lo.y = targetLoY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_EDGE_EAST) {
        float candidateHiX = m_dragStartExtent.hi.x + groundDelta.x;
        float targetHiX = SnapToNeighborEdge(area->GetID(), candidateHiX, true, m_dragStartExtent.lo.y, m_dragStartExtent.hi.y);
        targetHiX = std::max(m_dragStartExtent.lo.x + 8.0f, targetHiX);
        NavExtent nextExt = m_dragStartExtent;
        nextExt.hi.x = targetHiX;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_EDGE_WEST) {
        float candidateLoX = m_dragStartExtent.lo.x + groundDelta.x;
        float targetLoX = SnapToNeighborEdge(area->GetID(), candidateLoX, true, m_dragStartExtent.lo.y, m_dragStartExtent.hi.y);
        targetLoX = std::min(m_dragStartExtent.hi.x - 8.0f, targetLoX);
        NavExtent nextExt = m_dragStartExtent;
        nextExt.lo.x = targetLoX;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_CORNER_NW) {
        float targetLoX = std::min(m_dragStartExtent.hi.x - 8.0f, SnapToNeighborEdge(area->GetID(), m_dragStartExtent.lo.x + groundDelta.x, true, m_dragStartExtent.lo.y, m_dragStartExtent.hi.y));
        float targetHiY = std::max(m_dragStartExtent.lo.y + 8.0f, SnapToNeighborEdge(area->GetID(), m_dragStartExtent.hi.y + groundDelta.y, false, m_dragStartExtent.lo.x, m_dragStartExtent.hi.x));
        NavExtent nextExt = m_dragStartExtent;
        nextExt.lo.x = targetLoX;
        nextExt.hi.y = targetHiY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_CORNER_NE) {
        float targetHiX = std::max(m_dragStartExtent.lo.x + 8.0f, SnapToNeighborEdge(area->GetID(), m_dragStartExtent.hi.x + groundDelta.x, true, m_dragStartExtent.lo.y, m_dragStartExtent.hi.y));
        float targetHiY = std::max(m_dragStartExtent.lo.y + 8.0f, SnapToNeighborEdge(area->GetID(), m_dragStartExtent.hi.y + groundDelta.y, false, m_dragStartExtent.lo.x, m_dragStartExtent.hi.x));
        NavExtent nextExt = m_dragStartExtent;
        nextExt.hi.x = targetHiX;
        nextExt.hi.y = targetHiY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_CORNER_SE) {
        float targetHiX = std::max(m_dragStartExtent.lo.x + 8.0f, SnapToNeighborEdge(area->GetID(), m_dragStartExtent.hi.x + groundDelta.x, true, m_dragStartExtent.lo.y, m_dragStartExtent.hi.y));
        float targetLoY = std::min(m_dragStartExtent.hi.y - 8.0f, SnapToNeighborEdge(area->GetID(), m_dragStartExtent.lo.y + groundDelta.y, false, m_dragStartExtent.lo.x, m_dragStartExtent.hi.x));
        NavExtent nextExt = m_dragStartExtent;
        nextExt.hi.x = targetHiX;
        nextExt.lo.y = targetLoY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_CORNER_SW) {
        float targetLoX = std::min(m_dragStartExtent.hi.x - 8.0f, SnapToNeighborEdge(area->GetID(), m_dragStartExtent.lo.x + groundDelta.x, true, m_dragStartExtent.lo.y, m_dragStartExtent.hi.y));
        float targetLoY = std::min(m_dragStartExtent.hi.y - 8.0f, SnapToNeighborEdge(area->GetID(), m_dragStartExtent.lo.y + groundDelta.y, false, m_dragStartExtent.lo.x, m_dragStartExtent.hi.x));
        NavExtent nextExt = m_dragStartExtent;
        nextExt.lo.x = targetLoX;
        nextExt.lo.y = targetLoY;
        area->SetExtent(nextExt);
    }

    if (area) {
        RebuildNavRenderer();
    }
}

bool EditorScene::EndDragHandle(CommandManager& cmdMgr) {
    if (!m_isDraggingHandle) return false;

    if (m_multiDragStates.size() > 1 && m_nav && m_nav->IsLoaded()) {
        std::vector<std::unique_ptr<IEditCommand>> cmds;
        for (const auto& state : m_multiDragStates) {
            NavArea* a = m_nav->GetAreaByID(state.areaId);
            if (a) {
                NavExtent curExt = a->GetExtent();
                float curNeZ = a->GetNEZ();
                float curSwZ = a->GetSWZ();
                if (curExt.lo != state.startExtent.lo || curExt.hi != state.startExtent.hi ||
                    curNeZ != state.startNeZ || curSwZ != state.startSwZ) {
                    m_nav->GetGrid().RemoveArea(a);
                    a->SetExtent(state.startExtent);
                    a->SetCornerHeights(state.startNeZ, state.startSwZ);
                    m_nav->GetGrid().AddArea(a);

                    cmds.push_back(std::make_unique<CmdTransformArea>(this, state.areaId,
                        state.startExtent, state.startNeZ, state.startSwZ,
                        curExt, curNeZ, curSwZ, "Multi-Area Transform"));
                }
            }
        }
        m_multiDragStates.clear();
        m_isDraggingHandle = false;
        m_draggedHandle = HANDLE_NONE;
        if (!cmds.empty()) {
            cmdMgr.ExecuteCommand(std::make_unique<CmdCompound>(std::move(cmds), "Multi-Area Transform"));
        }
        return true;
    }

    NavArea* area = GetSelectedArea();
    if (area) {
        NavExtent newExt = area->GetExtent();
        float newNeZ = area->GetNEZ();
        float newSwZ = area->GetSWZ();

        area->SetExtent(m_dragStartExtent);
        area->SetCornerHeights(m_dragStartNeZ, m_dragStartSwZ);

        const char* cmdName = GetHandleName(m_draggedHandle);
        cmdMgr.ExecuteCommand(std::make_unique<CmdTransformArea>(this, area->GetID(),
            m_dragStartExtent, m_dragStartNeZ, m_dragStartSwZ,
            newExt, newNeZ, newSwZ, cmdName));
    }

    m_multiDragStates.clear();
    m_isDraggingHandle = false;
    m_draggedHandle = HANDLE_NONE;
    return true;
}

void EditorScene::StartGrab(const Vector3& initialHitPoint) {
    NavArea* area = GetSelectedArea();
    if (!area) return;
    m_transformMode = TRANSFORM_TRANSLATE;
    m_transformAxis = AXIS_NONE;
    m_initialExtent = area->GetExtent();
    m_initialNeZ = area->GetNEZ();
    m_initialSwZ = area->GetSWZ();
    m_initialHitPoint = initialHitPoint;
    m_grabOffset = Vector3(0.0f, 0.0f, 0.0f);
    RebuildNavRenderer();
}

void EditorScene::StartGrabWithRay(const Ray& ray) {
    NavArea* area = GetSelectedArea();
    if (!area) return;
    m_transformMode = TRANSFORM_TRANSLATE;
    m_transformAxis = AXIS_NONE;
    m_initialExtent = area->GetExtent();
    m_initialNeZ = area->GetNEZ();
    m_initialSwZ = area->GetSWZ();
    Vector3 center = area->GetCenter();
    if (std::abs(ray.direction.z) > 1e-4f) {
        float t = (center.z - ray.origin.z) / ray.direction.z;
        Vector3 hitPoint = ray.origin + ray.direction * t;
        m_grabOffset = hitPoint - center;
    } else {
        m_grabOffset = Vector3(0.0f, 0.0f, 0.0f);
    }
    RebuildNavRenderer();
}

void EditorScene::StartScale(const Vector3& initialHitPoint) {
    NavArea* area = GetSelectedArea();
    if (!area) return;
    m_transformMode = TRANSFORM_SCALE;
    m_transformAxis = AXIS_NONE;
    m_initialExtent = area->GetExtent();
    m_initialNeZ = area->GetNEZ();
    m_initialSwZ = area->GetSWZ();
    m_initialHitPoint = initialHitPoint;
    m_scaleStartDist = 50.0f;
    RebuildNavRenderer();
}

void EditorScene::StartScaleWithScreen(float screenX, float screenY, float viewportW, float viewportH, const Matrix4& viewProj) {
    NavArea* area = GetSelectedArea();
    if (!area) return;
    m_transformMode = TRANSFORM_SCALE;
    m_transformAxis = AXIS_NONE;
    m_initialExtent = area->GetExtent();
    m_initialNeZ = area->GetNEZ();
    m_initialSwZ = area->GetSWZ();

    Vector3 center = area->GetCenter();
    Vector4 clip = viewProj * Vector4(center.x, center.y, center.z, 1.0f);
    if (clip.w > 0.001f) {
        float ndcX = clip.x / clip.w;
        float ndcY = clip.y / clip.w;
        m_scaleCenterScreenX = (ndcX * 0.5f + 0.5f) * viewportW;
        m_scaleCenterScreenY = (1.0f - (ndcY * 0.5f + 0.5f)) * viewportH;
    } else {
        m_scaleCenterScreenX = viewportW * 0.5f;
        m_scaleCenterScreenY = viewportH * 0.5f;
    }

    m_scaleStartDist = std::hypot(screenX - m_scaleCenterScreenX, screenY - m_scaleCenterScreenY);
    if (m_scaleStartDist < 25.0f) m_scaleStartDist = 50.0f;

    RebuildNavRenderer();
}

void EditorScene::StartConnectMode() {
    if (m_selectedAreaId == 0) return;
    m_transformMode = TRANSFORM_CONNECT;
    m_transformAxis = AXIS_NONE;
    m_connectHoverAreaId = 0;
    RebuildNavRenderer();
}

void EditorScene::SetTransformAxis(EditorTransformAxis axis) {
    m_transformAxis = axis;
    RebuildNavRenderer();
}

void EditorScene::ToggleTransformAxis(EditorTransformAxis axis) {
    if (m_transformAxis == axis) {
        m_transformAxis = AXIS_NONE;
    } else {
        m_transformAxis = axis;
    }
    RebuildNavRenderer();
}

void EditorScene::CancelTransform() {
    NavArea* area = GetSelectedArea();
    if (area && (m_transformMode == TRANSFORM_TRANSLATE || m_transformMode == TRANSFORM_SCALE)) {
        if (m_nav && m_nav->IsLoaded()) {
            m_nav->GetGrid().RemoveArea(area);
            area->SetExtent(m_initialExtent);
            area->SetCornerHeights(m_initialNeZ, m_initialSwZ);
            m_nav->GetGrid().AddArea(area);
        }
    }
    m_transformMode = TRANSFORM_NONE;
    m_transformAxis = AXIS_NONE;
    m_connectHoverAreaId = 0;
    RebuildNavRenderer();
}

bool EditorScene::ConfirmTransform(CommandManager& cmdMgr) {
    NavArea* area = GetSelectedArea();
    if (!area || (m_transformMode != TRANSFORM_TRANSLATE && m_transformMode != TRANSFORM_SCALE)) {
        m_transformMode = TRANSFORM_NONE;
        m_transformAxis = AXIS_NONE;
        return false;
    }

    NavExtent curExt = area->GetExtent();
    float curNeZ = area->GetNEZ();
    float curSwZ = area->GetSWZ();

    area->SetExtent(m_initialExtent);
    area->SetCornerHeights(m_initialNeZ, m_initialSwZ);

    const char* cmdName = (m_transformMode == TRANSFORM_SCALE) ? "Scale Area" : "Move Area";
    cmdMgr.ExecuteCommand(std::make_unique<CmdTransformArea>(this, area->GetID(),
        m_initialExtent, m_initialNeZ, m_initialSwZ,
        curExt, curNeZ, curSwZ, cmdName));

    m_transformMode = TRANSFORM_NONE;
    m_transformAxis = AXIS_NONE;
    return true;
}

void EditorScene::UpdateTransform(const Vector3& currentHitPoint, float mouseDeltaY) {
    NavArea* area = GetSelectedArea();
    if (!area) return;

    if (m_transformMode == TRANSFORM_TRANSLATE) {
        Vector3 delta = currentHitPoint - m_initialHitPoint;
        if (m_transformAxis == AXIS_X) {
            delta.y = 0.0f; delta.z = 0.0f;
        } else if (m_transformAxis == AXIS_Y) {
            delta.x = 0.0f; delta.z = 0.0f;
        } else if (m_transformAxis == AXIS_Z) {
            delta = Vector3(0.0f, 0.0f, -mouseDeltaY * 0.8f);
        }

        NavExtent newExt;
        newExt.lo = m_initialExtent.lo + delta;
        newExt.hi = m_initialExtent.hi + delta;
        area->SetExtent(newExt);
        area->SetCornerHeights(m_initialNeZ + delta.z, m_initialSwZ + delta.z);
        RebuildNavRenderer();
    } else if (m_transformMode == TRANSFORM_SCALE) {
        Vector3 center = (m_initialExtent.lo + m_initialExtent.hi) * 0.5f;
        float initDist = (m_initialHitPoint - center).Length();
        float curDist = (currentHitPoint - center).Length();
        float scale = (initDist > 1.0f) ? (curDist / initDist) : 1.0f;
        scale = std::max(0.1f, std::min(10.0f, scale));

        float halfW = (m_initialExtent.hi.x - m_initialExtent.lo.x) * 0.5f;
        float halfL = (m_initialExtent.hi.y - m_initialExtent.lo.y) * 0.5f;
        float sx = (m_transformAxis == AXIS_Y) ? 1.0f : scale;
        float sy = (m_transformAxis == AXIS_X) ? 1.0f : scale;

        NavExtent newExt;
        newExt.lo = Vector3(center.x - halfW * sx, center.y - halfL * sy, m_initialExtent.lo.z);
        newExt.hi = Vector3(center.x + halfW * sx, center.y + halfL * sy, m_initialExtent.hi.z);
        area->SetExtent(newExt);
        RebuildNavRenderer();
    }
}

void EditorScene::UpdateTransformWithRay(const Ray& ray, float mouseX, float mouseY, float deltaY) {
    NavArea* area = GetSelectedArea();
    if (!area) return;

    if (m_transformMode == TRANSFORM_TRANSLATE) {
        Vector3 origCenter = (m_initialExtent.lo + m_initialExtent.hi) * 0.5f;
        if (m_transformAxis == AXIS_Z) {
            float zDelta = -deltaY * 0.8f;
            float origMidZ = (m_initialExtent.lo.z + m_initialExtent.hi.z + m_initialNeZ + m_initialSwZ) * 0.25f;
            float targetZ = SnapValue(origMidZ + zDelta);
            float shiftZ = targetZ - origMidZ;
            NavExtent newExt = m_initialExtent;
            newExt.lo.z += shiftZ;
            newExt.hi.z += shiftZ;
            area->SetExtent(newExt);
            area->SetCornerHeights(m_initialNeZ + shiftZ, m_initialSwZ + shiftZ);
        } else {
            Vector3 hitPoint;
            if (std::abs(ray.direction.z) > 1e-4f) {
                float t = (origCenter.z - ray.origin.z) / ray.direction.z;
                hitPoint = ray.origin + ray.direction * t;
            } else {
                hitPoint = origCenter;
            }

            Vector3 targetCenter = hitPoint - m_grabOffset;
            if (m_transformAxis == AXIS_X) {
                targetCenter.y = origCenter.y;
            } else if (m_transformAxis == AXIS_Y) {
                targetCenter.x = origCenter.x;
            }

            targetCenter = SnapVector(targetCenter);
            Vector3 moveDelta = targetCenter - origCenter;

            NavExtent newExt;
            newExt.lo = m_initialExtent.lo + moveDelta;
            newExt.hi = m_initialExtent.hi + moveDelta;
            area->SetExtent(newExt);
            area->SetCornerHeights(m_initialNeZ + moveDelta.z, m_initialSwZ + moveDelta.z);
        }
        RebuildNavRenderer();
    } else if (m_transformMode == TRANSFORM_SCALE) {
        float curDist = std::hypot(mouseX - m_scaleCenterScreenX, mouseY - m_scaleCenterScreenY);
        float scale = (m_scaleStartDist > 1.0f) ? (curDist / m_scaleStartDist) : 1.0f;
        scale = std::max(0.1f, std::min(10.0f, scale));

        Vector3 center = (m_initialExtent.lo + m_initialExtent.hi) * 0.5f;
        float initW = m_initialExtent.hi.x - m_initialExtent.lo.x;
        float initL = m_initialExtent.hi.y - m_initialExtent.lo.y;

        float newW = (m_transformAxis == AXIS_Y) ? initW : SnapValue(initW * scale);
        float newL = (m_transformAxis == AXIS_X) ? initL : SnapValue(initL * scale);
        newW = std::max(8.0f, newW);
        newL = std::max(8.0f, newL);

        NavExtent newExt;
        newExt.lo = Vector3(center.x - newW * 0.5f, center.y - newL * 0.5f, m_initialExtent.lo.z);
        newExt.hi = Vector3(center.x + newW * 0.5f, center.y + newL * 0.5f, m_initialExtent.hi.z);
        area->SetExtent(newExt);
        RebuildNavRenderer();
    }
}

void EditorScene::SetConnectHoverArea(uint32_t areaId) {
    if (m_connectHoverAreaId == areaId) return;
    m_connectHoverAreaId = areaId;
    RebuildNavRenderer();
}

void EditorScene::ConnectSelectedTo(uint32_t targetId, bool bidirectional, CommandManager& cmdMgr) {
    NavArea* area = GetSelectedArea();
    if (!area || targetId == 0 || targetId == area->GetID()) return;
    cmdMgr.ExecuteCommand(std::make_unique<CmdConnectAreas>(this, area->GetID(), targetId, bidirectional));
}

void EditorScene::DisconnectSelectedFrom(uint32_t targetId, bool bidirectional, CommandManager& cmdMgr) {
    NavArea* area = GetSelectedArea();
    if (!area || targetId == 0) return;
    cmdMgr.ExecuteCommand(std::make_unique<CmdDisconnectAreas>(this, area->GetID(), targetId, bidirectional));
}

void EditorScene::SelectConnection(uint32_t fromId, uint32_t toId, int dir) {
    m_selectedConnection = { fromId, toId, dir };
    m_navRenderer.SetSelectedConnection(fromId, toId);
}

void EditorScene::ClearSelectedConnection() {
    m_selectedConnection.clear();
    m_navRenderer.SetSelectedConnection(0, 0);
}

bool EditorScene::DeleteSelectedConnection(CommandManager& cmdMgr) {
    if (!m_selectedConnection.valid() || !m_nav || !m_nav->IsLoaded()) return false;
    uint32_t fromId = m_selectedConnection.fromId;
    uint32_t toId = m_selectedConnection.toId;
    NavArea* fromArea = m_nav->GetAreaByID(fromId);
    NavArea* toArea = m_nav->GetAreaByID(toId);
    if (!fromArea || !toArea) return false;

    bool isTwoWay = toArea->IsConnected(fromArea);
    cmdMgr.ExecuteCommand(std::make_unique<CmdDisconnectAreas>(this, fromId, toId, isTwoWay));
    ClearSelectedConnection();
    return true;
}

bool EditorScene::ReverseSelectedConnection(CommandManager& cmdMgr) {
    if (!m_selectedConnection.valid() || !m_nav || !m_nav->IsLoaded()) return false;
    uint32_t fromId = m_selectedConnection.fromId;
    uint32_t toId = m_selectedConnection.toId;
    NavArea* fromArea = m_nav->GetAreaByID(fromId);
    NavArea* toArea = m_nav->GetAreaByID(toId);
    if (!fromArea || !toArea) return false;

    cmdMgr.ExecuteCommand(std::make_unique<CmdDisconnectAreas>(this, fromId, toId, false));
    cmdMgr.ExecuteCommand(std::make_unique<CmdConnectAreas>(this, toId, fromId, false));
    SelectConnection(toId, fromId);
    return true;
}

bool EditorScene::ToggleSelectedConnectionBidirectional(CommandManager& cmdMgr) {
    if (!m_selectedConnection.valid() || !m_nav || !m_nav->IsLoaded()) return false;
    uint32_t fromId = m_selectedConnection.fromId;
    uint32_t toId = m_selectedConnection.toId;
    NavArea* fromArea = m_nav->GetAreaByID(fromId);
    NavArea* toArea = m_nav->GetAreaByID(toId);
    if (!fromArea || !toArea) return false;

    bool isTwoWay = toArea->IsConnected(fromArea);
    if (isTwoWay) {
        cmdMgr.ExecuteCommand(std::make_unique<CmdDisconnectAreas>(this, toId, fromId, false));
    } else {
        cmdMgr.ExecuteCommand(std::make_unique<CmdConnectAreas>(this, toId, fromId, false));
    }
    return true;
}

void EditorScene::DuplicateSelectedArea(CommandManager& cmdMgr) {
    if (m_selectedAreaIds.size() > 1) {
        BatchDuplicate(cmdMgr);
        return;
    }
    NavArea* area = GetSelectedArea();
    if (!area) return;
    auto dupCmd = std::make_unique<CmdDuplicateArea>(this, area->GetID());
    cmdMgr.ExecuteCommand(std::move(dupCmd));
    NavArea* newArea = GetSelectedArea();
    if (newArea) {
        StartGrab(newArea->GetCenter());
    }
}

void EditorScene::DeleteSelectedArea(CommandManager& cmdMgr) {
    if (m_selectedAreaIds.size() > 1) {
        BatchDelete(cmdMgr);
        return;
    }
    NavArea* area = GetSelectedArea();
    if (!area) return;
    cmdMgr.ExecuteCommand(std::make_unique<CmdDeleteArea>(this, area->GetID()));
}

void EditorScene::RotateSelectedArea90(CommandManager& cmdMgr) {
    NavArea* area = GetSelectedArea();
    if (!area) return;
    NavExtent oldExt = area->GetExtent();
    Vector3 center = (oldExt.lo + oldExt.hi) * 0.5f;
    float halfW = (oldExt.hi.x - oldExt.lo.x) * 0.5f;
    float halfL = (oldExt.hi.y - oldExt.lo.y) * 0.5f;
    NavExtent newExt;
    newExt.lo = Vector3(center.x - halfL, center.y - halfW, oldExt.lo.z);
    newExt.hi = Vector3(center.x + halfL, center.y + halfW, oldExt.hi.z);
    cmdMgr.ExecuteCommand(std::make_unique<CmdTransformArea>(this, area->GetID(),
        oldExt, area->GetNEZ(), area->GetSWZ(),
        newExt, area->GetNEZ(), area->GetSWZ(), "Rotate Area 90°"));
}

void EditorScene::ExtrudeSelectedEdge(CommandManager& cmdMgr, float length) {
    NavArea* area = GetSelectedArea();
    if (!area) return;

    SelectedHandleType edge = m_selectedHandle;
    if (edge < HANDLE_EDGE_NORTH || edge > HANDLE_EDGE_WEST) {
        edge = HANDLE_EDGE_NORTH;
    }

    if (length <= 0.0f) {
        length = (m_gridSize >= 4.0f) ? m_gridSize : 64.0f;
    }

    cmdMgr.ExecuteCommand(std::make_unique<CmdExtrudeArea>(this, area->GetID(), edge, length));
}

void EditorScene::SplitSelectedArea(CommandManager& cmdMgr) {
    NavArea* area = GetSelectedArea();
    if (!area) return;

    float width = area->GetExtent().hi.x - area->GetExtent().lo.x;
    float length = area->GetExtent().hi.y - area->GetExtent().lo.y;
    bool splitAlongY = (length >= width);
    float splitCoord = splitAlongY ? (area->GetExtent().lo.y + area->GetExtent().hi.y) * 0.5f
                                   : (area->GetExtent().lo.x + area->GetExtent().hi.x) * 0.5f;

    cmdMgr.ExecuteCommand(std::make_unique<CmdSplitAreaKnife>(this, area->GetID(), splitCoord, splitAlongY));
}

void EditorScene::MergeSelectedArea(CommandManager& cmdMgr) {
    if (!m_nav || !m_nav->IsLoaded() || m_nav->GetAreaCount() < 2) return;

    auto CanMerge = [](const NavArea* a, const NavArea* b) -> bool {
        if (!a || !b) return false;
        if (a->GetAttributes() != b->GetAttributes()) return false;
        if (a->GetPlace() != b->GetPlace() || a->GetPlaceName() != b->GetPlaceName()) return false;

        const NavArea* first = a;
        const NavArea* second = b;

        // Try X merge (horizontal)
        if (first->GetExtent().lo.x > second->GetExtent().lo.x) std::swap(first, second);
        const NavExtent& ext1 = first->GetExtent();
        const NavExtent& ext2 = second->GetExtent();

        if (std::fabs(ext1.hi.x - ext2.lo.x) <= 1.5f &&
            std::fabs(ext1.lo.y - ext2.lo.y) <= 1.5f &&
            std::fabs(ext1.hi.y - ext2.hi.y) <= 1.5f) {
            if (std::fabs(first->GetNEZ() - ext2.lo.z) <= 3.0f &&
                std::fabs(ext1.hi.z - second->GetSWZ()) <= 3.0f) {
                return true;
            }
        }

        // Try Y merge (vertical)
        first = a;
        second = b;
        if (first->GetExtent().lo.y > second->GetExtent().lo.y) std::swap(first, second);
        const NavExtent& yExt1 = first->GetExtent();
        const NavExtent& yExt2 = second->GetExtent();

        if (std::fabs(yExt1.hi.y - yExt2.lo.y) <= 1.5f &&
            std::fabs(yExt1.lo.x - yExt2.lo.x) <= 1.5f &&
            std::fabs(yExt1.hi.x - yExt2.hi.x) <= 1.5f) {
            if (std::fabs(first->GetSWZ() - yExt2.lo.z) <= 3.0f &&
                std::fabs(yExt1.hi.z - second->GetNEZ()) <= 3.0f) {
                return true;
            }
        }

        return false;
    };

    // Case 1: Exactly 2 areas in multi-selection
    if (m_selectedAreaIds.size() == 2) {
        NavArea* a1 = m_nav->GetAreaByID(m_selectedAreaIds[0]);
        NavArea* a2 = m_nav->GetAreaByID(m_selectedAreaIds[1]);
        if (CanMerge(a1, a2)) {
            uint32_t keepId = a1->GetID();
            uint32_t removeId = a2->GetID();
            cmdMgr.ExecuteCommand(std::make_unique<CmdMergeAreas>(this, keepId, removeId));
            ClearSelection();
            SelectArea(keepId);
            return;
        }
    }

    // Case 2: Multi-selection with > 2 areas: merge any pair that matches
    if (m_selectedAreaIds.size() > 2) {
        for (size_t i = 0; i < m_selectedAreaIds.size(); ++i) {
            NavArea* a1 = m_nav->GetAreaByID(m_selectedAreaIds[i]);
            if (!a1) continue;
            for (size_t j = i + 1; j < m_selectedAreaIds.size(); ++j) {
                NavArea* a2 = m_nav->GetAreaByID(m_selectedAreaIds[j]);
                if (!a2) continue;
                if (CanMerge(a1, a2)) {
                    uint32_t keepId = a1->GetID();
                    uint32_t removeId = a2->GetID();
                    cmdMgr.ExecuteCommand(std::make_unique<CmdMergeAreas>(this, keepId, removeId));
                    m_selectedAreaIds.erase(m_selectedAreaIds.begin() + j);
                    return;
                }
            }
        }
    }

    // Case 3: Single selected area: find first adjacent neighbor that can merge
    NavArea* area = GetSelectedArea();
    if (!area) return;

    for (int d = 0; d < 4; ++d) {
        const auto& conns = area->GetAdjacentList(static_cast<NavDirType>(d));
        for (const auto& c : conns) {
            if (!c.area || c.area->GetID() == area->GetID()) continue;
            if (CanMerge(area, c.area)) {
                cmdMgr.ExecuteCommand(std::make_unique<CmdMergeAreas>(this, area->GetID(), c.area->GetID()));
                return;
            }
        }
    }
}

bool EditorScene::LoadWAD(const std::string& wadPath) {
    if (!m_textureManager.LoadWAD(wadPath)) {
        return false;
    }
    if (m_bsp && m_bsp->IsLoaded()) {
        m_textureManager.LoadForBSP(*m_bsp, m_bspPath, m_gameDirectory);
        m_bspRenderer.BuildFromBSP(*m_bsp, &m_textureManager);
    }
    return true;
}

void EditorScene::BatchSetAttributes(uint8_t flags, CommandManager& cmdMgr) {
    if (m_selectedAreaIds.empty()) return;
    cmdMgr.ExecuteCommand(std::make_unique<CmdBatchSetAttributes>(this, m_selectedAreaIds, flags));
}

void EditorScene::BatchSetPlace(const std::string& placeName, CommandManager& cmdMgr) {
    if (m_selectedAreaIds.empty()) return;
    cmdMgr.ExecuteCommand(std::make_unique<CmdBatchSetPlace>(this, m_selectedAreaIds, placeName));
}

void EditorScene::BatchSnapToNeighbors(CommandManager& cmdMgr) {
    if (m_selectedAreaIds.empty()) return;
    std::vector<std::unique_ptr<IEditCommand>> cmds;
    for (uint32_t id : m_selectedAreaIds) {
        cmds.push_back(std::make_unique<CmdSnapAreaToNeighbors>(this, id, m_meshSnapTolerance * 1.5f));
    }
    cmdMgr.ExecuteCommand(std::make_unique<CmdCompound>(std::move(cmds), "Batch Snap to Neighbors"));
}

void EditorScene::BatchSnapToFloor(CommandManager& cmdMgr) {
    if (m_selectedAreaIds.empty() || !HasBSP()) return;
    std::vector<std::unique_ptr<IEditCommand>> cmds;
    for (uint32_t id : m_selectedAreaIds) {
        cmds.push_back(std::make_unique<CmdSnapAreaToFloor>(this, id));
    }
    cmdMgr.ExecuteCommand(std::make_unique<CmdCompound>(std::move(cmds), "Batch Snap to Floor"));
}

void EditorScene::BatchDuplicate(CommandManager& cmdMgr) {
    if (m_selectedAreaIds.empty()) return;
    std::vector<std::unique_ptr<IEditCommand>> cmds;
    for (uint32_t id : m_selectedAreaIds) {
        cmds.push_back(std::make_unique<CmdDuplicateArea>(this, id));
    }
    cmdMgr.ExecuteCommand(std::make_unique<CmdCompound>(std::move(cmds), "Batch Duplicate Areas"));
}

void EditorScene::BatchDelete(CommandManager& cmdMgr) {
    if (m_selectedAreaIds.empty()) return;
    std::vector<std::unique_ptr<IEditCommand>> cmds;
    for (uint32_t id : m_selectedAreaIds) {
        cmds.push_back(std::make_unique<CmdDeleteArea>(this, id));
    }
    cmdMgr.ExecuteCommand(std::make_unique<CmdCompound>(std::move(cmds), "Batch Delete Areas"));
    ClearSelection();
}

void EditorScene::StartDrawAreaMode() {
    m_isDrawAreaMode = true;
    m_drawAreaActive = false;
    m_drawAreaStart = Vector3(0.0f, 0.0f, 0.0f);
    m_drawAreaCurrent = Vector3(0.0f, 0.0f, 0.0f);
    m_drawAreaElevation = 0.0f;
    if (m_isBridgeMode) CancelBridgeMode();
}

void EditorScene::CancelDrawArea() {
    if (m_drawAreaActive) {
        m_drawAreaActive = false;
    } else {
        m_isDrawAreaMode = false;
    }
    m_drawAreaSnappedCorner = false;
}

void EditorScene::ExitDrawAreaMode() {
    m_isDrawAreaMode = false;
    m_drawAreaActive = false;
    m_drawAreaSnappedCorner = false;
}

void EditorScene::ToggleDrawAreaMode() {
    if (m_isDrawAreaMode) {
        ExitDrawAreaMode();
    } else {
        StartDrawAreaMode();
    }
}

void EditorScene::UpdateDrawArea(const Ray& ray) {
    if (!m_drawAreaActive) return;

    Vector3 hit;
    bool hasHit = false;
    if (HasBSP()) {
        hasHit = ScenePicker::PickBSPFloor(*this, ray, &hit);
    }
    if (!hasHit) {
        if (!IntersectRayWithPlane(ray, Vector3(0.0f, 0.0f, m_drawAreaElevation), Vector3(0.0f, 0.0f, 1.0f), hit)) {
            hit = ray.origin + ray.direction * 500.0f;
            hit.z = m_drawAreaElevation;
        }
    }

    if (m_gridSnap) {
        hit.x = SnapValue(hit.x);
        hit.y = SnapValue(hit.y);
    }
    if (m_meshSnap) {
        hit.x = SnapToNeighborEdge(0, hit.x, true, std::min(m_drawAreaStart.y, hit.y), std::max(m_drawAreaStart.y, hit.y));
        hit.y = SnapToNeighborEdge(0, hit.y, false, std::min(m_drawAreaStart.x, hit.x), std::max(m_drawAreaStart.x, hit.x));
    }

    // Magnetic corner snapping to existing navmesh areas
    Vector3 cornerHit = hit;
    if (SnapToAreaCorner(cornerHit, m_cornerSnapTolerance)) {
        hit = cornerHit;
        m_drawAreaSnappedCorner = true;
        m_drawAreaCornerPos = cornerHit;
    } else {
        m_drawAreaSnappedCorner = false;
    }

    m_drawAreaCurrent = hit;

    float minX = std::min(m_drawAreaStart.x, m_drawAreaCurrent.x);
    float maxX = std::max(m_drawAreaStart.x, m_drawAreaCurrent.x);
    float minY = std::min(m_drawAreaStart.y, m_drawAreaCurrent.y);
    float maxY = std::max(m_drawAreaStart.y, m_drawAreaCurrent.y);

    m_drawAreaNwZ = m_drawAreaElevation;
    m_drawAreaNeZ = m_drawAreaElevation;
    m_drawAreaSeZ = m_drawAreaElevation;
    m_drawAreaSwZ = m_drawAreaElevation;

    if (HasBSP() && maxX - minX >= 4.0f && maxY - minY >= 4.0f) {
        Vector3 center((minX + maxX) * 0.5f, (minY + maxY) * 0.5f, (m_drawAreaStart.z + m_drawAreaCurrent.z) * 0.5f);
        float topZ = std::max(m_drawAreaStart.z, m_drawAreaCurrent.z) + 64.0f;
        float botZ = std::min(m_drawAreaStart.z, m_drawAreaCurrent.z) - 256.0f;

        BSPTraceResult trCenter;
        bool hitCenter = false;
        if (GetBSP().TraceWorld(Vector3(center.x, center.y, topZ), Vector3(center.x, center.y, botZ), HULL_POINT, &trCenter)) {
            if (!trCenter.startsolid && !trCenter.allsolid && trCenter.fraction > 0.0f) {
                hitCenter = true;
            }
        }

        Vector3 planeNorm = hitCenter ? trCenter.planeNormal : Vector3(0.0f, 0.0f, 1.0f);
        if (std::abs(planeNorm.z) < 0.2f) {
            planeNorm = Vector3(0.0f, 0.0f, 1.0f);
        }

        Vector3 refPos = hitCenter ? trCenter.endpos : center;
        auto PlaneZ = [&](float x, float y) -> float {
            float dx = x - refPos.x;
            float dy = y - refPos.y;
            return refPos.z - (planeNorm.x * dx + planeNorm.y * dy) / planeNorm.z;
        };

        auto SampleCornerZ = [&](float x, float y) -> float {
            float expZ = PlaneZ(x, y);
            Vector3 cStart(x, y, expZ + 32.0f);
            Vector3 cEnd(x, y, expZ - 48.0f);
            BSPTraceResult tr;
            if (GetBSP().TraceWorld(cStart, cEnd, HULL_POINT, &tr)) {
                if (!tr.startsolid && !tr.allsolid && tr.fraction > 0.0f) {
                    return tr.endpos.z;
                }
            }
            return expZ;
        };

        m_drawAreaNwZ = SampleCornerZ(minX, minY);
        m_drawAreaNeZ = SampleCornerZ(maxX, minY);
        m_drawAreaSeZ = SampleCornerZ(maxX, maxY);
        m_drawAreaSwZ = SampleCornerZ(minX, maxY);
    }
}

void EditorScene::OnDrawAreaClick(const Ray& ray, CommandManager& cmdMgr) {
    if (!m_drawAreaActive) {
        // Step 1: 1st Corner
        Vector3 hitPoint;
        bool hasHit = false;
        if (HasBSP()) {
            hasHit = ScenePicker::PickBSPFloor(*this, ray, &hitPoint);
        }
        if (!hasHit && HasNAV()) {
            uint32_t hitAreaId = ScenePicker::PickNavArea(*this, ray, &hitPoint);
            if (hitAreaId != 0) {
                hasHit = true;
            }
        }
        if (!hasHit) {
            hasHit = IntersectRayWithPlane(ray, Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), hitPoint);
        }
        if (!hasHit) {
            hitPoint = ray.origin + ray.direction * 500.0f;
        }

        if (m_gridSnap) {
            hitPoint.x = SnapValue(hitPoint.x);
            hitPoint.y = SnapValue(hitPoint.y);
            hitPoint.z = SnapValue(hitPoint.z);
        }
        if (m_meshSnap) {
            hitPoint.x = SnapToNeighborEdge(0, hitPoint.x, true, hitPoint.y - 16.0f, hitPoint.y + 16.0f);
            hitPoint.y = SnapToNeighborEdge(0, hitPoint.y, false, hitPoint.x - 16.0f, hitPoint.x + 16.0f);
        }

        Vector3 cornerHit = hitPoint;
        if (SnapToAreaCorner(cornerHit, m_cornerSnapTolerance)) {
            hitPoint = cornerHit;
        }

        m_drawAreaStart = hitPoint;
        m_drawAreaElevation = hitPoint.z;
        m_drawAreaCurrent = hitPoint;
        m_drawAreaNwZ = hitPoint.z;
        m_drawAreaNeZ = hitPoint.z;
        m_drawAreaSeZ = hitPoint.z;
        m_drawAreaSwZ = hitPoint.z;
        m_drawAreaActive = true;
    } else {
        // Step 2: 2nd Corner
        UpdateDrawArea(ray);
        m_drawAreaSnappedCorner = false;

        float minX = std::min(m_drawAreaStart.x, m_drawAreaCurrent.x);
        float maxX = std::max(m_drawAreaStart.x, m_drawAreaCurrent.x);
        float minY = std::min(m_drawAreaStart.y, m_drawAreaCurrent.y);
        float maxY = std::max(m_drawAreaStart.y, m_drawAreaCurrent.y);

        if (maxX - minX >= 8.0f && maxY - minY >= 8.0f) {
            NavExtent extent(Vector3(minX, minY, m_drawAreaNwZ),
                             Vector3(maxX, maxY, m_drawAreaSeZ));

            auto cmd = std::make_unique<CmdCreateArea>(this, extent, m_drawAreaNeZ, m_drawAreaSwZ);
            cmdMgr.ExecuteCommand(std::move(cmd));
            m_isModified = true;
        }

        m_drawAreaActive = false;
    }
}

void EditorScene::StartFillAreaMode() {
    ExitDrawAreaMode();
    CancelBridgeMode();
    CancelTransform();
    m_isFillAreaMode = true;
}

void EditorScene::ExitFillAreaMode() {
    m_isFillAreaMode = false;
}

void EditorScene::ToggleFillAreaMode() {
    if (m_isFillAreaMode) {
        ExitFillAreaMode();
    } else {
        StartFillAreaMode();
    }
}

size_t EditorScene::FloodFillAreaAt(const Ray& ray, CommandManager& cmdMgr) {
    if (!HasBSP()) return 0;

    Vector3 seedPos;
    if (!ScenePicker::PickBSPFloor(*this, ray, &seedPos)) {
        return 0;
    }

    NavGenerateOptions opts;
    opts.stepSize = (m_gridSize >= 16.0f && m_gridSize <= 64.0f) ? m_gridSize : 25.0f;
    opts.mergeAreas = true;
    opts.generateCrouch = true;

    auto cmd = std::make_unique<CmdFloodFill>(this, seedPos, opts);
    cmdMgr.ExecuteCommand(std::move(cmd));
    m_isModified = true;

    return m_selectedAreaIds.size();
}

void EditorScene::BatchExtrude(CommandManager& cmdMgr, SelectedHandleType edge, float length) {
    if (m_selectedAreaIds.empty()) return;
    if (edge < HANDLE_EDGE_NORTH || edge > HANDLE_EDGE_WEST) {
        edge = HANDLE_EDGE_NORTH;
    }
    if (length <= 0.0f) {
        length = (m_gridSize >= 4.0f) ? m_gridSize : 64.0f;
    }
    cmdMgr.ExecuteCommand(std::make_unique<CmdBatchExtrude>(this, m_selectedAreaIds, edge, length));
}

EditorScene::AnalyzerStats EditorScene::AutoAnalyzeFlags(CommandManager& cmdMgr, bool selectedOnly) {
    AnalyzerStats stats;
    if (!m_nav || !m_nav->IsLoaded()) return stats;

    std::vector<NavArea*> candidates;
    if (selectedOnly && !m_selectedAreaIds.empty()) {
        for (uint32_t id : m_selectedAreaIds) {
            NavArea* a = m_nav->GetAreaByID(id);
            if (a) candidates.push_back(a);
        }
    } else {
        candidates = m_nav->GetAreas();
    }

    stats.totalScanned = candidates.size();
    std::vector<std::pair<uint32_t, uint8_t>> oldFlags;
    std::vector<std::pair<uint32_t, uint8_t>> newFlags;

    for (NavArea* area : candidates) {
        if (!area) continue;
        uint8_t currentAttr = area->GetAttributes();
        uint8_t detected = currentAttr;

        // 1. Low Headroom / Crouch check (< 72 units clearance)
        bool isCrouch = false;
        if (m_bsp && m_bsp->IsLoaded()) {
            Vector3 center = area->GetCenter();
            float w = area->GetExtent().hi.x - area->GetExtent().lo.x;
            float l = area->GetExtent().hi.y - area->GetExtent().lo.y;
            float insetX = std::min(8.0f, w * 0.25f);
            float insetY = std::min(8.0f, l * 0.25f);

            Vector3 testPts[5] = {
                center,
                Vector3(area->GetExtent().lo.x + insetX, area->GetExtent().lo.y + insetY, 0.0f),
                Vector3(area->GetExtent().hi.x - insetX, area->GetExtent().lo.y + insetY, 0.0f),
                Vector3(area->GetExtent().hi.x - insetX, area->GetExtent().hi.y - insetY, 0.0f),
                Vector3(area->GetExtent().lo.x + insetX, area->GetExtent().hi.y - insetY, 0.0f)
            };
            for (int i = 1; i < 5; ++i) {
                testPts[i].z = area->GetZ(testPts[i].x, testPts[i].y);
            }

            for (const auto& pt : testPts) {
                Vector3 start(pt.x, pt.y, pt.z + 4.0f);
                Vector3 end(pt.x, pt.y, pt.z + 74.0f);
                BSPTraceResult tr;
                if (m_bsp->TraceWorld(start, end, HULL_POINT, &tr)) {
                    if (!tr.startsolid && !tr.allsolid && tr.fraction < 1.0f) {
                        float clearance = tr.endpos.z - pt.z;
                        if (clearance < 72.0f && clearance >= 24.0f) {
                            isCrouch = true;
                            break;
                        }
                    }
                }
            }
        }
        if (isCrouch) {
            detected |= NAV_ATTR_CROUCH;
            if (!(currentAttr & NAV_ATTR_CROUCH)) stats.crouchCount++;
        }

        // 2. Narrow Constriction / Doorway check (< 48 units width or length)
        float width = area->GetExtent().hi.x - area->GetExtent().lo.x;
        float length = area->GetExtent().hi.y - area->GetExtent().lo.y;
        if (width < 48.0f || length < 48.0f) {
            detected |= NAV_ATTR_PRECISE;
            if (!(currentAttr & NAV_ATTR_PRECISE)) stats.preciseCount++;
        }

        // 3. Step Obstacle / Ledge Jump check (> 18 units elevation difference)
        bool isJump = false;
        for (int d = 0; d < 4; ++d) {
            for (const auto& conn : area->GetAdjacentList(static_cast<NavDirType>(d))) {
                if (conn.area) {
                    float dz = conn.area->GetCenter().z - area->GetCenter().z;
                    if (dz > 18.0f && dz <= 45.0f) {
                        isJump = true;
                        break;
                    }
                }
            }
            if (isJump) break;
        }
        if (isJump) {
            detected |= NAV_ATTR_JUMP;
            if (!(currentAttr & NAV_ATTR_JUMP)) stats.jumpCount++;
        }

        if (detected != currentAttr) {
            oldFlags.push_back({ area->GetID(), currentAttr });
            newFlags.push_back({ area->GetID(), detected });
        }
    }

    stats.totalModified = newFlags.size();
    if (!newFlags.empty()) {
        cmdMgr.ExecuteCommand(std::make_unique<CmdAnalyzeAreaFlags>(this, oldFlags, newFlags));
    }
    return stats;
}

EditorScene::OptimizeMeshStats EditorScene::OptimizeMesh(CommandManager& cmdMgr, bool selectedOnly) {
    OptimizeMeshStats stats;
    if (!m_nav || !m_nav->IsLoaded()) return stats;

    stats.initialAreaCount = m_nav->GetAreaCount();

    std::vector<OptimizeMergeRecord> allRecords;
    std::vector<uint32_t> candidateIds;
    if (selectedOnly && !m_selectedAreaIds.empty()) {
        candidateIds = m_selectedAreaIds;
    } else {
        for (const auto* a : m_nav->GetAreas()) {
            if (a) candidateIds.push_back(a->GetID());
        }
    }

    bool mergedAny = false;
    do {
        mergedAny = false;
        std::vector<uint32_t> activeIds;
        for (uint32_t id : candidateIds) {
            if (m_nav->GetAreaByID(id)) activeIds.push_back(id);
        }

        for (size_t i = 0; i < activeIds.size(); ++i) {
            NavArea* a = m_nav->GetAreaByID(activeIds[i]);
            if (!a) continue;

            for (size_t j = i + 1; j < activeIds.size(); ++j) {
                NavArea* b = m_nav->GetAreaByID(activeIds[j]);
                if (!b) continue;

                if (a->GetAttributes() != b->GetAttributes()) continue;
                if (a->GetPlace() != b->GetPlace() || a->GetPlaceName() != b->GetPlaceName()) continue;

                NavArea* first = a;
                NavArea* second = b;
                bool isXMerge = false;
                bool isYMerge = false;

                // Horizontal (along X)
                if (first->GetExtent().lo.x > second->GetExtent().lo.x) {
                    std::swap(first, second);
                }
                const NavExtent& ext1 = first->GetExtent();
                const NavExtent& ext2 = second->GetExtent();

                if (std::fabs(ext1.hi.x - ext2.lo.x) <= 0.25f &&
                    std::fabs(ext1.lo.y - ext2.lo.y) <= 0.25f &&
                    std::fabs(ext1.hi.y - ext2.hi.y) <= 0.25f) {
                    if (std::fabs(first->GetNEZ() - ext2.lo.z) <= 2.5f &&
                        std::fabs(ext1.hi.z - second->GetSWZ()) <= 2.5f) {
                        float w1 = ext1.hi.x - ext1.lo.x;
                        float w2 = ext2.hi.x - ext2.lo.x;
                        float wTot = w1 + w2;
                        if (wTot > 0.001f) {
                            float r = w1 / wTot;
                            float expN = ext1.lo.z + (second->GetNEZ() - ext1.lo.z) * r;
                            float expS = first->GetSWZ() + (ext2.hi.z - first->GetSWZ()) * r;
                            if (std::fabs(first->GetNEZ() - expN) <= 2.5f &&
                                std::fabs(ext1.hi.z - expS) <= 2.5f) {
                                isXMerge = true;
                            }
                        }
                    }
                }

                if (!isXMerge) {
                    // Vertical (along Y)
                    first = a;
                    second = b;
                    if (first->GetExtent().lo.y > second->GetExtent().lo.y) {
                        std::swap(first, second);
                    }
                    const NavExtent& yExt1 = first->GetExtent();
                    const NavExtent& yExt2 = second->GetExtent();

                    if (std::fabs(yExt1.hi.y - yExt2.lo.y) <= 0.25f &&
                        std::fabs(yExt1.lo.x - yExt2.lo.x) <= 0.25f &&
                        std::fabs(yExt1.hi.x - yExt2.hi.x) <= 0.25f) {
                        if (std::fabs(first->GetSWZ() - yExt2.lo.z) <= 2.5f &&
                            std::fabs(yExt1.hi.z - second->GetNEZ()) <= 2.5f) {
                            float l1 = yExt1.hi.y - yExt1.lo.y;
                            float l2 = yExt2.hi.y - yExt2.lo.y;
                            float lTot = l1 + l2;
                            if (lTot > 0.001f) {
                                float r = l1 / lTot;
                                float expW = yExt1.lo.z + (second->GetSWZ() - yExt1.lo.z) * r;
                                float expE = first->GetNEZ() + (yExt2.hi.z - first->GetNEZ()) * r;
                                if (std::fabs(first->GetSWZ() - expW) <= 2.5f &&
                                    std::fabs(yExt1.hi.z - expE) <= 2.5f) {
                                    isYMerge = true;
                                }
                            }
                        }
                    }
                }

                if (isXMerge || isYMerge) {
                    OptimizeMergeRecord rec;
                    rec.keepId = first->GetID();
                    rec.removeId = second->GetID();
                    rec.oldExtKeep = first->GetExtent();
                    rec.oldNeZKeep = first->GetNEZ();
                    rec.oldSwZKeep = first->GetSWZ();
                    rec.oldExtRemove = second->GetExtent();
                    rec.oldNeZRemove = second->GetNEZ();
                    rec.oldSwZRemove = second->GetSWZ();
                    rec.removeAttr = second->GetAttributes();
                    rec.removePlace = second->GetPlace();
                    rec.removePlaceName = second->GetPlaceName();

                    if (isXMerge) {
                        rec.mergedExt.lo = Vector3(first->GetExtent().lo.x, first->GetExtent().lo.y, first->GetExtent().lo.z);
                        rec.mergedExt.hi = Vector3(second->GetExtent().hi.x, second->GetExtent().hi.y, second->GetExtent().hi.z);
                        rec.mergedNeZ = second->GetNEZ();
                        rec.mergedSwZ = first->GetSWZ();
                    } else {
                        rec.mergedExt.lo = Vector3(first->GetExtent().lo.x, first->GetExtent().lo.y, first->GetExtent().lo.z);
                        rec.mergedExt.hi = Vector3(second->GetExtent().hi.x, second->GetExtent().hi.y, second->GetExtent().hi.z);
                        rec.mergedNeZ = first->GetNEZ();
                        rec.mergedSwZ = second->GetSWZ();
                    }

                    for (int d = 0; d < 4; ++d) {
                        for (const auto& conn : second->GetAdjacentList(static_cast<NavDirType>(d))) {
                            if (conn.area && conn.area->GetID() != rec.keepId && conn.area->GetID() != rec.removeId) {
                                rec.removeOutgoing.push_back({ conn.area->GetID(), static_cast<NavDirType>(d) });
                            }
                        }
                    }
                    for (const NavArea* other : m_nav->GetAreas()) {
                        if (other && other->GetID() != rec.removeId && other->GetID() != rec.keepId) {
                            for (int d = 0; d < 4; ++d) {
                                if (other->IsConnected(second, d)) {
                                    rec.removeIncoming.push_back({ other->GetID(), static_cast<NavDirType>(d) });
                                }
                            }
                        }
                    }

                    m_nav->GetGrid().RemoveArea(first);
                    first->SetExtent(rec.mergedExt);
                    first->SetCornerHeights(rec.mergedNeZ, rec.mergedSwZ);
                    m_nav->GetGrid().AddArea(first);

                    m_nav->RemoveArea(rec.removeId);

                    for (const auto& out : rec.removeOutgoing) {
                        m_nav->ConnectAreas(rec.keepId, out.targetId, false, out.dir);
                    }
                    for (const auto& in : rec.removeIncoming) {
                        m_nav->ConnectAreas(in.targetId, rec.keepId, false, in.dir);
                    }

                    allRecords.push_back(std::move(rec));
                    mergedAny = true;
                    break;
                }
            }
            if (mergedAny) break;
        }
    } while (mergedAny);

    stats.mergedCount = allRecords.size();
    stats.finalAreaCount = m_nav->GetAreaCount();

    if (!allRecords.empty()) {
        for (auto it = allRecords.rbegin(); it != allRecords.rend(); ++it) {
            const auto& rec = *it;
            NavArea* keep = m_nav->GetAreaByID(rec.keepId);
            if (keep) {
                m_nav->GetGrid().RemoveArea(keep);
                keep->SetExtent(rec.oldExtKeep);
                keep->SetCornerHeights(rec.oldNeZKeep, rec.oldSwZKeep);
                m_nav->GetGrid().AddArea(keep);
            }
            NavArea* recreat = m_nav->CreateArea(rec.oldExtRemove, rec.oldNeZRemove, rec.oldSwZRemove);
            if (recreat) {
                m_nav->GetGrid().RemoveArea(recreat);
                recreat->SetID(rec.removeId);
                m_nav->GetGrid().AddArea(recreat);
                recreat->SetAttributes(rec.removeAttr);
                recreat->SetPlace(rec.removePlace);
                recreat->SetPlaceName(rec.removePlaceName);

                for (const auto& out : rec.removeOutgoing) {
                    m_nav->ConnectAreas(rec.removeId, out.targetId, false, out.dir);
                }
                for (const auto& in : rec.removeIncoming) {
                    m_nav->ConnectAreas(in.targetId, rec.removeId, false, in.dir);
                }
            }
        }

        cmdMgr.ExecuteCommand(std::make_unique<CmdOptimizeMesh>(this, std::move(allRecords)));
    }

    return stats;
}

void EditorScene::StartKnifeMode() {
    m_isKnifeMode = true;
    m_knifeActive = false;
    m_knifeHoverAreaId = 0;
    m_knifeForceAxis = false;
    m_knifeAxisOverride = false;
    m_knifeAngle = KNIFE_ANGLE_0;
    ExitDrawAreaMode();
    ExitFillAreaMode();
    CancelBridgeMode();
}

void EditorScene::ExitKnifeMode() {
    m_isKnifeMode = false;
    m_knifeActive = false;
    m_knifeHoverAreaId = 0;
    m_knifeForceAxis = false;
    m_knifeAxisOverride = false;
    m_knifeAngle = KNIFE_ANGLE_0;
}

void EditorScene::ToggleKnifeMode() {
    if (m_isKnifeMode) {
        ExitKnifeMode();
    } else {
        StartKnifeMode();
    }
}

void EditorScene::SetKnifeAngle(KnifeCutAngle angle) {
    m_knifeAngle = angle;
    m_knifeForceAxis = true;
}

void EditorScene::CycleKnifeAngle() {
    m_knifeForceAxis = true;
    if (m_knifeAngle == KNIFE_ANGLE_0) m_knifeAngle = KNIFE_ANGLE_45;
    else if (m_knifeAngle == KNIFE_ANGLE_45) m_knifeAngle = KNIFE_ANGLE_90;
    else if (m_knifeAngle == KNIFE_ANGLE_90) m_knifeAngle = KNIFE_ANGLE_135;
    else m_knifeAngle = KNIFE_ANGLE_0;
}

void EditorScene::UpdateKnife(const Ray& ray) {
    if (!m_isKnifeMode || !m_nav || !m_nav->IsLoaded()) {
        m_knifeHoverAreaId = 0;
        return;
    }

    uint32_t hitId = ScenePicker::PickNavArea(*this, ray);
    m_knifeHoverAreaId = hitId;
    if (hitId == 0) return;

    NavArea* area = m_nav->GetAreaByID(hitId);
    if (!area) {
        m_knifeHoverAreaId = 0;
        return;
    }

    Vector3 hitPos = area->GetCenter();
    if (std::abs(ray.direction.z) > 1e-4f) {
        float t = (area->GetCenter().z - ray.origin.z) / ray.direction.z;
        hitPos = ray.origin + ray.direction * t;
    }

    const NavExtent& ext = area->GetExtent();
    float rawX = hitPos.x;
    float rawY = hitPos.y;
    if (m_gridSnap) {
        rawX = SnapValue(rawX);
        rawY = SnapValue(rawY);
    }

    if (!m_knifeForceAxis) {
        float w = ext.hi.x - ext.lo.x;
        float l = ext.hi.y - ext.lo.y;
        m_knifeAngle = (l >= w) ? KNIFE_ANGLE_0 : KNIFE_ANGLE_90;
    }

    m_knifeSplitCoordX = std::max(ext.lo.x + 4.0f, std::min(ext.hi.x - 4.0f, rawX));
    m_knifeSplitCoordY = std::max(ext.lo.y + 4.0f, std::min(ext.hi.y - 4.0f, rawY));
    m_knifeSplitCoord = (m_knifeAngle == KNIFE_ANGLE_0) ? m_knifeSplitCoordY : m_knifeSplitCoordX;
    m_knifeSplitAlongY = (m_knifeAngle == KNIFE_ANGLE_0);
}

void EditorScene::OnKnifeClick(const Ray& ray, CommandManager& cmdMgr) {
    if (!m_isKnifeMode) return;
    UpdateKnife(ray);
    if (m_knifeHoverAreaId != 0) {
        if (m_knifeAngle == KNIFE_ANGLE_0) {
            cmdMgr.ExecuteCommand(std::make_unique<CmdSplitAreaKnife>(this, m_knifeHoverAreaId, m_knifeSplitCoordY, true));
        } else if (m_knifeAngle == KNIFE_ANGLE_90) {
            cmdMgr.ExecuteCommand(std::make_unique<CmdSplitAreaKnife>(this, m_knifeHoverAreaId, m_knifeSplitCoordX, false));
        } else if (m_knifeAngle == KNIFE_ANGLE_45) {
            cmdMgr.ExecuteCommand(std::make_unique<CmdSplitAreaQuadKnife>(this, m_knifeHoverAreaId, m_knifeSplitCoordX, m_knifeSplitCoordY, true));
        } else if (m_knifeAngle == KNIFE_ANGLE_135) {
            cmdMgr.ExecuteCommand(std::make_unique<CmdSplitAreaQuadKnife>(this, m_knifeHoverAreaId, m_knifeSplitCoordX, m_knifeSplitCoordY, false));
        }
    }
}

bool EditorScene::CopySelectedAreas() {
    if (!m_nav || !m_nav->IsLoaded()) return false;

    std::vector<uint32_t> targets;
    if (!m_selectedAreaIds.empty()) {
        targets = m_selectedAreaIds;
    } else if (m_selectedAreaId != 0) {
        targets.push_back(m_selectedAreaId);
    }
    if (targets.empty()) return false;

    std::vector<NavArea*> areaList;
    for (uint32_t id : targets) {
        NavArea* a = m_nav->GetAreaByID(id);
        if (a) areaList.push_back(a);
    }
    if (areaList.empty()) return false;

    std::ostringstream ss;
    ss << "{\n";
    ss << "  \"navstudio_clipboard\": \"areas\",\n";
    ss << "  \"version\": 1,\n";
    ss << "  \"count\": " << areaList.size() << ",\n";
    ss << "  \"areas\": [\n";

    for (size_t i = 0; i < areaList.size(); ++i) {
        NavArea* a = areaList[i];
        const NavExtent& ext = a->GetExtent();
        ss << "    {\n";
        ss << "      \"lo\": [" << ext.lo.x << ", " << ext.lo.y << ", " << ext.lo.z << "],\n";
        ss << "      \"hi\": [" << ext.hi.x << ", " << ext.hi.y << ", " << ext.hi.z << "],\n";
        ss << "      \"neZ\": " << a->GetNEZ() << ",\n";
        ss << "      \"swZ\": " << a->GetSWZ() << ",\n";
        ss << "      \"attributes\": " << static_cast<int>(a->GetAttributes()) << ",\n";
        ss << "      \"place\": " << a->GetPlace() << ",\n";
        ss << "      \"placeName\": \"" << a->GetPlaceName() << "\",\n";
        ss << "      \"connections\": [";

        bool firstConn = true;
        for (int d = 0; d < 4; ++d) {
            for (const auto& conn : a->GetAdjacentList(static_cast<NavDirType>(d))) {
                if (!conn.area) continue;
                for (size_t j = 0; j < areaList.size(); ++j) {
                    if (areaList[j]->GetID() == conn.area->GetID()) {
                        if (!firstConn) ss << ", ";
                        ss << "{\"to\": " << j << ", \"dir\": " << d << "}";
                        firstConn = false;
                        break;
                    }
                }
            }
        }
        ss << "]\n";
        ss << "    }" << (i + 1 < areaList.size() ? ",\n" : "\n");
    }

    ss << "  ]\n";
    ss << "}\n";

    std::string jsonStr = ss.str();
    ImGui::SetClipboardText(jsonStr.c_str());
    return true;
}

bool EditorScene::PasteAreas(const Vector3* targetPos, CommandManager& cmdMgr) {
    if (!m_nav) return false;
    const char* clipText = ImGui::GetClipboardText();
    if (!clipText || std::string(clipText).find("\"navstudio_clipboard\": \"areas\"") == std::string::npos) {
        return false;
    }

    std::string str(clipText);
    std::vector<CmdPasteAreas::CopiedAreaData> copiedList;

    size_t pos = 0;
    Vector3 minLo(1e9f, 1e9f, 1e9f);
    Vector3 maxHi(-1e9f, -1e9f, -1e9f);

    while ((pos = str.find("\"lo\":", pos)) != std::string::npos) {
        CmdPasteAreas::CopiedAreaData data;
        float lox = 0, loy = 0, loz = 0;
        float hix = 0, hiy = 0, hiz = 0;
        float neZ = 0, swZ = 0;
        int attr = 0, place = 0;

        if (std::sscanf(str.c_str() + pos, "\"lo\": [%f, %f, %f]", &lox, &loy, &loz) == 3) {
            data.extent.lo = Vector3(lox, loy, loz);
        }
        size_t hiPos = str.find("\"hi\":", pos);
        if (hiPos != std::string::npos && std::sscanf(str.c_str() + hiPos, "\"hi\": [%f, %f, %f]", &hix, &hiy, &hiz) == 3) {
            data.extent.hi = Vector3(hix, hiy, hiz);
        }
        size_t nePos = str.find("\"neZ\":", pos);
        if (nePos != std::string::npos) std::sscanf(str.c_str() + nePos, "\"neZ\": %f", &neZ);
        size_t swPos = str.find("\"swZ\":", pos);
        if (swPos != std::string::npos) std::sscanf(str.c_str() + swPos, "\"swZ\": %f", &swZ);
        size_t attrPos = str.find("\"attributes\":", pos);
        if (attrPos != std::string::npos) std::sscanf(str.c_str() + attrPos, "\"attributes\": %d", &attr);
        size_t plPos = str.find("\"place\":", pos);
        if (plPos != std::string::npos) std::sscanf(str.c_str() + plPos, "\"place\": %d", &place);

        size_t namePos = str.find("\"placeName\": \"", pos);
        if (namePos != std::string::npos) {
            namePos += 14;
            size_t nameEnd = str.find('\"', namePos);
            if (nameEnd != std::string::npos) {
                data.placeName = str.substr(namePos, nameEnd - namePos);
            }
        }

        data.neZ = neZ;
        data.swZ = swZ;
        data.attributes = static_cast<uint8_t>(attr);
        data.place = static_cast<uint16_t>(place);

        size_t connPos = str.find("\"connections\": [", pos);
        size_t blockEnd = str.find('}', pos);
        if (connPos != std::string::npos && connPos < blockEnd) {
            size_t endConn = str.find(']', connPos);
            size_t cCur = connPos;
            while ((cCur = str.find("{\"to\":", cCur)) != std::string::npos && cCur < endConn) {
                int toIdx = 0, dir = 0;
                if (std::sscanf(str.c_str() + cCur, "{\"to\": %d, \"dir\": %d}", &toIdx, &dir) == 2) {
                    data.connections.push_back({ static_cast<size_t>(toIdx), static_cast<NavDirType>(dir) });
                }
                cCur += 6;
            }
        }

        minLo.x = std::min(minLo.x, data.extent.lo.x);
        minLo.y = std::min(minLo.y, data.extent.lo.y);
        minLo.z = std::min(minLo.z, data.extent.lo.z);
        maxHi.x = std::max(maxHi.x, data.extent.hi.x);
        maxHi.y = std::max(maxHi.y, data.extent.hi.y);
        maxHi.z = std::max(maxHi.z, data.extent.hi.z);

        copiedList.push_back(std::move(data));
        pos = (hiPos != std::string::npos) ? hiPos + 5 : pos + 5;
    }

    if (copiedList.empty()) return false;

    Vector3 groupCenter = (minLo + maxHi) * 0.5f;
    Vector3 offset(32.0f, 32.0f, 0.0f);
    if (targetPos) {
        offset = *targetPos - groupCenter;
        if (m_gridSnap) {
            offset.x = SnapValue(offset.x);
            offset.y = SnapValue(offset.y);
        }
    }

    cmdMgr.ExecuteCommand(std::make_unique<CmdPasteAreas>(this, copiedList, offset));
    return true;
}
