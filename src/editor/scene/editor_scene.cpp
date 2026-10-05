#include "editor/scene/editor_scene.h"
#include <cstdio>
#include <fstream>
#include <algorithm>
#include <cctype>

EditorScene::EditorScene()
    : m_bsp(std::make_unique<BSPFile>())
    , m_nav(std::make_unique<NavMesh>())
    , m_selectedAreaId(0)
    , m_hoveredAreaId(0)
    , m_bspMode(BSP_RENDER_SOLID)
    , m_showBSP(true)
    , m_showNAV(true)
    , m_showConnections(true)
{
}

EditorScene::~EditorScene() {
    if (m_loadThread.joinable()) {
        m_loadThread.join();
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
    m_bspRenderer.BuildFromBSP(*m_bsp);

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
    m_navRenderer.BuildFromNav(*m_nav, m_selectedAreaId, m_hoveredAreaId);
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
                m_bspRenderer.BuildFromBSP(*m_bsp);
            }

            if (m_loadCtx.loadedNav) {
                m_nav = std::move(m_loadCtx.loadedNav);
                m_navPath = m_loadCtx.targetNavPath;
                m_selectedAreaId = 0;
                m_hoveredAreaId = 0;
                m_navRenderer.BuildFromNav(*m_nav, 0, 0);
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
    RebuildNavRenderer();
}

void EditorScene::SetHoveredArea(uint32_t id) {
    if (m_hoveredAreaId == id) return;
    m_hoveredAreaId = id;
    RebuildNavRenderer();
}

NavArea* EditorScene::GetSelectedArea() {
    if (m_selectedAreaId == 0 || !m_nav || !m_nav->IsLoaded()) return nullptr;
    return m_nav->GetAreaByID(m_selectedAreaId);
}

void EditorScene::RebuildNavRenderer() {
    if (m_nav && m_nav->IsLoaded()) {
        m_navRenderer.BuildFromNav(*m_nav, m_selectedAreaId, m_hoveredAreaId);
    }
}

void EditorScene::Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp, const Vector3& camPos) {
    if (m_showBSP && m_bspRenderer.IsLoaded()) {
        m_bspRenderer.Render(meshShader, lineShader, mvp, m_bspMode, camPos);
    }

    if (m_showNAV && m_navRenderer.IsLoaded()) {
        m_navRenderer.Render(meshShader, lineShader, mvp);
    }
}
