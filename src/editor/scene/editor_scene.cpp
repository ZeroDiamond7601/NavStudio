#include "editor/scene/editor_scene.h"
#include "editor/commands/nav_commands.h"
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cmath>

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

void EditorScene::SetShowGroundGrid(bool show) {
    m_showGroundGrid = show;
}

void EditorScene::SnapGridElevationToSelection() {
    NavArea* sel = GetSelectedArea();
    if (sel) {
        m_gridElevation = sel->GetCenter().z;
        return;
    }
    const EditorEntity* ent = GetSelectedEntity();
    if (ent) {
        m_gridElevation = ent->origin.z;
        return;
    }
}

void EditorScene::SnapGridElevationToFloorUnderCamera(const Vector3& camPos) {
    if (m_bsp) {
        Vector3 groundPos;
        if (m_bsp->GetGround(camPos, &groundPos, 4096.0f)) {
            m_gridElevation = groundPos.z;
            return;
        }
    }
    m_gridElevation = SnapValue(camPos.z);
}

float EditorScene::SnapValue(float val) const {
    if (!m_gridSnap || m_gridSize < 1.0f) return val;
    return std::round(val / m_gridSize) * m_gridSize;
}

Vector3 EditorScene::SnapVector(const Vector3& v) const {
    return Vector3(SnapValue(v.x), SnapValue(v.y), SnapValue(v.z));
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

    // Set initial grid elevation to player spawn floor
    for (const auto& ent : m_bsp->GetEntities()) {
        if (ent.classname == "info_player_start" || ent.classname == "info_player_deathmatch") {
            Vector3 entOrigin;
            if (ent.GetOrigin(entOrigin)) {
                Vector3 gPos;
                if (m_bsp->GetGround(entOrigin, &gPos, 1024.0f)) {
                    m_gridElevation = gPos.z;
                } else {
                    m_gridElevation = entOrigin.z;
                }
                break;
            }
        }
    }

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
    AddRecentFile(navPath);
    RebuildNavRenderer();
    return true;
}

bool EditorScene::SaveNAV(const std::string& navPath) {
    std::string path = navPath.empty() ? m_navPath : navPath;
    if (path.empty()) {
        std::fprintf(stderr, "[EditorScene] Cannot save NAV: no target path specified\n");
        return false;
    }

    if (!m_nav || !m_nav->Save(path)) {
        std::fprintf(stderr, "[EditorScene] Failed to save NAV to %s\n", path.c_str());
        return false;
    }

    m_navPath = path;
    return true;
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
    RebuildNavRenderer();
    return true;
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
                m_bspRenderer.BuildFromBSP(*m_bsp, &m_textureManager);
                m_entityRenderer.BuildFromBSP(*m_bsp);
                m_selectedEntityIndex = -1;
                AddRecentFile(m_bspPath);
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

void EditorScene::SelectArea(uint32_t id) {
    if (m_selectedAreaId == id) return;
    m_selectedAreaId = id;
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
    if (m_selectedAreaId == 0 || !m_nav || !m_nav->IsLoaded()) return nullptr;
    return m_nav->GetAreaByID(m_selectedAreaId);
}

const NavArea* EditorScene::GetSelectedArea() const {
    if (m_selectedAreaId == 0 || !m_nav || !m_nav->IsLoaded()) return nullptr;
    return m_nav->GetAreaByID(m_selectedAreaId);
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
            (m_isDraggingHandle ? m_draggedHandle : m_selectedHandle)
        );
    }
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

