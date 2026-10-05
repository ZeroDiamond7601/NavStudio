#ifndef EDITOR_UI_H
#define EDITOR_UI_H

#include "editor/scene/editor_scene.h"
#include "editor/camera/camera.h"
#include "editor/commands/command.h"
#include <string>

class EditorUI {
public:
    EditorUI();
    ~EditorUI();

    void Init();
    void Render(EditorScene& scene, Camera& camera, CommandManager& cmdMgr, float deltaTime);

    bool IsMouseOverUI() const { return m_mouseOverUI; }
    bool RequestQuit() const { return m_requestQuit; }

    const std::string& GetOpenBSPRequested() const { return m_openBSPRequested; }
    void ClearOpenBSPRequested() { m_openBSPRequested.clear(); }

    const std::string& GetOpenNAVRequested() const { return m_openNAVRequested; }
    void ClearOpenNAVRequested() { m_openNAVRequested.clear(); }

private:
    void RenderMenuBar(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderToolPalette(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderHierarchy(EditorScene& scene, Camera& camera);
    void RenderInspector(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderStatusBar(const EditorScene& scene, const Camera& camera);
    void RenderHelpModal();

    bool m_mouseOverUI;
    bool m_requestQuit;
    bool m_showHelpModal;
    char m_searchFilter[64];
    char m_placeEditBuffer[64];
    std::string m_openBSPRequested;
    std::string m_openNAVRequested;
};

#endif // EDITOR_UI_H
