#ifndef EDITOR_SCENE_H
#define EDITOR_SCENE_H

#include <string>
#include "bsp/bsp_file.h"
#include "nav/nav_file.h"
#include "editor/render/bsp_renderer.h"
#include "editor/render/nav_renderer.h"

class EditorScene {
public:
    EditorScene();
    ~EditorScene();

    bool LoadBSP(const std::string& bspPath);
    bool LoadNAV(const std::string& navPath);
    bool SaveNAV(const std::string& navPath = "");

    void SelectArea(uint32_t id);
    void SetHoveredArea(uint32_t id);
    NavArea* GetSelectedArea();
    uint32_t GetSelectedAreaID() const { return m_selectedAreaId; }

    void Render(const Shader& meshShader, const Shader& lineShader, const Matrix4& mvp);
    void RebuildNavRenderer();

    const BSPFile& GetBSP() const { return m_bsp; }
    BSPFile& GetBSP() { return m_bsp; }

    const NavMesh& GetNAV() const { return m_nav; }
    NavMesh& GetNAV() { return m_nav; }

    const std::string& GetBSPPath() const { return m_bspPath; }
    const std::string& GetNAVPath() const { return m_navPath; }

    bool HasBSP() const { return m_bsp.IsLoaded(); }
    bool HasNAV() const { return m_nav.IsLoaded(); }

    BSPRenderMode GetBSPMode() const { return m_bspMode; }
    void SetBSPMode(BSPRenderMode mode) { m_bspMode = mode; }

    bool GetShowBSP() const { return m_showBSP; }
    void SetShowBSP(bool show) { m_showBSP = show; }

    bool GetShowNAV() const { return m_showNAV; }
    void SetShowNAV(bool show) { m_showNAV = show; }

    bool GetShowConnections() const { return m_showConnections; }
    void SetShowConnections(bool show) { m_showConnections = show; }

private:
    BSPFile m_bsp;
    NavMesh m_nav;
    BSPRenderer m_bspRenderer;
    NavRenderer m_navRenderer;

    std::string m_bspPath;
    std::string m_navPath;

    uint32_t m_selectedAreaId;
    uint32_t m_hoveredAreaId;

    BSPRenderMode m_bspMode;
    bool m_showBSP;
    bool m_showNAV;
    bool m_showConnections;
};

#endif // EDITOR_SCENE_H
