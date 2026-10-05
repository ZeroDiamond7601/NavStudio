#include "editor/scene/editor_scene.h"
#include <cstdio>

EditorScene::EditorScene()
    : m_selectedAreaId(0)
    , m_hoveredAreaId(0)
    , m_bspMode(BSP_RENDER_SOLID)
    , m_showBSP(true)
    , m_showNAV(true)
    , m_showConnections(true)
{
}

EditorScene::~EditorScene() {
}

bool EditorScene::LoadBSP(const std::string& bspPath) {
    if (!m_bsp.Load(bspPath)) {
        std::fprintf(stderr, "[EditorScene] Failed to load BSP: %s\n", bspPath.c_str());
        return false;
    }

    m_bspPath = bspPath;
    m_bspRenderer.BuildFromBSP(m_bsp);

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
    if (!m_nav.Load(navPath)) {
        std::fprintf(stderr, "[EditorScene] Failed to load NAV: %s\n", navPath.c_str());
        return false;
    }

    m_navPath = navPath;
    m_selectedAreaId = 0;
    m_hoveredAreaId = 0;
    m_navRenderer.BuildFromNav(m_nav, m_selectedAreaId, m_hoveredAreaId);
    return true;
}

bool EditorScene::SaveNAV(const std::string& navPath) {
    std::string path = navPath.empty() ? m_navPath : navPath;
    if (path.empty()) {
        std::fprintf(stderr, "[EditorScene] Cannot save NAV: no target path specified\n");
        return false;
    }

    if (!m_nav.Save(path)) {
        std::fprintf(stderr, "[EditorScene] Failed to save NAV to %s\n", path.c_str());
        return false;
    }

    m_navPath = path;
    return true;
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
    if (m_selectedAreaId == 0 || !m_nav.IsLoaded()) return nullptr;
    return m_nav.GetAreaByID(m_selectedAreaId);
}

void EditorScene::RebuildNavRenderer() {
    if (m_nav.IsLoaded()) {
        m_navRenderer.BuildFromNav(m_nav, m_selectedAreaId, m_hoveredAreaId);
    }
}

void EditorScene::Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp) {
    if (m_showBSP && m_bspRenderer.IsLoaded()) {
        m_bspRenderer.Render(meshShader, lineShader, mvp, m_bspMode);
    }

    if (m_showNAV && m_navRenderer.IsLoaded()) {
        m_navRenderer.Render(meshShader, lineShader, mvp);
    }
}
