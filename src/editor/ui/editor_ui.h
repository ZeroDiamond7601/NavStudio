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
    void ApplyTheme(int themeIndex);
    void ApplyPreferencesToRuntime(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void Render(EditorScene& scene, Camera& camera, CommandManager& cmdMgr, float deltaTime);

    bool IsMouseOverUI() const { return m_mouseOverUI; }
    bool RequestQuit() const { return m_requestQuit; }
    void PromptQuit(EditorScene& scene, CommandManager& cmdMgr);

    enum PendingAction {
        PENDING_NONE = 0,
        PENDING_QUIT,
        PENDING_OPEN_BSP,
        PENDING_OPEN_NAV,
        PENDING_RECENT,
        PENDING_UNLOAD_NAV,
        PENDING_UNLOAD_BSP
    };

    bool CheckUnsavedChanges(EditorScene& scene, CommandManager& cmdMgr, PendingAction action, const std::string& path = "");
    void ExecutePendingAction(EditorScene& scene, CommandManager& cmdMgr);
    void OpenPreferences() { m_showPreferencesModal = true; }

private:
    void RenderMenuBar(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderToolPalette(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderHierarchy(EditorScene& scene, Camera& camera);
    void RenderInspector(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderConnectionInspector(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderStatusBar(const EditorScene& scene, const Camera& camera);
    void RenderHelpModal();
    void RenderPreferencesModal(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderWelcomeOverlay(EditorScene& scene);
    void RenderOpenPathModal(EditorScene& scene);
    void RenderLoadingModal(const EditorScene& scene);
    void RenderLoadingErrorModal(EditorScene& scene);
    void RenderTransformHUD(EditorScene& scene, CommandManager& cmdMgr);
    void RenderEntityHierarchy(EditorScene& scene, Camera& camera);
    void RenderEntityInspector(EditorScene& scene, Camera& camera);
    void RenderGenerateModal(EditorScene& scene);
    void RenderBatchGenerateModal(EditorScene& scene);
    void RenderUnsavedModal(EditorScene& scene, CommandManager& cmdMgr);
    void RenderSaveSuccessModal();
    void RenderAnalyzerModal();
    void RenderOptimizeModal();
    void RenderNavMeshGlobalInspector(EditorScene& scene, CommandManager& cmdMgr);
    void RenderBSPGlobalInspector(EditorScene& scene);
    void RenderStatsOverlay(const EditorScene& scene, const Camera& camera);

    bool m_mouseOverUI;
    bool m_requestQuit;
    bool m_showHelpModal;
    bool m_showOpenPathModal;
    bool m_showGenerateModal{false};
    bool m_showBatchGenerateModal{false};
    char m_searchFilter[64];
    char m_entityFilter[64];
    int m_entityCategoryFilter{-1};
    char m_placeEditBuffer[64];
    char m_openPathBuffer[512];
    int m_openPathType; // 0 = BSP, 1 = NAV
    std::string m_openPathStatusMessage;

    NavGenerateOptions m_genOptions;
    std::string m_generateStatusText;

    char m_batchMapDirBuffer[512];
    char m_batchOutDirBuffer[512];
    bool m_batchOverwrite{false};
    bool m_batchRecursive{false};
    int m_batchThreads{0};
    bool m_batchRunning{false};
    std::vector<NavGenerator::BatchItem> m_batchItems;
    size_t m_batchCompletedCount{0};
    size_t m_batchTotalCount{0};

    int m_connectTargetInputId{0};
    int m_connectDirSelection{0};
    bool m_connectBidirectional{true};

    PendingAction m_pendingAction{PENDING_NONE};
    std::string m_pendingPath;
    bool m_showUnsavedModal{false};
    bool m_showSaveSuccessModal{false};
    std::string m_saveSuccessMessage;

    bool m_showAnalyzerModal{false};
    EditorScene::AnalyzerStats m_analyzerStats;
    bool m_showOptimizeModal{false};
    EditorScene::OptimizeMeshStats m_optimizeStats;
    bool m_showStatsOverlay{true};
    bool m_showPreferencesModal{false};
};

#endif // EDITOR_UI_H
