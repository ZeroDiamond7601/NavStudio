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
    void OpenIslandModal() { m_showIslandModal = true; }
    void OpenLandmarksModal() { m_showLandmarksModal = true; }
    void ToggleCommandPalette() {
        m_showCommandPalette = !m_showCommandPalette;
        m_commandPaletteFilter[0] = '\0';
        m_commandPaletteFocus = true;
        m_commandPaletteSelectedIndex = 0;
    }
    void TogglePathPanel() { m_showPathPanel = !m_showPathPanel; }
    void OpenFindModal() { m_showFindModal = true; }
    void OpenDiagnosticsModal() { m_showDiagnosticsModal = true; }
    void OpenLadderCreateModal() { m_showLadderCreateModal = true; }
    void OpenPawnExportModal() { m_showPawnExportModal = true; }
    void OpenWaypointExportModal() { m_showWaypointExportModal = true; }
    void OpenNavToWaypointModal() { m_showNavToWaypointModal = true; }
    void SetMarqueeBox(bool active, float startX, float startY, float curX, float curY) {
        m_marqueeActive = active;
        m_marqueeStartX = startX;
        m_marqueeStartY = startY;
        m_marqueeCurX = curX;
        m_marqueeCurY = curY;
    }

private:
    void RenderMenuBar(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderToolPalette(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderHierarchy(EditorScene& scene, Camera& camera);
    void RenderInspector(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderAreaInspector(EditorScene& scene, Camera& camera, CommandManager& cmdMgr, class NavArea* area);
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

    // Navigation Testing & Productivity Tools
    void RenderPathSimulationPanel(EditorScene& scene, Camera& camera);
    void RenderIslandModal(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderClearanceHUD(EditorScene& scene);
    void RenderViewportCompass(EditorScene& scene, Camera& camera, float screenW, float screenH);
    void RenderCommandPalette(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderLandmarksModal(EditorScene& scene, Camera& camera);
    void RenderAutosaveRecoveryModal(EditorScene& scene, CommandManager& cmdMgr);
    void RenderMarqueeBox();

    // Ladders, Diagnostics & Workflow UI
    void RenderLadderInspector(EditorScene& scene, CommandManager& cmdMgr);
    void RenderDiagnosticsModal(EditorScene& scene, Camera& camera, CommandManager& cmdMgr);
    void RenderFindModal(EditorScene& scene, Camera& camera);
    void RenderPawnExportModal(EditorScene& scene);
    void RenderLadderCreateModal(EditorScene& scene, Camera& camera);
    void RenderToastHUD(const EditorScene& scene);

    // Bot Waypoint System UI
    void RenderWaypointInspector(EditorScene& scene, CommandManager& cmdMgr);
    void RenderWaypointExportModal(EditorScene& scene);
    void RenderNavToWaypointModal(EditorScene& scene);

    bool m_marqueeActive{false};
    float m_marqueeStartX{0.0f};
    float m_marqueeStartY{0.0f};
    float m_marqueeCurX{0.0f};
    float m_marqueeCurY{0.0f};

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

    // Diagnostics & Productivity States
    bool m_showPathPanel{false};
    bool m_showIslandModal{false};
    bool m_showCommandPalette{false};
    bool m_commandPaletteFocus{false};
    char m_commandPaletteFilter[128]{""};
    int m_commandPaletteSelectedIndex{0};
    bool m_showLandmarksModal{false};
    char m_landmarkFilter[64]{""};
    bool m_showRecoveryModal{false};
    std::string m_recoveryBakPath;
    std::string m_recoveryNavPath;
    int64_t m_recoveryBakAgeSec{0};
    std::string m_lastCheckedRecoveryMap;

    // Diagnostics, Find, Pawn & Ladder Modals
    bool m_showDiagnosticsModal{false};
    bool m_showFindModal{false};
    char m_findFilter[64]{""};
    bool m_showPawnExportModal{false};
    bool m_pawnExportSelectedOnly{false};
    bool m_showLadderCreateModal{false};
    float m_ladderCreateWidth{32.0f};
    int m_ladderCreateDir{0};

    // Bot Waypoint UI State
    bool m_showWaypointExportModal{false};
    bool m_showNavToWaypointModal{false};
    int m_waypointExportBot{0}; // 0=EBot, 1=SyPB, 2=YaPB, 3=PODBot
    int m_waypointExportMod{0}; // 0=Standard, 1=ZombiePlague, 2=Deathmatch
    char m_waypointExportPath[512]{""};
    char m_waypointFilter[64]{""};
};

#endif // EDITOR_UI_H