void EditorScene::Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp, const Vector3& camPos) {
    if (m_showBSP && m_bspRenderer.IsLoaded()) {
        m_bspRenderer.Render(meshShader, lineShader, mvp, m_bspMode, camPos);
    }

    if (m_showGroundGrid) {
        m_gridRenderer.Render(lineShader, mvp, camPos, m_gridSize, m_gridElevation);
    }

    if (m_entityRenderer.IsLoaded()) {
        m_entityRenderer.Render(meshShader, lineShader, mvp, m_selectedEntityIndex, camPos);
    }

    if (m_showNAV && m_navRenderer.IsLoaded()) {
        m_navRenderer.Render(meshShader, lineShader, mvp);
    }

    // 3D Transform Gizmo (Blender / Hammer style cones, rings, boxes)
    SelectedHandleType activeHandle = m_isDraggingHandle ? m_draggedHandle : m_selectedHandle;
    if (m_selectedAreaId != 0 && m_nav && m_nav->IsLoaded()) {
        const NavArea* sel = m_nav->GetAreaByID(m_selectedAreaId);
        if (sel) {
            Vector3 center = sel->GetCenter();
            center.z += 4.0f;
            GizmoMode effectiveMode = (m_gizmoMode == GIZMO_MODE_ROTATE) ? GIZMO_MODE_TRANSLATE : m_gizmoMode;
            m_gizmoRenderer.Render(lineShader, mvp, center, camPos, effectiveMode, m_hoveredHandle, activeHandle, false);
        }
    } else if (m_selectedEntityIndex >= 0 && m_entityRenderer.IsLoaded()) {
        const EditorEntity* selEnt = m_entityRenderer.GetEntity(m_selectedEntityIndex);
        if (selEnt) {
            m_gizmoRenderer.Render(lineShader, mvp, selEnt->origin, camPos, m_gizmoMode, m_hoveredHandle, activeHandle, true);
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

    if (area) {
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
            if (area) {
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
            if (area) {
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
            if (area) {
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
        if (area) {
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
        }
    } else if (area && m_draggedHandle == HANDLE_EDGE_NORTH) {
        float targetHiY = SnapValue(m_dragStartExtent.hi.y + groundDelta.y);
        targetHiY = std::max(m_dragStartExtent.lo.y + 8.0f, targetHiY);
        NavExtent nextExt = m_dragStartExtent;
        nextExt.hi.y = targetHiY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_EDGE_SOUTH) {
        float targetLoY = SnapValue(m_dragStartExtent.lo.y + groundDelta.y);
        targetLoY = std::min(m_dragStartExtent.hi.y - 8.0f, targetLoY);
        NavExtent nextExt = m_dragStartExtent;
        nextExt.lo.y = targetLoY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_EDGE_EAST) {
        float targetHiX = SnapValue(m_dragStartExtent.hi.x + groundDelta.x);
        targetHiX = std::max(m_dragStartExtent.lo.x + 8.0f, targetHiX);
        NavExtent nextExt = m_dragStartExtent;
        nextExt.hi.x = targetHiX;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_EDGE_WEST) {
        float targetLoX = SnapValue(m_dragStartExtent.lo.x + groundDelta.x);
        targetLoX = std::min(m_dragStartExtent.hi.x - 8.0f, targetLoX);
        NavExtent nextExt = m_dragStartExtent;
        nextExt.lo.x = targetLoX;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_CORNER_NW) {
        float targetLoX = std::min(m_dragStartExtent.hi.x - 8.0f, SnapValue(m_dragStartExtent.lo.x + groundDelta.x));
        float targetHiY = std::max(m_dragStartExtent.lo.y + 8.0f, SnapValue(m_dragStartExtent.hi.y + groundDelta.y));
        NavExtent nextExt = m_dragStartExtent;
        nextExt.lo.x = targetLoX;
        nextExt.hi.y = targetHiY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_CORNER_NE) {
        float targetHiX = std::max(m_dragStartExtent.lo.x + 8.0f, SnapValue(m_dragStartExtent.hi.x + groundDelta.x));
        float targetHiY = std::max(m_dragStartExtent.lo.y + 8.0f, SnapValue(m_dragStartExtent.hi.y + groundDelta.y));
        NavExtent nextExt = m_dragStartExtent;
        nextExt.hi.x = targetHiX;
        nextExt.hi.y = targetHiY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_CORNER_SE) {
        float targetHiX = std::max(m_dragStartExtent.lo.x + 8.0f, SnapValue(m_dragStartExtent.hi.x + groundDelta.x));
        float targetLoY = std::min(m_dragStartExtent.hi.y - 8.0f, SnapValue(m_dragStartExtent.lo.y + groundDelta.y));
        NavExtent nextExt = m_dragStartExtent;
        nextExt.hi.x = targetHiX;
        nextExt.lo.y = targetLoY;
        area->SetExtent(nextExt);
    } else if (area && m_draggedHandle == HANDLE_CORNER_SW) {
        float targetLoX = std::min(m_dragStartExtent.hi.x - 8.0f, SnapValue(m_dragStartExtent.lo.x + groundDelta.x));
        float targetLoY = std::min(m_dragStartExtent.hi.y - 8.0f, SnapValue(m_dragStartExtent.lo.y + groundDelta.y));
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

void EditorScene::DuplicateSelectedArea(CommandManager& cmdMgr) {
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

    cmdMgr.ExecuteCommand(std::make_unique<CmdSplitArea>(this, area->GetID(), splitAlongY));
}

void EditorScene::MergeSelectedArea(CommandManager& cmdMgr) {
    NavArea* area = GetSelectedArea();
    if (!area) return;

    uint32_t targetId = 0;
    for (int d = 0; d < 4; ++d) {
        const auto& conns = area->GetAdjacentList(static_cast<NavDirType>(d));
        for (const auto& c : conns) {
            if (c.area && c.area->GetID() != area->GetID()) {
                targetId = c.area->GetID();
                break;
            }
        }
        if (targetId != 0) break;
    }

    if (targetId != 0) {
        cmdMgr.ExecuteCommand(std::make_unique<CmdMergeAreas>(this, area->GetID(), targetId));
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
