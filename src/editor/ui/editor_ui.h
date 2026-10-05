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

private:
    void RenderMenuBar(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderToolPalette(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderHierarchy(EditorScene& scene, Camera& camera);
    void RenderInspector(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderStatusBar(const EditorScene& scene, const Camera& camera);
    void RenderHelpModal();
    void RenderWelcomeOverlay(EditorScene& scene);
    void RenderOpenPathModal(EditorScene& scene);
    void RenderLoadingModal(const EditorScene& scene);
    void RenderLoadingErrorModal(EditorScene& scene);

    bool m_mouseOverUI;
    bool m_requestQuit;
    bool m_showHelpModal;
    bool m_showOpenPathModal;
    char m_searchFilter[64];
    char m_placeEditBuffer[64];
    char m_openPathBuffer[512];
    int m_openPathType; // 0 = BSP, 1 = NAV
    std::string m_openPathStatusMessage;
};

#endif // EDITOR_UI_H
