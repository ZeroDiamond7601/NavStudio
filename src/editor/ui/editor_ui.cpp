#include "editor/ui/editor_ui.h"
#include <imgui.h>
#include "editor/commands/nav_commands.h"
#include "editor/ui/file_dialog.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

EditorUI::EditorUI()
    : m_mouseOverUI(false)
    , m_requestQuit(false)
    , m_showHelpModal(false)
    , m_showOpenPathModal(false)
    , m_showGenerateModal(false)
    , m_showBatchGenerateModal(false)
    , m_openPathType(0)
    , m_batchOverwrite(false)
    , m_batchRecursive(false)
    , m_batchThreads(std::max(1u, std::thread::hardware_concurrency()))
    , m_batchRunning(false)
    , m_batchCompletedCount(0)
    , m_batchTotalCount(0)
{
    m_searchFilter[0] = '\0';
    m_entityFilter[0] = '\0';
    m_entityCategoryFilter = -1;
    m_placeEditBuffer[0] = '\0';
    m_openPathBuffer[0] = '\0';
    m_batchMapDirBuffer[0] = '\0';
    m_batchOutDirBuffer[0] = '\0';

    // Auto-detect default Half-Life / Counter-Strike maps path
    const std::string defaultMapsPath = "C:\\Program Files (x86)\\Steam\\steamapps\\common\\Half-Life\\cstrike\\maps";
    if (fs::exists(defaultMapsPath)) {
        std::strncpy(m_batchMapDirBuffer, defaultMapsPath.c_str(), sizeof(m_batchMapDirBuffer) - 1);
        m_batchMapDirBuffer[sizeof(m_batchMapDirBuffer) - 1] = '\0';
    }
}

EditorUI::~EditorUI() {
}

void EditorUI::Init() {
    // Customize ImGui theme to match dark studio aesthetics
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 4.0f;
    style.TabRounding = 3.0f;

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_WindowBg] = ImVec4(0.14f, 0.14f, 0.16f, 0.94f);
    colors[ImGuiCol_Header] = ImVec4(0.24f, 0.26f, 0.32f, 1.0f);
    colors[ImGuiCol_HeaderHovered] = ImVec4(0.32f, 0.36f, 0.44f, 1.0f);
    colors[ImGuiCol_HeaderActive] = ImVec4(0.40f, 0.44f, 0.54f, 1.0f);
    colors[ImGuiCol_Button] = ImVec4(0.22f, 0.25f, 0.32f, 1.0f);
    colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.35f, 0.45f, 1.0f);
    colors[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.45f, 0.58f, 1.0f);
    colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.19f, 0.22f, 1.0f);
    colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.26f, 0.30f, 1.0f);
    colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.30f, 0.36f, 1.0f);
    colors[ImGuiCol_Tab] = ImVec4(0.18f, 0.20f, 0.24f, 1.0f);
    colors[ImGuiCol_TabHovered] = ImVec4(0.32f, 0.36f, 0.44f, 1.0f);
    colors[ImGuiCol_TabActive] = ImVec4(0.26f, 0.30f, 0.38f, 1.0f);
    colors[ImGuiCol_TitleBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.0f);
    colors[ImGuiCol_TitleBgActive] = ImVec4(0.18f, 0.20f, 0.25f, 1.0f);
}

void EditorUI::Render(EditorScene& scene, Camera& camera, CommandManager& cmdMgr, float /*deltaTime*/) {
    ImGuiIO& io = ImGui::GetIO();
    m_mouseOverUI = io.WantCaptureMouse;

    // Handle global shortcuts when not typing into input fields
    if (!io.WantTextInput) {
        if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            std::string path = FileDialog::OpenFile(FileDialog::kBSPFilter, "Open GoldSrc BSP Map");
            if (!path.empty()) {
                if (!CheckUnsavedChanges(scene, cmdMgr, PENDING_OPEN_BSP, path)) {
                    cmdMgr.Clear();
                    scene.StartAsyncLoad(path);
                }
            }
        } else if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            std::string path = FileDialog::OpenFile(FileDialog::kNAVFilter, "Open Navigation Mesh");
            if (!path.empty()) {
                if (!CheckUnsavedChanges(scene, cmdMgr, PENDING_OPEN_NAV, path)) {
                    cmdMgr.Clear();
                    scene.StartAsyncLoad(path);
                }
            }
        } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false) && scene.HasNAV()) {
            bool saved = false;
            std::string saveTarget = scene.GetNAVPath();
            if (!saveTarget.empty()) {
                saved = scene.SaveNAV();
            } else {
                std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh");
                if (!path.empty()) {
                    saved = scene.SaveNAV(path);
                    saveTarget = path;
                }
            }
            if (saved) {
                cmdMgr.MarkSaved();
                scene.SetModified(false);
                m_showSaveSuccessModal = true;
                m_saveSuccessMessage = "Navigation mesh saved successfully:\n" + saveTarget;
            }
        } else if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false) && scene.HasNAV()) {
            std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh As");
            if (!path.empty()) {
                if (scene.SaveNAV(path)) {
                    cmdMgr.MarkSaved();
                    scene.SetModified(false);
                    m_showSaveSuccessModal = true;
                    m_saveSuccessMessage = "Navigation mesh saved successfully:\n" + path;
                }
            }
        } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_G, false) && scene.HasBSP()) {
            m_showGenerateModal = true;
            m_generateStatusText.clear();
        }
    }

    // Build main dockspace over viewport
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("NavStudioDockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    ImGuiID dockspaceId = ImGui::GetID("NavStudioDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    // Render Panels
    RenderMenuBar(scene, camera, cmdMgr);
    RenderToolPalette(scene, camera, cmdMgr);
    RenderHierarchy(scene, camera);
    RenderInspector(scene, camera, cmdMgr);
    RenderStatusBar(scene, camera);
    RenderTransformHUD(scene, cmdMgr);

    if (scene.IsLoading()) {
        RenderLoadingModal(scene);
    } else if (scene.HasLoadingError()) {
        RenderLoadingErrorModal(scene);
    }

    if (!scene.HasBSP() && !scene.HasNAV() && !scene.IsLoading()) {
        RenderWelcomeOverlay(scene);
    }

    if (m_showHelpModal) {
        RenderHelpModal();
    }

    if (m_showOpenPathModal) {
        RenderOpenPathModal(scene);
    }

    if (m_showGenerateModal) {
        RenderGenerateModal(scene);
    }

    if (m_showBatchGenerateModal) {
        RenderBatchGenerateModal(scene);
    }

    RenderUnsavedModal(scene, cmdMgr);
    RenderSaveSuccessModal();

    if (m_showAnalyzerModal) {
        RenderAnalyzerModal();
    }
    if (m_showOptimizeModal) {
        RenderOptimizeModal();
    }
}

void EditorUI::RenderMenuBar(EditorScene& scene, Camera& camera, CommandManager& cmdMgr) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open BSP Map...", "Ctrl+O")) {
                std::string path = FileDialog::OpenFile(FileDialog::kBSPFilter, "Open GoldSrc BSP Map");
                if (!path.empty()) {
                    if (!CheckUnsavedChanges(scene, cmdMgr, PENDING_OPEN_BSP, path)) {
                        cmdMgr.Clear();
                        scene.StartAsyncLoad(path);
                    }
                }
            }
            if (ImGui::MenuItem("Open NAV Mesh...", "Ctrl+Shift+O")) {
                std::string path = FileDialog::OpenFile(FileDialog::kNAVFilter, "Open Navigation Mesh");
                if (!path.empty()) {
                    if (!CheckUnsavedChanges(scene, cmdMgr, PENDING_OPEN_NAV, path)) {
                        cmdMgr.Clear();
                        scene.StartAsyncLoad(path);
                    }
                }
            }

            if (ImGui::BeginMenu("Open Recent")) {
                const auto& recents = scene.GetRecentFiles();
                if (recents.empty()) {
                    ImGui::MenuItem("No Recent Files", nullptr, false, false);
                } else {
                    for (const auto& rPath : recents) {
                        std::string label = rPath;
                        size_t lastSlash = label.find_last_of("/\\");
                        std::string nameOnly = (lastSlash != std::string::npos) ? label.substr(lastSlash + 1) : label;
                        std::string itemText = nameOnly + "  (" + label + ")";
                        if (ImGui::MenuItem(itemText.c_str())) {
                            if (!CheckUnsavedChanges(scene, cmdMgr, PENDING_RECENT, rPath)) {
                                cmdMgr.Clear();
                                scene.StartAsyncLoad(rPath);
                            }
                        }
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent Files")) {
                        scene.ClearRecentFiles();
                    }
                }
                ImGui::EndMenu();
            }

            if (ImGui::MenuItem("Open File from Path...", nullptr)) {
                m_showOpenPathModal = true;
                m_openPathBuffer[0] = '\0';
                m_openPathStatusMessage.clear();
            }
            if (ImGui::MenuItem("Load Texture WAD...", nullptr)) {
                std::string path = FileDialog::OpenFile(FileDialog::kWADFilter, "Open Texture WAD Archive");
                if (!path.empty()) {
                    scene.LoadWAD(path);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save NAV Mesh", "Ctrl+S", false, scene.HasNAV())) {
                bool saved = false;
                std::string saveTarget = scene.GetNAVPath();
                if (!saveTarget.empty()) {
                    saved = scene.SaveNAV();
                } else {
                    std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh");
                    if (!path.empty()) {
                        saved = scene.SaveNAV(path);
                        saveTarget = path;
                    }
                }
                if (saved) {
                    cmdMgr.MarkSaved();
                    scene.SetModified(false);
                    m_showSaveSuccessModal = true;
                    m_saveSuccessMessage = "Navigation mesh saved successfully:\n" + saveTarget;
                }
            }
            if (ImGui::MenuItem("Save NAV Mesh As...", "Ctrl+Shift+S", false, scene.HasNAV())) {
                std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh As");
                if (!path.empty()) {
                    if (scene.SaveNAV(path)) {
                        cmdMgr.MarkSaved();
                        scene.SetModified(false);
                        m_showSaveSuccessModal = true;
                        m_saveSuccessMessage = "Navigation mesh saved successfully:\n" + path;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Unload NAV Mesh", "Ctrl+U", false, scene.HasNAV())) {
                if (!CheckUnsavedChanges(scene, cmdMgr, PENDING_UNLOAD_NAV)) {
                    cmdMgr.Clear();
                    scene.UnloadNAV();
                }
            }
            if (ImGui::MenuItem("Unload BSP Map", nullptr, false, scene.HasBSP())) {
                if (!CheckUnsavedChanges(scene, cmdMgr, PENDING_UNLOAD_BSP)) {
                    cmdMgr.Clear();
                    scene.UnloadBSP();
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                if (!CheckUnsavedChanges(scene, cmdMgr, PENDING_QUIT)) {
                    m_requestQuit = true;
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Edit")) {
            std::string undoLabel = "Undo";
            if (cmdMgr.CanUndo()) {
                undoLabel += " " + std::string(cmdMgr.GetUndoName());
            }
            if (ImGui::MenuItem(undoLabel.c_str(), "Ctrl+Z", false, cmdMgr.CanUndo())) {
                cmdMgr.Undo();
            }

            std::string redoLabel = "Redo";
            if (cmdMgr.CanRedo()) {
                redoLabel += " " + std::string(cmdMgr.GetRedoName());
            }
            if (ImGui::MenuItem(redoLabel.c_str(), "Ctrl+Y", false, cmdMgr.CanRedo())) {
                cmdMgr.Redo();
            }

            ImGui::Separator();
            NavArea* sel = scene.GetSelectedArea();
            bool drawActive = scene.IsDrawAreaMode();
            if (ImGui::MenuItem("Draw Area Box...", "N", &drawActive)) {
                scene.ToggleDrawAreaMode();
            }
            bool fillActive = scene.IsFillAreaMode();
            if (ImGui::MenuItem("Fill Area / Room...", "F", &fillActive)) {
                scene.ToggleFillAreaMode();
            }
            bool bridgeActive = scene.IsBridgeMode();
            if (ImGui::MenuItem("Bridge Two Edges...", "B", &bridgeActive)) {
                scene.ToggleBridgeMode();
            }
            if (ImGui::MenuItem("Snap to Neighbors (Close Gaps)", "Shift+S", false, sel != nullptr)) {
                scene.SnapSelectedAreaToNeighbors(cmdMgr);
            }
            if (ImGui::MenuItem("Extrude Selected Edge", "E", false, sel != nullptr)) {
                scene.ExtrudeSelectedEdge(cmdMgr);
            }
            if (ImGui::MenuItem("Split Selected Area", "Shift+X", false, sel != nullptr)) {
                scene.SplitSelectedArea(cmdMgr);
            }
            if (ImGui::MenuItem("Merge with Adjacent Area", "Shift+M", false, sel != nullptr)) {
                scene.MergeSelectedArea(cmdMgr);
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Clear Selection", "Escape", false, scene.GetSelectedAreaID() != 0)) {
                scene.SelectArea(0);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            bool showBSP = scene.GetShowBSP();
            if (ImGui::MenuItem("Show BSP Geometry", nullptr, &showBSP)) {
                scene.SetShowBSP(showBSP);
            }

            bool showNAV = scene.GetShowNAV();
            if (ImGui::MenuItem("Show Navigation Mesh", nullptr, &showNAV)) {
                scene.SetShowNAV(showNAV);
            }

            bool showConn = scene.GetShowConnections();
            if (ImGui::MenuItem("Show Directional Connections", nullptr, &showConn)) {
                scene.SetShowConnections(showConn);
            }

            bool showWireOnSolid = scene.GetShowWireframeOnSolid();
            if (ImGui::MenuItem("Show Brush Edge Outlines", nullptr, &showWireOnSolid)) {
                scene.SetShowWireframeOnSolid(showWireOnSolid);
            }

            ImGui::Separator();
            auto& entR = scene.GetEntityRenderer();
            bool showEnts = entR.GetShowEntities();
            if (ImGui::MenuItem("Show Entities", nullptr, &showEnts)) {
                entR.SetShowEntities(showEnts);
            }
            if (showEnts) {
                bool showSpawns = entR.GetShowSpawns();
                if (ImGui::MenuItem("  Show Player Spawns", nullptr, &showSpawns)) {
                    entR.SetShowSpawns(showSpawns);
                }
                bool showObjs = entR.GetShowObjectives();
                if (ImGui::MenuItem("  Show Objectives & Hostages", nullptr, &showObjs)) {
                    entR.SetShowObjectives(showObjs);
                }
                bool showLights = entR.GetShowLights();
                if (ImGui::MenuItem("  Show Light Sources", nullptr, &showLights)) {
                    entR.SetShowLights(showLights);
                }
                bool showItems = entR.GetShowItems();
                if (ImGui::MenuItem("  Show Weapons & Armoury", nullptr, &showItems)) {
                    entR.SetShowItems(showItems);
                }
                bool showTrig = entR.GetShowTriggers();
                if (ImGui::MenuItem("  Show Triggers & Volumes", nullptr, &showTrig)) {
                    entR.SetShowTriggers(showTrig);
                }
                bool showBrushes = entR.GetShowBrushes();
                if (ImGui::MenuItem("  Show Brush Entities", nullptr, &showBrushes)) {
                    entR.SetShowBrushes(showBrushes);
                }
                bool showLinks = entR.GetShowTargetLines();
                if (ImGui::MenuItem("  Show Target Connections", nullptr, &showLinks)) {
                    entR.SetShowTargetLines(showLinks);
                }
            }

            ImGui::Separator();
            bool gridSnap = scene.GetGridSnap();
            if (ImGui::MenuItem("Snap to Grid", "Shift+W", &gridSnap)) {
                scene.SetGridSnap(gridSnap);
            }

            if (ImGui::BeginMenu("Grid Size")) {
                float sizes[] = { 1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f, 128.0f, 256.0f, 512.0f };
                for (float s : sizes) {
                    char label[32];
                    std::snprintf(label, sizeof(label), "%.0f units", s);
                    if (ImGui::MenuItem(label, nullptr, std::abs(scene.GetGridSize() - s) < 0.1f)) {
                        scene.SetGridSize(s);
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Decrease Grid Size", "[")) {
                    scene.DecreaseGridSize();
                }
                if (ImGui::MenuItem("Increase Grid Size", "]")) {
                    scene.IncreaseGridSize();
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::BeginMenu("BSP Shading Mode")) {
                if (ImGui::MenuItem("3D Textured (Hammer)", "F4", scene.GetBSPMode() == BSP_RENDER_TEXTURED)) {
                    scene.SetBSPMode(BSP_RENDER_TEXTURED);
                }
                if (ImGui::MenuItem("Solid Clay / Shaded", nullptr, scene.GetBSPMode() == BSP_RENDER_SOLID)) {
                    scene.SetBSPMode(BSP_RENDER_SOLID);
                }
                if (ImGui::MenuItem("Wireframe", nullptr, scene.GetBSPMode() == BSP_RENDER_WIREFRAME)) {
                    scene.SetBSPMode(BSP_RENDER_WIREFRAME);
                }
                if (ImGui::MenuItem("Ghost / X-Ray (Translucent)", nullptr, scene.GetBSPMode() == BSP_RENDER_GHOST)) {
                    scene.SetBSPMode(BSP_RENDER_GHOST);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Camera Mode")) {
                if (ImGui::MenuItem("FPS Flycam (WASD + Right-Click)", nullptr, camera.GetMode() == CAMERA_MODE_FPS)) {
                    camera.SetMode(CAMERA_MODE_FPS);
                }
                if (ImGui::MenuItem("Orbit Mode (Alt + Left-Click)", nullptr, camera.GetMode() == CAMERA_MODE_ORBIT)) {
                    camera.SetMode(CAMERA_MODE_ORBIT);
                }
                if (ImGui::MenuItem("Top-Down 2D Orthographic", nullptr, camera.GetMode() == CAMERA_MODE_TOPDOWN_2D)) {
                    camera.SetMode(CAMERA_MODE_TOPDOWN_2D);
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Reset Camera", "Home")) {
                camera.SetPosition(Vector3(0.0f, -500.0f, 300.0f));
                camera.SetTarget(Vector3(0.0f, 0.0f, 0.0f));
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Tools")) {
            NavArea* sel = scene.GetSelectedArea();
            if (ImGui::MenuItem("Extrude Selected Edge", "E", false, sel != nullptr)) {
                scene.ExtrudeSelectedEdge(cmdMgr);
            }
            if (ImGui::MenuItem("Split Selected Area", "Shift+X", false, sel != nullptr)) {
                scene.SplitSelectedArea(cmdMgr);
            }
            if (ImGui::MenuItem("Split / Knife Tool", "K", scene.IsKnifeMode())) {
                scene.ToggleKnifeMode();
            }
            if (ImGui::MenuItem("Merge Adjacent Area", "Shift+M", false, sel != nullptr)) {
                scene.MergeSelectedArea(cmdMgr);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Snap Selected Area to Floor", "Space", false, sel != nullptr && scene.HasBSP())) {
                cmdMgr.ExecuteCommand(std::make_unique<CmdSnapAreaToFloor>(&scene, sel->GetID()));
            }

            if (ImGui::MenuItem("Focus on Selection", "F", false, sel != nullptr)) {
                camera.FocusOn(sel->GetCenter());
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Auto-Crouch & Obstacle Flag Analyzer...", nullptr, false, scene.HasNAV())) {
                m_analyzerStats = scene.AutoAnalyzeFlags(cmdMgr, false);
                m_showAnalyzerModal = true;
            }
            if (ImGui::MenuItem("Optimize Mesh (Merge Coplanar)...", nullptr, false, scene.HasNAV())) {
                m_optimizeStats = scene.OptimizeMesh(cmdMgr, false);
                m_showOptimizeModal = true;
            }

            ImGui::Separator();
            if (ImGui::MenuItem("Auto-Generate NavMesh...", "Ctrl+G", false, scene.HasBSP())) {
                m_showGenerateModal = true;
                m_generateStatusText.clear();
            }
            if (ImGui::MenuItem("Batch Generate NavMeshes...", nullptr)) {
                m_showBatchGenerateModal = true;
            }

            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Help")) {
            if (ImGui::MenuItem("Controls & Shortcuts...")) {
                m_showHelpModal = true;
            }
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
}

void EditorUI::RenderToolPalette(EditorScene& scene, Camera& camera, CommandManager& cmdMgr) {
    ImGui::SetNextWindowSize(ImVec2(190, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Tool Palette")) {
        ImGui::Text("File Actions:");
        if (ImGui::Button("Open BSP Map...", ImVec2(-1, 26))) {
            std::string path = FileDialog::OpenFile(FileDialog::kBSPFilter, "Open GoldSrc BSP Map");
            if (!path.empty()) {
                scene.StartAsyncLoad(path);
            }
        }
        if (ImGui::Button("Open NAV Mesh...", ImVec2(-1, 26))) {
            std::string path = FileDialog::OpenFile(FileDialog::kNAVFilter, "Open Navigation Mesh");
            if (!path.empty()) {
                scene.StartAsyncLoad(path);
            }
        }
        if (ImGui::Button("Save NAV Mesh", ImVec2(-1, 26))) {
            if (scene.HasNAV()) {
                if (!scene.GetNAVPath().empty()) {
                    scene.SaveNAV();
                } else {
                    std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh");
                    if (!path.empty()) {
                        scene.SaveNAV(path);
                    }
                }
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Generation Tools:");
        if (ImGui::Button("Auto-Generate [Ctrl+G]", ImVec2(-1, 26))) {
            if (scene.HasBSP()) {
                m_showGenerateModal = true;
                m_generateStatusText.clear();
            }
        }
        if (ImGui::Button("Batch Generate...", ImVec2(-1, 26))) {
            m_showBatchGenerateModal = true;
        }

        if (ImGui::Button("Auto-Flag Obstacles", ImVec2(-1, 26))) {
            if (scene.HasNAV()) {
                m_analyzerStats = scene.AutoAnalyzeFlags(cmdMgr, false);
                m_showAnalyzerModal = true;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scans low ceilings (<72u) for Crouch, narrow corridors (<48u) for Precise, and ledges (>18u) for Jump");

        if (ImGui::Button("Optimize Mesh (Merge)", ImVec2(-1, 26))) {
            if (scene.HasNAV()) {
                m_optimizeStats = scene.OptimizeMesh(cmdMgr, false);
                m_showOptimizeModal = true;
            }
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Combines collinear coplanar rectangular areas sharing elevation, slope, and attributes to reduce area count");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Snapping:");
        bool gridSnap = scene.GetGridSnap();
        if (ImGui::Checkbox("Grid", &gridSnap)) {
            scene.SetGridSnap(gridSnap);
        }
        ImGui::SameLine();
        bool meshSnap = scene.GetMeshSnap();
        if (ImGui::Checkbox("Snap to Areas", &meshSnap)) {
            scene.SetMeshSnap(meshSnap);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Magnetically snaps dragged edges and areas flush against neighbors to eliminate gaps");

        float curGrid = scene.GetGridSize();
        if (ImGui::Button("[-]##decgrid", ImVec2(24, 22))) {
            scene.DecreaseGridSize();
        }
        ImGui::SameLine();
        ImGui::Text("%.0f u", curGrid);
        ImGui::SameLine();
        if (ImGui::Button("[+]##incgrid", ImVec2(24, 22))) {
            scene.IncreaseGridSize();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("3D Transform Gizmo Mode:");
        GizmoMode curGizmo = scene.GetGizmoMode();
        float btnW = (ImGui::GetContentRegionAvail().x - 12.0f) / 4.0f;

        bool isTrans = (curGizmo == GIZMO_MODE_TRANSLATE);
        if (isTrans) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button("Move##gzmMove", ImVec2(btnW, 24))) {
            scene.SetGizmoMode(GIZMO_MODE_TRANSLATE);
        }
        if (isTrans) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Move Gizmo: Axis Arrows, Planar Quads (XY/XZ/YZ), and Center Move");

        ImGui::SameLine();
        bool isRot = (curGizmo == GIZMO_MODE_ROTATE);
        if (isRot) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button("Rotate##gzmRot", ImVec2(btnW, 24))) {
            scene.SetGizmoMode(GIZMO_MODE_ROTATE);
        }
        if (isRot) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate Gizmo: Yaw (Z), Pitch (X), Roll (Y), and Screen Trackball Ring");

        ImGui::SameLine();
        bool isScale = (curGizmo == GIZMO_MODE_SCALE);
        if (isScale) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button("Scale##gzmScale", ImVec2(btnW, 24))) {
            scene.SetGizmoMode(GIZMO_MODE_SCALE);
        }
        if (isScale) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale Gizmo: Axis Boxes, Planar Scale Quads, and Outer Uniform Scale Ring");

        ImGui::SameLine();
        bool isComb = (curGizmo == GIZMO_MODE_COMBINED);
        if (isComb) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button("All##gzmAll", ImVec2(btnW, 24))) {
            scene.SetGizmoMode(GIZMO_MODE_COMBINED);
        }
        if (isComb) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Combined Gizmo: Move + Rotate + Scale combined simultaneously");

        ImGui::Spacing();
        ImGui::Text("3D Transform Tools:");
        NavArea* sel = scene.GetSelectedArea();
        bool hasSel = (sel != nullptr);

        if (ImGui::Button("Move / Grab [G]", ImVec2(-1, 26))) {
            if (hasSel) scene.StartGrab(sel->GetCenter());
        }

        if (ImGui::Button("Scale [S]", ImVec2(-1, 26))) {
            if (hasSel) scene.StartScale(sel->GetCenter());
        }

        if (ImGui::Button("Extrude Edge [E]", ImVec2(-1, 26))) {
            if (hasSel) scene.ExtrudeSelectedEdge(cmdMgr);
        }

        bool drawActive = scene.IsDrawAreaMode();
        if (drawActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button(drawActive ? "Cancel Draw Area [Esc]" : "Draw Area Box [N]", ImVec2(-1, 26))) {
            scene.ToggleDrawAreaMode();
        }
        if (drawActive) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to start rectangular area, move cursor, click to finish (Desktop marquee creation)");

        bool bridgeActive = scene.IsBridgeMode();
        if (bridgeActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button(bridgeActive ? "Cancel Bridge [Esc]" : "Bridge Edges [B]", ImVec2(-1, 26))) {
            scene.ToggleBridgeMode();
        }
        if (bridgeActive) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click edge A then edge B to create an intermediate connecting NavArea between them");

        bool fillActive = scene.IsFillAreaMode();
        if (fillActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button(fillActive ? "Cancel Fill Area [Esc]" : "Fill Area / Room [F]", ImVec2(-1, 26))) {
            scene.ToggleFillAreaMode();
        }
        if (fillActive) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click on any floor surface to auto-generate connected NavMesh filling the room or area");

        if (ImGui::Button("Snap to Neighbors [Shift+S]", ImVec2(-1, 26))) {
            if (hasSel) scene.SnapSelectedAreaToNeighbors(cmdMgr);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snaps selected area flush to adjacent areas and closes micro-gaps");

        if (ImGui::Button("Split Area [Shift+X]", ImVec2(-1, 26))) {
            if (hasSel) scene.SplitSelectedArea(cmdMgr);
        }

        bool knifeActive = scene.IsKnifeMode();
        if (knifeActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button(knifeActive ? "Exit Knife Tool [Esc]" : "Knife / Split Tool [K]", ImVec2(-1, 26))) {
            scene.ToggleKnifeMode();
        }
        if (knifeActive) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click or drag across an area to slice along seam with bilinearly interpolated elevation");

        if (ImGui::Button("Merge Areas [Shift+M]", ImVec2(-1, 26))) {
            if (hasSel) scene.MergeSelectedArea(cmdMgr);
        }

        if (ImGui::Button("Rotate 90° [R]", ImVec2(-1, 26))) {
            if (hasSel) scene.RotateSelectedArea90(cmdMgr);
        }

        if (ImGui::Button("Connect Mode [C]", ImVec2(-1, 26))) {
            if (hasSel) {
                if (scene.GetTransformMode() == EditorScene::TRANSFORM_CONNECT) scene.CancelTransform();
                else scene.StartConnectMode();
            }
        }

        if (ImGui::Button("Duplicate [Shift+D]", ImVec2(-1, 26))) {
            if (hasSel) scene.DuplicateSelectedArea(cmdMgr);
        }

        if (ImGui::Button("Delete Area [X]", ImVec2(-1, 26))) {
            if (hasSel) scene.DeleteSelectedArea(cmdMgr);
        }

        if (ImGui::Button("Snap to Floor [Space]", ImVec2(-1, 26))) {
            if (sel && scene.HasBSP()) {
                cmdMgr.ExecuteCommand(std::make_unique<CmdSnapAreaToFloor>(&scene, sel->GetID()));
            }
        }

        if (ImGui::Button("Focus Area [F]", ImVec2(-1, 26))) {
            if (sel) {
                camera.FocusOn(sel->GetCenter());
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Shading quick buttons
        ImGui::Text("BSP View (F4):");
        if (ImGui::RadioButton("Textured", scene.GetBSPMode() == BSP_RENDER_TEXTURED)) {
            scene.SetBSPMode(BSP_RENDER_TEXTURED);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Solid", scene.GetBSPMode() == BSP_RENDER_SOLID)) {
            scene.SetBSPMode(BSP_RENDER_SOLID);
        }
        if (ImGui::RadioButton("Wireframe", scene.GetBSPMode() == BSP_RENDER_WIREFRAME)) {
            scene.SetBSPMode(BSP_RENDER_WIREFRAME);
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Ghost", scene.GetBSPMode() == BSP_RENDER_GHOST)) {
            scene.SetBSPMode(BSP_RENDER_GHOST);
        }

        bool showWireOnSolid = scene.GetShowWireframeOnSolid();
        if (ImGui::Checkbox("Brush Outlines", &showWireOnSolid)) {
            scene.SetShowWireframeOnSolid(showWireOnSolid);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Entities (F3):");
        bool showEntities = scene.GetEntityRenderer().GetShowEntities();
        if (ImGui::Checkbox("Show Entities", &showEntities)) {
            scene.GetEntityRenderer().SetShowEntities(showEntities);
        }
        bool showTargetLines = scene.GetEntityRenderer().GetShowTargetLines();
        if (ImGui::Checkbox("Show Target Lines", &showTargetLines)) {
            scene.GetEntityRenderer().SetShowTargetLines(showTargetLines);
        }
    }
    ImGui::End();
}


void EditorUI::RenderHierarchy(EditorScene& scene, Camera& camera) {
    ImGui::SetNextWindowSize(ImVec2(270, 420), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Explorer")) {
        if (ImGui::BeginTabBar("ExplorerTabs")) {
            char navTabTitle[64];
            size_t areaCount = scene.HasNAV() ? scene.GetNAV().GetAreaCount() : 0;
            std::snprintf(navTabTitle, sizeof(navTabTitle), "NavAreas (%zu)", areaCount);

            if (ImGui::BeginTabItem(navTabTitle)) {
                if (!scene.HasNAV()) {
                    ImGui::TextDisabled("No NAV mesh loaded.");
                } else {
                    ImGui::InputTextWithHint("##SearchArea", "Search Area ID or Place...", m_searchFilter, sizeof(m_searchFilter));
                    ImGui::Separator();

                    const auto& areas = scene.GetNAV().GetAreas();
                    std::string filterStr = m_searchFilter;
                    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

                    ImGui::BeginChild("AreaList", ImVec2(0, 0), true);

                    for (const NavArea* area : areas) {
                        if (!area) continue;

                        uint32_t id = area->GetID();
                        std::string idStr = std::to_string(id);
                        const std::string& place = area->GetPlaceName();

                        if (!filterStr.empty()) {
                            std::string lowerPlace = place;
                            std::transform(lowerPlace.begin(), lowerPlace.end(), lowerPlace.begin(), ::tolower);
                            if (idStr.find(filterStr) == std::string::npos && lowerPlace.find(filterStr) == std::string::npos) {
                                continue;
                            }
                        }

                        char label[128];
                        if (place.empty()) {
                            std::snprintf(label, sizeof(label), "Area #%u", id);
                        } else {
                            std::snprintf(label, sizeof(label), "Area #%u (%s)", id, place.c_str());
                        }

                        bool isSelected = scene.IsAreaSelected(id);
                        if (ImGui::Selectable(label, isSelected)) {
                            ImGuiIO& io = ImGui::GetIO();
                            scene.SelectArea(id, io.KeyShift, io.KeyCtrl);
                            scene.SelectEntity(-1);
                        }

                        if (ImGui::IsItemHovered()) {
                            scene.SetHoveredArea(id);
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                camera.FocusOn(area->GetCenter());
                            }
                        }
                    }

                    ImGui::EndChild();
                }
                ImGui::EndTabItem();
            }

            char entTabTitle[64];
            size_t entCount = scene.GetEntityRenderer().GetEntityCount();
            std::snprintf(entTabTitle, sizeof(entTabTitle), "Entities (%zu)", entCount);

            if (ImGui::BeginTabItem(entTabTitle)) {
                RenderEntityHierarchy(scene, camera);
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

void EditorUI::RenderEntityHierarchy(EditorScene& scene, Camera& camera) {
    auto& entRenderer = scene.GetEntityRenderer();
    if (!entRenderer.IsLoaded()) {
        ImGui::TextDisabled("No BSP entities loaded.");
        return;
    }

    // Category filter buttons
    ImGui::Text("Filter:");
    if (ImGui::SmallButton(m_entityCategoryFilter == -1 ? "[All]" : "All")) {
        m_entityCategoryFilter = -1;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(m_entityCategoryFilter == ENT_CAT_SPAWN_CT ? "[Spawns]" : "Spawns")) {
        m_entityCategoryFilter = (m_entityCategoryFilter == ENT_CAT_SPAWN_CT) ? -1 : ENT_CAT_SPAWN_CT;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(m_entityCategoryFilter == ENT_CAT_OBJECTIVE_BOMB ? "[Objectives]" : "Objectives")) {
        m_entityCategoryFilter = (m_entityCategoryFilter == ENT_CAT_OBJECTIVE_BOMB) ? -1 : ENT_CAT_OBJECTIVE_BOMB;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(m_entityCategoryFilter == ENT_CAT_LIGHT ? "[Lights]" : "Lights")) {
        m_entityCategoryFilter = (m_entityCategoryFilter == ENT_CAT_LIGHT) ? -1 : ENT_CAT_LIGHT;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(m_entityCategoryFilter == ENT_CAT_ITEM ? "[Items]" : "Items")) {
        m_entityCategoryFilter = (m_entityCategoryFilter == ENT_CAT_ITEM) ? -1 : ENT_CAT_ITEM;
    }

    ImGui::InputTextWithHint("##SearchEnt", "Search classname or target...", m_entityFilter, sizeof(m_entityFilter));
    ImGui::Separator();

    std::string filterStr = m_entityFilter;
    std::transform(filterStr.begin(), filterStr.end(), filterStr.begin(), ::tolower);

    const auto& entities = entRenderer.GetEntities();
    ImGui::BeginChild("EntityList", ImVec2(0, 0), true);

    for (const auto& ent : entities) {
        if (m_entityCategoryFilter != -1) {
            if (m_entityCategoryFilter == ENT_CAT_SPAWN_CT) {
                if (ent.category != ENT_CAT_SPAWN_CT && ent.category != ENT_CAT_SPAWN_T && ent.category != ENT_CAT_SPAWN_VIP) {
                    continue;
                }
            } else if (m_entityCategoryFilter == ENT_CAT_OBJECTIVE_BOMB) {
                if (ent.category != ENT_CAT_OBJECTIVE_BOMB && ent.category != ENT_CAT_OBJECTIVE_HOSTAGE && ent.category != ENT_CAT_OBJECTIVE_RESCUE && ent.category != ENT_CAT_OBJECTIVE_BUYZONE) {
                    continue;
                }
            } else if (ent.category != m_entityCategoryFilter) {
                continue;
            }
        }

        if (!filterStr.empty()) {
            std::string lowerClass = ent.classname;
            std::transform(lowerClass.begin(), lowerClass.end(), lowerClass.begin(), ::tolower);
            std::string lowerTarget = ent.targetname + " " + ent.target;
            std::transform(lowerTarget.begin(), lowerTarget.end(), lowerTarget.begin(), ::tolower);

            if (lowerClass.find(filterStr) == std::string::npos && lowerTarget.find(filterStr) == std::string::npos) {
                continue;
            }
        }

        const char* badge = "[ENT]";
        ImVec4 badgeCol(0.6f, 0.6f, 0.6f, 1.0f);
        if (ent.category == ENT_CAT_SPAWN_CT) { badge = "[CT]"; badgeCol = ImVec4(0.2f, 0.6f, 1.0f, 1.0f); }
        else if (ent.category == ENT_CAT_SPAWN_T) { badge = "[T ]"; badgeCol = ImVec4(0.95f, 0.25f, 0.2f, 1.0f); }
        else if (ent.category == ENT_CAT_SPAWN_VIP) { badge = "[VIP]"; badgeCol = ImVec4(0.0f, 0.9f, 0.9f, 1.0f); }
        else if (ent.category == ENT_CAT_OBJECTIVE_BOMB) { badge = "[BOMB]"; badgeCol = ImVec4(0.95f, 0.2f, 0.2f, 1.0f); }
        else if (ent.category == ENT_CAT_OBJECTIVE_HOSTAGE) { badge = "[HOST]"; badgeCol = ImVec4(0.25f, 0.8f, 0.3f, 1.0f); }
        else if (ent.category == ENT_CAT_OBJECTIVE_RESCUE) { badge = "[RESC]"; badgeCol = ImVec4(0.2f, 0.85f, 0.6f, 1.0f); }
        else if (ent.category == ENT_CAT_OBJECTIVE_BUYZONE) { badge = "[BUY]"; badgeCol = ImVec4(0.85f, 0.9f, 0.2f, 1.0f); }
        else if (ent.category == ENT_CAT_LIGHT) { badge = "[LGT]"; badgeCol = ImVec4(1.0f, 0.92f, 0.25f, 1.0f); }
        else if (ent.category == ENT_CAT_ITEM) { badge = "[WPN]"; badgeCol = ImVec4(1.0f, 0.72f, 0.15f, 1.0f); }
        else if (ent.category == ENT_CAT_SOUND) { badge = "[SND]"; badgeCol = ImVec4(0.85f, 0.3f, 0.95f, 1.0f); }
        else if (ent.category == ENT_CAT_TRIGGER) { badge = "[TRG]"; badgeCol = ImVec4(0.95f, 0.55f, 0.15f, 1.0f); }
        else if (ent.category == ENT_CAT_BRUSH) { badge = "[BRS]"; badgeCol = ImVec4(0.35f, 0.75f, 0.9f, 1.0f); }

        char itemText[160];
        if (!ent.targetname.empty()) {
            std::snprintf(itemText, sizeof(itemText), "%s %s \"%s\"", badge, ent.classname.c_str(), ent.targetname.c_str());
        } else {
            std::snprintf(itemText, sizeof(itemText), "%s %s #%d", badge, ent.classname.c_str(), ent.index);
        }

        bool isSelected = (scene.GetSelectedEntityIndex() == ent.index);
        ImGui::PushStyleColor(ImGuiCol_Text, badgeCol);
        if (ImGui::Selectable(itemText, isSelected)) {
            scene.SelectEntity(ent.index);
            scene.SelectArea(0);
        }
        ImGui::PopStyleColor();

        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            camera.FocusOn(ent.origin);
        }
    }

    ImGui::EndChild();
}

void EditorUI::RenderEntityInspector(EditorScene& scene, Camera& camera) {
    const EditorEntity* ent = scene.GetSelectedEntity();
    if (!ent) {
        ImGui::TextDisabled("No entity selected.");
        return;
    }

    // Category badge & header
    ImGui::TextColored(ImVec4(ent->color.x, ent->color.y, ent->color.z, 1.0f), "[%s]", ent->GetCategoryName());
    ImGui::SameLine();
    ImGui::Text("%s", ent->classname.c_str());
    ImGui::TextDisabled("%s (#%d)", ent->GetFriendlyName(), ent->index);
    ImGui::Separator();

    // Transform Coordinates
    ImGui::Text("Origin:");
    ImGui::Text("  X: %.1f  Y: %.1f  Z: %.1f", ent->origin.x, ent->origin.y, ent->origin.z);

    if (ent->yaw != 0.0f || ent->angles.x != 0.0f || ent->angles.z != 0.0f) {
        ImGui::Text("Angles:");
        ImGui::Text("  Pitch: %.1f  Yaw: %.1f  Roll: %.1f", ent->angles.x, ent->angles.y, ent->angles.z);
    }

    // Special item info: Armoury
    if (ent->classname == "armoury_entity") {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.15f, 1.0f), "Weapon Spawner:");
        ImGui::Text("  Weapon: %s", ent->GetArmouryItemName().c_str());
        auto itCount = ent->keyvalues.find("count");
        if (itCount != ent->keyvalues.end()) {
            ImGui::Text("  Count: %s", itCount->second.c_str());
        }
    }

    // Special item info: Lights
    if (ent->category == ENT_CAT_LIGHT) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0f, 0.95f, 0.2f, 1.0f), "Light Parameters:");
        auto itLight = ent->keyvalues.find("_light");
        if (itLight != ent->keyvalues.end()) {
            ImGui::Text("  RGB / Intensity: %s", itLight->second.c_str());
            ImVec4 colSwatch(ent->color.x, ent->color.y, ent->color.z, 1.0f);
            ImGui::ColorButton("##LightColorSwatch", colSwatch, ImGuiColorEditFlags_NoTooltip, ImVec2(24, 18));
            ImGui::SameLine();
            ImGui::Text("Light Color Preview");
        }
    }

    // Special item info: Audio
    if (ent->category == ENT_CAT_SOUND) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.95f, 1.0f), "Sound Parameters:");
        auto itMsg = ent->keyvalues.find("message");
        if (itMsg != ent->keyvalues.end()) {
            ImGui::TextWrapped("  File: %s", itMsg->second.c_str());
        }
        auto itVol = ent->keyvalues.find("health");
        if (itVol != ent->keyvalues.end()) {
            ImGui::Text("  Volume: %s / 10", itVol->second.c_str());
        }
    }

    // Target relationships
    if (!ent->target.empty() || !ent->targetname.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Target Linkages:");
        if (!ent->target.empty()) {
            ImGui::Text("  Target -> %s", ent->target.c_str());
            if (ImGui::Button("Jump to Target Entity", ImVec2(-1, 24))) {
                const auto& ents = scene.GetEntityRenderer().GetEntities();
                for (const auto& other : ents) {
                    if (other.targetname == ent->target) {
                        scene.SelectEntity(other.index);
                        camera.FocusOn(other.origin);
                        break;
                    }
                }
            }
        }
        if (!ent->targetname.empty()) {
            ImGui::Text("  Targetname <- %s", ent->targetname.c_str());
        }
    }

    // Brush model info
    if (ent->isBrush) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Brush Model: %s", ent->model.c_str());
        ImGui::Text("Bounds: (%.0f, %.0f, %.0f) to (%.0f, %.0f, %.0f)",
            ent->worldMins.x, ent->worldMins.y, ent->worldMins.z,
            ent->worldMaxs.x, ent->worldMaxs.y, ent->worldMaxs.z);
    }

    // Key-Values Dictionary Table
    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Key-Values Dictionary", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::BeginTable("KeyValTable", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const auto& kv : ent->keyvalues) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(kv.first.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(kv.second.c_str());
            }
            ImGui::EndTable();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Focus Camera [F]", ImVec2(-1, 26))) {
        camera.FocusOn(ent->origin);
    }
    if (ImGui::Button("Deselect [Escape]", ImVec2(-1, 24))) {
        scene.SelectEntity(-1);
    }
}

void EditorUI::RenderInspector(EditorScene& scene, Camera& camera, CommandManager& cmdMgr) {
    ImGui::SetNextWindowSize(ImVec2(290, 460), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Property Inspector")) {
        if (scene.GetSelectedEntityIndex() >= 0) {
            RenderEntityInspector(scene, camera);
            ImGui::End();
            return;
        }

        const auto& selIds = scene.GetSelectedAreaIDs();
        if (selIds.size() > 1) {
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Multi-Selection: %zu NavAreas", selIds.size());
            ImGui::Separator();

            // Batch Place Name
            ImGui::Text("Batch Place Name:");
            static char batchPlaceBuffer[64] = "";
            ImGui::InputText("##BatchPlaceInput", batchPlaceBuffer, sizeof(batchPlaceBuffer));
            if (ImGui::Button("Apply Place to All", ImVec2(-1, 24))) {
                if (batchPlaceBuffer[0] != '\0') {
                    scene.BatchSetPlace(batchPlaceBuffer, cmdMgr);
                }
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Batch Attributes / Flags:");

            static bool batchCrouch = false;
            static bool batchJump = false;
            static bool batchPrecise = false;
            static bool batchNoJump = false;

            ImGui::Checkbox("Crouch##batch", &batchCrouch);
            ImGui::Checkbox("Jump##batch", &batchJump);
            ImGui::Checkbox("Precise##batch", &batchPrecise);
            ImGui::Checkbox("No Jump##batch", &batchNoJump);

            if (ImGui::Button("Apply Attributes to All", ImVec2(-1, 24))) {
                uint8_t newFlags = 0;
                if (batchCrouch) newFlags |= NAV_ATTR_CROUCH;
                if (batchJump) newFlags |= NAV_ATTR_JUMP;
                if (batchPrecise) newFlags |= NAV_ATTR_PRECISE;
                if (batchNoJump) newFlags |= NAV_ATTR_NO_JUMP;
                scene.BatchSetAttributes(newFlags, cmdMgr);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Batch Actions:");

            if (ImGui::Button("Snap All Neighbors [Shift+S]", ImVec2(-1, 26))) {
                scene.BatchSnapToNeighbors(cmdMgr);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Closes micro-gaps and connects all selected areas to their neighbors");

            bool hasBSP = scene.HasBSP();
            if (!hasBSP) ImGui::BeginDisabled();
            if (ImGui::Button("Snap All to Floor [Space]", ImVec2(-1, 26))) {
                scene.BatchSnapToFloor(cmdMgr);
            }
            if (!hasBSP) ImGui::EndDisabled();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip(hasBSP ? "Snaps corner elevations of all selected areas onto BSP floor" : "Requires loaded BSP map");

            if (ImGui::Button("Extrude All [E]", ImVec2(-1, 26))) {
                scene.BatchExtrude(cmdMgr);
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Extrudes all selected areas outward along active edge");

            if (ImGui::Button("Auto-Flag Selected", ImVec2(-1, 26))) {
                m_analyzerStats = scene.AutoAnalyzeFlags(cmdMgr, true);
                m_showAnalyzerModal = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scans selected areas for Crouch, Precise, and Jump flags");

            if (ImGui::Button("Optimize Selected", ImVec2(-1, 26))) {
                m_optimizeStats = scene.OptimizeMesh(cmdMgr, true);
                m_showOptimizeModal = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Merges adjacent coplanar rectangular areas within the selection");

            if (ImGui::Button("Duplicate All [Shift+D]", ImVec2(-1, 26))) {
                scene.BatchDuplicate(cmdMgr);
            }

            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 0.8f));
            if (ImGui::Button("Delete All [X]", ImVec2(-1, 26))) {
                scene.BatchDelete(cmdMgr);
            }
            ImGui::PopStyleColor();

            if (ImGui::Button("Deselect All [Esc]", ImVec2(-1, 24))) {
                scene.ClearSelection();
            }

            ImGui::Spacing();
            ImGui::Separator();
            if (ImGui::CollapsingHeader("Selected Area IDs", ImGuiTreeNodeFlags_DefaultOpen)) {
                std::string idSummary;
                for (size_t i = 0; i < selIds.size(); ++i) {
                    if (i > 0) idSummary += ", ";
                    idSummary += "#" + std::to_string(selIds[i]);
                    if (i > 40) {
                        idSummary += " ... (" + std::to_string(selIds.size() - 40) + " more)";
                        break;
                    }
                }
                ImGui::TextWrapped("%s", idSummary.c_str());
            }

            ImGui::End();
            return;
        }

        NavArea* area = scene.GetSelectedArea();
        if (!area) {
            ImGui::TextDisabled("No object or area selected.\nClick a NavArea or Entity in the 3D viewport or explorer.");
            ImGui::End();
            return;
        }

        uint32_t id = area->GetID();
        ImGui::Text("Area ID: #%u", id);
        ImGui::Separator();

        // Place Name
        ImGui::Text("Place Name:");
        std::string curPlace = area->GetPlaceName();
        if (m_placeEditBuffer[0] == '\0' || curPlace != m_placeEditBuffer) {
            std::strncpy(m_placeEditBuffer, curPlace.c_str(), sizeof(m_placeEditBuffer) - 1);
            m_placeEditBuffer[sizeof(m_placeEditBuffer) - 1] = '\0';
        }

        if (ImGui::InputText("##PlaceInput", m_placeEditBuffer, sizeof(m_placeEditBuffer), ImGuiInputTextFlags_EnterReturnsTrue)) {
            cmdMgr.ExecuteCommand(std::make_unique<CmdSetAreaPlace>(&scene, id, m_placeEditBuffer));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Attributes / Flags:");

        uint8_t flags = area->GetAttributes();
        bool crouch = (flags & NAV_ATTR_CROUCH) != 0;
        bool jump = (flags & NAV_ATTR_JUMP) != 0;
        bool precise = (flags & NAV_ATTR_PRECISE) != 0;
        bool noJump = (flags & NAV_ATTR_NO_JUMP) != 0;

        bool changed = false;
        if (ImGui::Checkbox("Crouch", &crouch)) changed = true;
        if (ImGui::Checkbox("Jump", &jump)) changed = true;
        if (ImGui::Checkbox("Precise", &precise)) changed = true;
        if (ImGui::Checkbox("No Jump", &noJump)) changed = true;

        if (changed) {
            uint8_t newFlags = 0;
            if (crouch) newFlags |= NAV_ATTR_CROUCH;
            if (jump) newFlags |= NAV_ATTR_JUMP;
            if (precise) newFlags |= NAV_ATTR_PRECISE;
            if (noJump) newFlags |= NAV_ATTR_NO_JUMP;

            cmdMgr.ExecuteCommand(std::make_unique<CmdSetAreaAttributes>(&scene, id, newFlags));
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("3D Transform & Geometry:");

        SelectedHandleType curH = scene.GetSelectedHandle();
        if (curH == HANDLE_NONE) curH = scene.GetHoveredHandle();
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "Handle: %s", GetHandleName(curH));

        const NavExtent& extent = area->GetExtent();
        Vector3 center = area->GetCenter();
        float width = extent.hi.x - extent.lo.x;
        float length = extent.hi.y - extent.lo.y;

        float pos[3] = { center.x, center.y, center.z };
        if (ImGui::DragFloat3("Position (X,Y,Z)", pos, 1.0f, -65536.0f, 65536.0f, "%.1f")) {
            Vector3 delta(pos[0] - center.x, pos[1] - center.y, pos[2] - center.z);
            NavExtent newExt;
            newExt.lo = extent.lo + delta;
            newExt.hi = extent.hi + delta;
            scene.GetNAV().GetGrid().RemoveArea(area);
            area->SetExtent(newExt);
            area->SetCornerHeights(area->GetNEZ() + delta.z, area->GetSWZ() + delta.z);
            scene.GetNAV().GetGrid().AddArea(area);
            scene.RebuildNavRenderer();
        }

        float size[2] = { width, length };
        if (ImGui::DragFloat2("Size (W, L)", size, 1.0f, 8.0f, 8192.0f, "%.1f")) {
            float hw = std::max(4.0f, size[0] * 0.5f);
            float hl = std::max(4.0f, size[1] * 0.5f);
            NavExtent newExt;
            newExt.lo = Vector3(center.x - hw, center.y - hl, extent.lo.z);
            newExt.hi = Vector3(center.x + hw, center.y + hl, extent.hi.z);
            scene.GetNAV().GetGrid().RemoveArea(area);
            area->SetExtent(newExt);
            scene.GetNAV().GetGrid().AddArea(area);
            scene.RebuildNavRenderer();
        }

        float corners[4] = {
            area->GetCorner(NAV_CORNER_NORTH_WEST).z,
            area->GetNEZ(),
            area->GetCorner(NAV_CORNER_SOUTH_EAST).z,
            area->GetSWZ()
        };
        if (ImGui::DragFloat4("Corners (NW,NE,SE,SW)", corners, 0.5f, -65536.0f, 65536.0f, "%.1f")) {
            NavExtent newExt = extent;
            newExt.lo.z = corners[0];
            newExt.hi.z = corners[2];
            scene.GetNAV().GetGrid().RemoveArea(area);
            area->SetExtent(newExt);
            area->SetCornerHeights(corners[1], corners[3]);
            scene.GetNAV().GetGrid().AddArea(area);
            scene.RebuildNavRenderer();
        }

        if (ImGui::Button("Flatten / Level Area Heights", ImVec2(-1, 22))) {
            float avgZ = std::round((corners[0] + corners[1] + corners[2] + corners[3]) * 0.25f);
            NavExtent newExt = extent;
            newExt.lo.z = avgZ;
            newExt.hi.z = avgZ;
            scene.GetNAV().GetGrid().RemoveArea(area);
            area->SetExtent(newExt);
            area->SetCornerHeights(avgZ, avgZ);
            scene.GetNAV().GetGrid().AddArea(area);
            scene.RebuildNavRenderer();
        }

        ImGui::Spacing();
        ImGui::Separator();

        float availW = ImGui::GetContentRegionAvail().x;
        float spacing = ImGui::GetStyle().ItemSpacing.x;
        float w2 = (availW - spacing) * 0.5f;
        float w3 = (availW - spacing * 2.0f) / 3.0f;
        float w4 = (availW - spacing * 3.0f) / 4.0f;

        // Gizmo Mode Selection
        ImGui::Text("Gizmo Mode:");
        auto curGizmo = scene.GetGizmoMode();
        bool isPos = (curGizmo == GIZMO_MODE_TRANSLATE);
        if (isPos) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button("Move##piGzmMove", ImVec2(w4, 22))) {
            scene.SetGizmoMode(GIZMO_MODE_TRANSLATE);
        }
        if (isPos) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Translate Gizmo: 3D Axis Arrows & Translation Quads");

        ImGui::SameLine();
        bool isRot = (curGizmo == GIZMO_MODE_ROTATE);
        if (isRot) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button("Rot##piGzmRot", ImVec2(w4, 22))) {
            scene.SetGizmoMode(GIZMO_MODE_ROTATE);
        }
        if (isRot) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate Gizmo: Yaw/Z Rotation Ring");

        ImGui::SameLine();
        bool isScl = (curGizmo == GIZMO_MODE_SCALE);
        if (isScl) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button("Scale##piGzmScl", ImVec2(w4, 22))) {
            scene.SetGizmoMode(GIZMO_MODE_SCALE);
        }
        if (isScl) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale Gizmo: Axis Boxes & Uniform Scale Ring");

        ImGui::SameLine();
        bool isComb = (curGizmo == GIZMO_MODE_COMBINED);
        if (isComb) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button("All##piGzmAll", ImVec2(w4, 22))) {
            scene.SetGizmoMode(GIZMO_MODE_COMBINED);
        }
        if (isComb) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Combined Gizmo: Move + Rotate + Scale simultaneously");

        // Snapping toggles
        bool gridSnap = scene.GetGridSnap();
        if (ImGui::Checkbox("Snap to Grid", &gridSnap)) {
            scene.SetGridSnap(gridSnap);
        }
        ImGui::SameLine();
        bool meshSnap = scene.GetMeshSnap();
        if (ImGui::Checkbox("Snap to Areas", &meshSnap)) {
            scene.SetMeshSnap(meshSnap);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Magnetically snaps dragged edges and areas flush against neighbors");

        ImGui::Spacing();

        // Row 1: Grab, Scale, Rotate
        if (ImGui::Button("Grab [G]", ImVec2(w3, 24))) {
            scene.StartGrab(center);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Grab / Move area freely in 3D");
        ImGui::SameLine();
        if (ImGui::Button("Scale [S]", ImVec2(w3, 24))) {
            scene.StartScale(center);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Scale area dimensions");
        ImGui::SameLine();
        if (ImGui::Button("Rotate [R]", ImVec2(w3, 24))) {
            scene.RotateSelectedArea90(cmdMgr);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Rotate area orientation 90 degrees");

        // Row 2: Extrude, Bridge Edges
        if (ImGui::Button("Extrude [E]", ImVec2(w2, 24))) {
            scene.ExtrudeSelectedEdge(cmdMgr);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Extrude selected edge outward to create adjacent area");
        ImGui::SameLine();
        bool bridgeActive = scene.IsBridgeMode();
        if (bridgeActive) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.45f, 0.15f, 1.0f));
        if (ImGui::Button(bridgeActive ? "Cancel Bridge [Esc]" : "Bridge Edges [B]", ImVec2(w2, 24))) {
            scene.ToggleBridgeMode();
        }
        if (bridgeActive) ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Bridge Tool: Click edge A then edge B to create a connecting NavArea between them");

        // Row 3: Snap to Neighbors, Snap to Floor
        if (ImGui::Button("Snap Neighbors [Shift+S]", ImVec2(w2, 24))) {
            scene.SnapSelectedAreaToNeighbors(cmdMgr);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Snaps selected area flush against adjacent areas and closes micro-gaps");
        ImGui::SameLine();
        bool hasBSP = scene.HasBSP();
        if (!hasBSP) ImGui::BeginDisabled();
        if (ImGui::Button("Snap Floor [Space]", ImVec2(w2, 24))) {
            cmdMgr.ExecuteCommand(std::make_unique<CmdSnapAreaToFloor>(&scene, id));
        }
        if (!hasBSP) ImGui::EndDisabled();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip(hasBSP ? "Snaps area corner elevations flush onto BSP floor geometry" : "Requires loaded BSP map");

        // Row 4: Split, Merge
        if (ImGui::Button("Split [Shift+X]", ImVec2(w2, 24))) {
            scene.SplitSelectedArea(cmdMgr);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Split selected area along longest axis");
        ImGui::SameLine();
        if (ImGui::Button("Merge [Shift+M]", ImVec2(w2, 24))) {
            scene.MergeSelectedArea(cmdMgr);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Merge selected area with adjacent collinear area");

        // Row 5: Duplicate, Delete
        if (ImGui::Button("Duplicate [Shift+D]", ImVec2(w2, 24))) {
            scene.DuplicateSelectedArea(cmdMgr);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duplicate selected area");
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.20f, 0.20f, 0.8f));
        if (ImGui::Button("Delete [X]", ImVec2(w2, 24))) {
            scene.DeleteSelectedArea(cmdMgr);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete selected area");

        // Row 6: Focus Viewport
        if (ImGui::Button("Focus Viewport [F]", ImVec2(-1, 24))) {
            camera.FocusOn(area->GetCenter());
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Center camera viewport on this area");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Connections (Clean Colors):");

        bool inConnectMode = (scene.GetTransformMode() == EditorScene::TRANSFORM_CONNECT);
        if (ImGui::Button(inConnectMode ? "Exit Connect Mode [C]" : "Enter Connect Mode [C]", ImVec2(-1, 26))) {
            if (inConnectMode) {
                scene.CancelTransform();
            } else {
                scene.StartConnectMode();
            }
        }

        // Add Connection Sub-panel
        ImGui::Spacing();
        ImGui::Text("Add Connection:");
        ImGui::SetNextItemWidth(90.0f);
        ImGui::InputInt("##TargetID", &m_connectTargetInputId, 0, 0);
        ImGui::SameLine();
        const char* dirOptions[] = { "Auto-Dir", "North", "East", "South", "West" };
        ImGui::SetNextItemWidth(85.0f);
        ImGui::Combo("##DirCombo", &m_connectDirSelection, dirOptions, 5);
        ImGui::Checkbox("Two-Way", &m_connectBidirectional);
        ImGui::SameLine();
        if (ImGui::Button("+ Connect", ImVec2(-1, 22))) {
            if (m_connectTargetInputId > 0 && static_cast<uint32_t>(m_connectTargetInputId) != id) {
                int explicitDir = (m_connectDirSelection > 0) ? (m_connectDirSelection - 1) : -1;
                cmdMgr.ExecuteCommand(std::make_unique<CmdConnectAreas>(&scene, id, static_cast<uint32_t>(m_connectTargetInputId), m_connectBidirectional, explicitDir));
            }
        }

        ImGui::Separator();
        ImGui::Text("Active Connections:");

        static const char* dirNames[] = { "North", "East", "South", "West" };
        for (int d = 0; d < 4; ++d) {
            const auto& conns = area->GetAdjacentList(static_cast<NavDirType>(d));
            for (const auto& conn : conns) {
                if (!conn.area) continue;
                uint32_t targetId = conn.area->GetID();
                bool twoWay = conn.area->IsConnected(area);

                ImGui::PushID(static_cast<int>(targetId * 10 + d));
                ImGui::BulletText("[%s] Area #%u", dirNames[d], targetId);
                ImGui::SameLine();

                if (twoWay) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.0f, 0.6f, 0.7f, 0.8f));
                    if (ImGui::SmallButton("2-Way")) {
                        cmdMgr.ExecuteCommand(std::make_unique<CmdDisconnectAreas>(&scene, targetId, id, false));
                    }
                    ImGui::PopStyleColor();
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.4f, 0.0f, 0.8f));
                    if (ImGui::SmallButton("1-Way")) {
                        cmdMgr.ExecuteCommand(std::make_unique<CmdConnectAreas>(&scene, targetId, id, false));
                    }
                    ImGui::PopStyleColor();
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("Jump")) {
                    scene.SelectArea(targetId);
                }

                ImGui::SameLine();
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.15f, 0.15f, 0.8f));
                if (ImGui::SmallButton("X")) {
                    cmdMgr.ExecuteCommand(std::make_unique<CmdDisconnectAreas>(&scene, id, targetId, twoWay));
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

void EditorUI::RenderStatusBar(const EditorScene& scene, const Camera& camera) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - 24.0f));
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x, 24.0f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings;

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.12f, 1.0f));
    if (ImGui::Begin("StatusBar", nullptr, flags)) {
        const char* bspName = scene.HasBSP() ? scene.GetBSP().GetMapName().c_str() : "No Map";
        size_t areaCount = scene.HasNAV() ? scene.GetNAV().GetAreaCount() : 0;
        uint32_t selId = scene.GetSelectedAreaID();
        float grid = scene.GetGridSize();
        bool snap = scene.GetGridSnap();

        size_t entCount = scene.GetEntityRenderer().GetEntityCount();
        const auto& entR = scene.GetEntityRenderer();
        int selEntIdx = scene.GetSelectedEntityIndex();

        if (scene.IsDrawAreaMode()) {
            if (!scene.IsDrawAreaActive()) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 1.0f, 1.0f),
                    "[DRAW AREA TOOL ACTIVE] Click 1st corner on floor/slope | Hold Right-Click to fly/freelook | Esc to cancel");
            } else {
                Vector3 p0 = scene.GetDrawAreaStart();
                Vector3 p1 = scene.GetDrawAreaCurrent();
                float w = std::abs(p1.x - p0.x);
                float l = std::abs(p1.y - p0.y);
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                    "[DRAW AREA TOOL ACTIVE] 1st Corner @ (%.0f, %.0f, %.0f) | Live Size: %.0f x %.0f | Click 2nd corner to finish | Esc to cancel",
                    p0.x, p0.y, p0.z, w, l);
            }
        } else if (scene.IsFillAreaMode()) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.5f, 1.0f),
                "[FILL AREA TOOL ACTIVE] Click any floor or room surface in 3D viewport to auto-generate NavMesh | Esc to cancel");
        } else if (scene.IsBridgeMode()) {
            uint32_t a1 = scene.GetBridgeFirstArea();
            if (a1 == 0) {
                ImGui::TextColored(ImVec4(0.2f, 1.0f, 1.0f, 1.0f),
                    "[BRIDGE TOOL ACTIVE] Step 1: Click FIRST edge on any NavArea in 3D viewport | Esc to cancel");
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f),
                    "[BRIDGE TOOL ACTIVE] Step 2: First edge selected on Area #%u. Now click SECOND edge on another NavArea to bridge | Esc to cancel", a1);
            }
        } else if (selEntIdx >= 0 && selEntIdx < static_cast<int>(entCount)) {
            const auto* selEnt = scene.GetSelectedEntity();
            if (selEnt) {
                ImGui::Text("Map: %s | Entity #%d (%s) @ (%.0f, %.0f, %.0f) | Entities: %zu | Grid: %.0f [%s]",
                    bspName, selEnt->index, selEnt->classname.c_str(), selEnt->origin.x, selEnt->origin.y, selEnt->origin.z,
                    entCount, grid, snap ? "SNAP" : "FREE");
            }
        } else if (scene.GetSelectedAreaIDs().size() > 1) {
            ImGui::Text("Map: %s | Multi-Selection: %zu NavAreas Selected | Grid: %.0f [%s] | Esc to clear",
                bspName, scene.GetSelectedAreaIDs().size(), grid, snap ? "SNAP" : "FREE");
        } else if (selId != 0 && scene.HasNAV()) {
            const NavArea* sel = scene.GetNAV().GetAreaByID(selId);
            if (sel) {
                Vector3 c = sel->GetCenter();
                float w = sel->GetExtent().hi.x - sel->GetExtent().lo.x;
                float l = sel->GetExtent().hi.y - sel->GetExtent().lo.y;
                SelectedHandleType h = scene.GetSelectedHandle();
                if (h == HANDLE_NONE) h = scene.GetHoveredHandle();
                const char* hName = (h != HANDLE_NONE) ? GetHandleName(h) : "None";

                ImGui::Text("Map: %s | NavArea #%u [W: %.0f, L: %.0f @ (%.0f, %.0f, %.0f)] | Handle: %s | Ents: %zu | Grid: %.0f [%s]",
                    bspName, selId, w, l, c.x, c.y, c.z, hName, entCount, grid, snap ? "SNAP" : "FREE");
            }
        } else {
            if (entCount > 0) {
                ImGui::Text("Map: %s | NavAreas: %zu | Entities: %zu (CT: %d, T: %d, Obj: %d, Light: %d) | Grid: %.0f [%s] | Cam: (%.0f, %.0f, %.0f)",
                    bspName, areaCount, entCount, entR.GetSpawnCTCount(), entR.GetSpawnTCount(), entR.GetObjectiveCount(), entR.GetLightCount(),
                    grid, snap ? "SNAP" : "FREE", camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);
            } else {
                ImGui::Text("Map: %s | NavAreas: %zu | No Selection | Grid: %.0f [%s] | Cam: (%.0f, %.0f, %.0f)",
                    bspName, areaCount, grid, snap ? "SNAP" : "FREE", camera.GetPosition().x, camera.GetPosition().y, camera.GetPosition().z);
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void EditorUI::RenderHelpModal() {
    ImGui::OpenPopup("Controls and Shortcuts");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 420));

    if (ImGui::BeginPopupModal("Controls and Shortcuts", &m_showHelpModal, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Camera Navigation:");
        ImGui::BulletText("W / A / S / D: Fly forward / backward / left / right");
        ImGui::BulletText("E / Q: Fly up / down");
        ImGui::BulletText("Right-Click + Drag: First-person camera look");
        ImGui::BulletText("Mouse Wheel (Hold Right-Click): Change camera speed");
        ImGui::BulletText("Alt + Left-Click + Drag: Orbit selected area");
        ImGui::BulletText("F: Focus camera on selected area");
        ImGui::BulletText("Home: Reset camera to origin");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("3D View & Hammer Shortcuts:");
        ImGui::BulletText("Left-Click: Select area in 3D viewport / Drag Gizmo arrows or edges");
        ImGui::BulletText("G: Move / Grab selected area (smoothly follows cursor in 3D)");
        ImGui::BulletText("S: Scale selected area dimensions");
        ImGui::BulletText("X / Y / Z: Constrain Move/Scale strictly along X, Y, or Z axis");
        ImGui::BulletText("E: Extrude selected area edge (creates connected adjacent area)");
        ImGui::BulletText("Shift + Left-Drag Edge: Extrude edge interactively");
        ImGui::BulletText("Shift + X: Split selected area into two connected halves");
        ImGui::BulletText("Shift + M: Merge selected area with adjacent collinear area");
        ImGui::BulletText("[ / ]: Decrease / Increase grid snap size (1 to 512)");
        ImGui::BulletText("Shift + W: Toggle Grid Snapping");
        ImGui::BulletText("B: Bridge Mode (Click Edge 1 + Edge 2 to generate connecting area)");
        ImGui::BulletText("Shift + S: Snap selected area flush to neighbors (close micro-gaps)");
        ImGui::BulletText("R: Rotate area orientation 90 degrees");
        ImGui::BulletText("C: Connect Mode (Left-Click target: 2-Way, Shift+Click: 1-Way)");
        ImGui::BulletText("Shift + D: Duplicate selected area");
        ImGui::BulletText("X / Delete: Delete selected area");
        ImGui::BulletText("Space: Snap selected area elevation to BSP floor");
        ImGui::BulletText("Ctrl+Z / Ctrl+Y: Undo / Redo history");
        ImGui::BulletText("Ctrl+S: Save current navigation mesh");
        ImGui::BulletText("F3: Toggle Entity 3D visualization");
        ImGui::BulletText("F4: Cycle BSP Shading Mode (Textured / Solid / Wireframe / Ghost)");
        ImGui::BulletText("N: Draw Area Marquee Tool (Click 1st corner, move cursor, click 2nd corner to create)");
        ImGui::BulletText("Shift + Click / Ctrl + Click: Multi-select NavAreas in 3D viewport or explorer");
        ImGui::BulletText("Ctrl + A: Select All NavAreas");
        ImGui::BulletText("Escape: Clear selection / Cancel modal tool");

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::Button("Close", ImVec2(-1, 28))) {
            m_showHelpModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorUI::RenderWelcomeOverlay(EditorScene& scene) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 360), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("NavStudio - Welcome", nullptr, flags)) {
        ImGui::Text("NavStudio - GoldSrc BSP & NavMesh Editor");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Drag & Drop visual target box
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float boxHeight = 110.0f;
        ImVec2 boxSize = ImVec2(ImGui::GetContentRegionAvail().x, boxHeight);
        ImVec2 p1 = ImVec2(p0.x + boxSize.x, p0.y + boxSize.y);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(p0, p1, IM_COL32(28, 32, 42, 255), 6.0f);
        drawList->AddRect(p0, p1, IM_COL32(65, 120, 210, 255), 6.0f, 0, 2.0f);

        ImGui::SetCursorScreenPos(ImVec2(p0.x + 20, p0.y + 18));
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "DRAG & DROP BSP OR NAV FILE HERE");
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 20, p0.y + 46));
        ImGui::TextDisabled("Drop any GoldSrc .bsp map or .nav mesh directly into this window.");
        ImGui::SetCursorScreenPos(ImVec2(p0.x + 20, p0.y + 70));
        ImGui::TextDisabled("Corresponding .nav files in the same directory load automatically.");
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p1.y + 16.0f));

        ImGui::Text("Or select files manually:");
        ImGui::Spacing();

        if (ImGui::Button("Open BSP Map (.bsp)...", ImVec2(245, 32))) {
            std::string path = FileDialog::OpenFile(FileDialog::kBSPFilter, "Open GoldSrc BSP Map");
            if (!path.empty()) {
                scene.StartAsyncLoad(path);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Open NAV Mesh (.nav)...", ImVec2(245, 32))) {
            std::string path = FileDialog::OpenFile(FileDialog::kNAVFilter, "Open Navigation Mesh");
            if (!path.empty()) {
                scene.StartAsyncLoad(path);
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Enter File Path Directly...", ImVec2(-1, 28))) {
            m_showOpenPathModal = true;
            m_openPathBuffer[0] = '\0';
            m_openPathStatusMessage.clear();
        }
    }
    ImGui::End();
}

void EditorUI::RenderOpenPathModal(EditorScene& scene) {
    ImGui::OpenPopup("Open File by Path");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 210), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Open File by Path", &m_showOpenPathModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Enter the absolute or relative path to a .bsp or .nav file:");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(380);
        ImGui::InputText("##PathInput", m_openPathBuffer, sizeof(m_openPathBuffer));
        ImGui::SameLine();
        if (ImGui::Button("Browse...")) {
            std::string chosen = FileDialog::OpenFile(FileDialog::kAllFilter, "Select Map or Nav Mesh");
            if (!chosen.empty()) {
                std::strncpy(m_openPathBuffer, chosen.c_str(), sizeof(m_openPathBuffer) - 1);
                m_openPathBuffer[sizeof(m_openPathBuffer) - 1] = '\0';
            }
        }

        if (!m_openPathStatusMessage.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_openPathStatusMessage.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Load File", ImVec2(120, 28))) {
            std::string p = m_openPathBuffer;
            if (p.empty()) {
                m_openPathStatusMessage = "File path cannot be empty.";
            } else {
                scene.StartAsyncLoad(p);
                m_openPathStatusMessage.clear();
                m_showOpenPathModal = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 28))) {
            m_showOpenPathModal = false;
            m_openPathStatusMessage.clear();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderLoadingModal(const EditorScene& scene) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 190), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("NavStudio - Loading", nullptr, flags)) {
        ImGui::Text("Loading File: %s", scene.GetLoadingFilename().c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float progress = scene.GetLoadingProgress();
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        char progressText[32];
        std::snprintf(progressText, sizeof(progressText), "%.0f%%", progress * 100.0f);

        ImGui::ProgressBar(progress, ImVec2(-1, 26), progressText);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.45f, 0.8f, 1.0f, 1.0f), "%s", scene.GetLoadingStatus().c_str());

        int dotCount = static_cast<int>(ImGui::GetTime() * 4.0) % 4;
        std::string dots = std::string(dotCount, '.');
        ImGui::TextDisabled("Processing map geometry and navigation structures%s", dots.c_str());
    }
    ImGui::End();
}

void EditorUI::RenderLoadingErrorModal(EditorScene& scene) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImVec2 center = viewport->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 200), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("NavStudio - Error", nullptr, flags)) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.35f, 1.0f), "Error Loading Map:");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("%s", scene.GetLoadingError().c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Dismiss", ImVec2(120, 28))) {
            scene.ClearLoadingError();
        }
    }
    ImGui::End();
}

void EditorUI::RenderTransformHUD(EditorScene& scene, CommandManager& /*cmdMgr*/) {
    auto mode = scene.GetTransformMode();
    if (mode == EditorScene::TRANSFORM_NONE) return;

    ImGuiIO& io = ImGui::GetIO();
    ImVec2 centerPos(io.DisplaySize.x * 0.5f, io.DisplaySize.y - 70.0f);
    ImGui::SetNextWindowPos(centerPos, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.88f);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                            ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                            ImGuiWindowFlags_NoNav;

    if (ImGui::Begin("TransformStatusHUD", nullptr, flags)) {
        if (mode == EditorScene::TRANSFORM_TRANSLATE) {
            const char* axisStr = "FREE";
            if (scene.GetTransformAxis() == EditorScene::AXIS_X) axisStr = "X-AXIS (EAST/WEST)";
            else if (scene.GetTransformAxis() == EditorScene::AXIS_Y) axisStr = "Y-AXIS (NORTH/SOUTH)";
            else if (scene.GetTransformAxis() == EditorScene::AXIS_Z) axisStr = "Z-AXIS (ELEVATION)";

            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "GRAB / MOVE: [%s]", axisStr);
            ImGui::SameLine();
            ImGui::TextDisabled("| Keys: [X][Y][Z] Constrain | [L-Click / Enter] Confirm | [R-Click / Esc] Cancel");
        } else if (mode == EditorScene::TRANSFORM_SCALE) {
            const char* axisStr = "UNIFORM";
            if (scene.GetTransformAxis() == EditorScene::AXIS_X) axisStr = "X (WIDTH)";
            else if (scene.GetTransformAxis() == EditorScene::AXIS_Y) axisStr = "Y (LENGTH)";

            ImGui::TextColored(ImVec4(0.2f, 0.95f, 0.4f, 1.0f), "SCALE: [%s]", axisStr);
            ImGui::SameLine();
            ImGui::TextDisabled("| Keys: [X][Y] Constrain | [L-Click / Enter] Confirm | [R-Click / Esc] Cancel");
        } else if (mode == EditorScene::TRANSFORM_CONNECT) {
            uint32_t hover = scene.GetConnectHoverArea();
            if (hover != 0) {
                ImGui::TextColored(ImVec4(0.0f, 0.95f, 1.0f, 1.0f), "CONNECT MODE -> Hovering Area #%u", hover);
            } else {
                ImGui::TextColored(ImVec4(0.0f, 0.95f, 1.0f, 1.0f), "CONNECT MODE: Click target area in 3D");
            }
            ImGui::SameLine();
            ImGui::TextDisabled("| [Left-Click: 2-Way | Shift+Click: 1-Way] | [C / Esc: Exit]");
        }
    }
    ImGui::End();
}

void EditorUI::RenderGenerateModal(EditorScene& scene) {
    ImGui::OpenPopup("Auto-Generate NavMesh");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(460, 420), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Auto-Generate NavMesh", &m_showGenerateModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string mapName = scene.GetBSPPath();
        size_t slash = mapName.find_last_of("/\\");
        if (slash != std::string::npos) mapName = mapName.substr(slash + 1);

        ImGui::Text("Map: %s", mapName.c_str());
        ImGui::TextDisabled("Analyzes BSP walkability geometry and generates Valve .nav mesh.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Generation Parameters:");
        ImGui::SliderFloat("Grid Step Size", &m_genOptions.stepSize, 15.0f, 40.0f, "%.1f units");
        ImGui::SliderFloat("Max Step Height", &m_genOptions.maxStepHeight, 8.0f, 32.0f, "%.1f units");
        ImGui::SliderFloat("Max Jump Height", &m_genOptions.maxJumpHeight, 18.0f, 64.0f, "%.1f units");
        ImGui::SliderFloat("Max Drop Height", &m_genOptions.maxDrop, 100.0f, 600.0f, "%.0f units");

        ImGui::Spacing();
        ImGui::Checkbox("Detect Crouch Passages", &m_genOptions.generateCrouch);
        ImGui::Checkbox("Connect Jump-Down Drops", &m_genOptions.generateJumpConnections);
        ImGui::Checkbox("Link Ladder Entities", &m_genOptions.generateLadders);
        ImGui::Checkbox("Merge Coplanar Adjacent Areas", &m_genOptions.mergeAreas);

        if (!m_generateStatusText.empty()) {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", m_generateStatusText.c_str());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Generate NavMesh", ImVec2(160, 28))) {
            bool ok = scene.GenerateNavMesh(m_genOptions);
            if (ok) {
                m_showGenerateModal = false;
                ImGui::CloseCurrentPopup();
            } else {
                m_generateStatusText = "Generation failed. Verify map geometry and spawns.";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 28))) {
            m_showGenerateModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderBatchGenerateModal(EditorScene& /*scene*/) {
    ImGui::OpenPopup("Batch Mass-Produce NavMeshes");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(680, 520), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Batch Mass-Produce NavMeshes", &m_showBatchGenerateModal, ImGuiWindowFlags_None)) {
        ImGui::Text("Batch NavMesh Mass-Production");
        ImGui::TextDisabled("Generate navigation meshes for multiple GoldSrc BSP maps concurrently.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Maps Directory (containing .bsp files):");
        ImGui::SetNextItemWidth(-90);
        ImGui::InputText("##BatchMapsDir", m_batchMapDirBuffer, sizeof(m_batchMapDirBuffer));
        ImGui::SameLine();
        if (ImGui::Button("Browse...##MapsDir", ImVec2(80, 22))) {
            std::string selected = FileDialog::OpenFolder("Select Maps Directory");
            if (!selected.empty()) {
                std::strncpy(m_batchMapDirBuffer, selected.c_str(), sizeof(m_batchMapDirBuffer) - 1);
                m_batchMapDirBuffer[sizeof(m_batchMapDirBuffer) - 1] = '\0';
            }
        }

        ImGui::Text("Output Directory (leave empty to save alongside BSP files):");
        ImGui::SetNextItemWidth(-90);
        ImGui::InputText("##BatchOutDir", m_batchOutDirBuffer, sizeof(m_batchOutDirBuffer));
        ImGui::SameLine();
        if (ImGui::Button("Browse...##OutDir", ImVec2(80, 22))) {
            std::string selected = FileDialog::OpenFolder("Select Output Directory");
            if (!selected.empty()) {
                std::strncpy(m_batchOutDirBuffer, selected.c_str(), sizeof(m_batchOutDirBuffer) - 1);
                m_batchOutDirBuffer[sizeof(m_batchOutDirBuffer) - 1] = '\0';
            }
        }

        ImGui::Spacing();
        ImGui::Columns(2, "BatchOptionsColumns", false);
        ImGui::SliderFloat("Step Size", &m_genOptions.stepSize, 15.0f, 40.0f, "%.1f");
        ImGui::SliderInt("Threads", &m_batchThreads, 1, 32);
        ImGui::NextColumn();
        ImGui::Checkbox("Overwrite Existing .nav", &m_batchOverwrite);
        ImGui::Checkbox("Recursive Directory Scan", &m_batchRecursive);
        ImGui::Columns(1);

        ImGui::Spacing();
        if (ImGui::Button("Scan Maps", ImVec2(120, 24)) && !m_batchRunning) {
            m_batchItems.clear();
            std::string dir = m_batchMapDirBuffer;
            if (!dir.empty() && fs::exists(dir)) {
                try {
                    auto scanDir = [&](const fs::path& p) {
                        if (fs::is_regular_file(p) && p.extension() == ".bsp") {
                            NavGenerator::BatchItem item;
                            item.bspPath = p.string();
                            fs::path outP = (m_batchOutDirBuffer[0] != '\0')
                                ? fs::path(m_batchOutDirBuffer) / (p.stem().string() + ".nav")
                                : p.parent_path() / (p.stem().string() + ".nav");
                            item.navPath = outP.string();
                            m_batchItems.push_back(item);
                        }
                    };

                    if (m_batchRecursive) {
                        for (const auto& entry : fs::recursive_directory_iterator(dir)) {
                            scanDir(entry.path());
                        }
                    } else {
                        for (const auto& entry : fs::directory_iterator(dir)) {
                            scanDir(entry.path());
                        }
                    }
                    std::sort(m_batchItems.begin(), m_batchItems.end(), [](const auto& a, const auto& b) {
                        return a.bspPath < b.bspPath;
                    });
                } catch (...) {}
            }
        }
        ImGui::SameLine();
        ImGui::Text("Found %zu maps", m_batchItems.size());

        // Progress bar
        if (m_batchTotalCount > 0) {
            float frac = static_cast<float>(m_batchCompletedCount) / m_batchTotalCount;
            char progressBuf[64];
            std::snprintf(progressBuf, sizeof(progressBuf), "%zu / %zu Maps (%.0f%%)",
                          m_batchCompletedCount, m_batchTotalCount, frac * 100.0f);
            ImGui::ProgressBar(frac, ImVec2(-1, 20), progressBuf);
        }

        // Map list table
        ImGui::BeginChild("BatchItemsTableChild", ImVec2(-1, 180), true);
        if (ImGui::BeginTable("BatchMapTable", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY)) {
            ImGui::TableSetupColumn("Map", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Target NAV", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableHeadersRow();

            for (const auto& item : m_batchItems) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                fs::path bp(item.bspPath);
                ImGui::Text("%s", bp.filename().string().c_str());

                ImGui::TableSetColumnIndex(1);
                fs::path np(item.navPath);
                ImGui::Text("%s", np.filename().string().c_str());

                ImGui::TableSetColumnIndex(2);
                if (item.result.success) {
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Done (%zu areas)", item.result.areasGenerated);
                } else if (!item.result.errorMessage.empty()) {
                    ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", item.result.errorMessage.c_str());
                } else {
                    ImGui::TextDisabled("Ready");
                }
            }
            ImGui::EndTable();
        }
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Start Batch Generation", ImVec2(180, 30)) && !m_batchRunning && !m_batchItems.empty()) {
            m_batchRunning = true;
            m_batchCompletedCount = 0;
            m_batchTotalCount = m_batchItems.size();

            std::vector<std::string> bspList;
            for (const auto& it : m_batchItems) bspList.push_back(it.bspPath);
            std::string outDir = m_batchOutDirBuffer;
            NavGenerateOptions opts = m_genOptions;
            opts.maxThreads = m_batchThreads;
            bool overwrite = m_batchOverwrite;

            std::thread([this, bspList, outDir, opts, overwrite]() {
                auto res = NavGenerator::GenerateBatch(
                    bspList, outDir, opts, overwrite,
                    [this](size_t done, size_t /*total*/, const NavGenerator::BatchItem& item) {
                        m_batchCompletedCount = done;
                        for (auto& bi : m_batchItems) {
                            if (bi.bspPath == item.bspPath) {
                                bi.result = item.result;
                                break;
                            }
                        }
                    }
                );
                (void)res;
                m_batchRunning = false;
            }).detach();
        }

        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(100, 30)) && !m_batchRunning) {
            m_showBatchGenerateModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::PromptQuit(EditorScene& scene, CommandManager& cmdMgr) {
    if (!CheckUnsavedChanges(scene, cmdMgr, PENDING_QUIT)) {
        m_requestQuit = true;
    }
}

bool EditorUI::CheckUnsavedChanges(EditorScene& scene, CommandManager& cmdMgr, PendingAction action, const std::string& path) {
    if (scene.HasNAV() && (cmdMgr.HasUnsavedChanges() || scene.IsModified())) {
        m_pendingAction = action;
        m_pendingPath = path;
        m_showUnsavedModal = true;
        return true;
    }
    return false;
}

void EditorUI::ExecutePendingAction(EditorScene& scene, CommandManager& cmdMgr) {
    PendingAction act = m_pendingAction;
    std::string p = m_pendingPath;
    m_pendingAction = PENDING_NONE;
    m_pendingPath.clear();

    switch (act) {
        case PENDING_QUIT:
            m_requestQuit = true;
            break;
        case PENDING_OPEN_BSP:
            if (!p.empty()) {
                cmdMgr.Clear();
                scene.StartAsyncLoad(p);
            }
            break;
        case PENDING_OPEN_NAV:
            if (!p.empty()) {
                cmdMgr.Clear();
                scene.StartAsyncLoad(p);
            }
            break;
        case PENDING_RECENT:
            if (!p.empty()) {
                cmdMgr.Clear();
                scene.StartAsyncLoad(p);
            }
            break;
        case PENDING_UNLOAD_NAV:
            cmdMgr.Clear();
            scene.UnloadNAV();
            break;
        case PENDING_UNLOAD_BSP:
            cmdMgr.Clear();
            scene.UnloadBSP();
            break;
        default:
            break;
    }
}

void EditorUI::RenderUnsavedModal(EditorScene& scene, CommandManager& cmdMgr) {
    if (m_showUnsavedModal) {
        ImGui::OpenPopup("Unsaved Changes##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Unsaved Changes##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Spacing();
        ImGui::TextWrapped("The current navigation mesh has unsaved changes.\nDo you want to save before proceeding?");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Save##ModalBtn", ImVec2(120, 28))) {
            bool saved = false;
            std::string saveTarget = scene.GetNAVPath();
            if (!saveTarget.empty()) {
                saved = scene.SaveNAV();
            } else {
                std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh");
                if (!path.empty()) {
                    saved = scene.SaveNAV(path);
                    saveTarget = path;
                }
            }
            if (saved) {
                cmdMgr.MarkSaved();
                scene.SetModified(false);
                m_showUnsavedModal = false;
                ImGui::CloseCurrentPopup();
                ExecutePendingAction(scene, cmdMgr);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Don't Save##ModalBtn", ImVec2(120, 28))) {
            m_showUnsavedModal = false;
            ImGui::CloseCurrentPopup();
            ExecutePendingAction(scene, cmdMgr);
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel##ModalBtn", ImVec2(100, 28))) {
            m_pendingAction = PENDING_NONE;
            m_pendingPath.clear();
            m_showUnsavedModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderSaveSuccessModal() {
    if (m_showSaveSuccessModal) {
        ImGui::OpenPopup("Save Successful##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Save Successful##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", m_saveSuccessMessage.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("OK##SaveOkBtn", ImVec2(100, 26))) {
            m_showSaveSuccessModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorUI::RenderAnalyzerModal() {
    if (m_showAnalyzerModal) {
        ImGui::OpenPopup("Auto-Crouch & Obstacle Flag Analyzer##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Auto-Crouch & Obstacle Flag Analyzer##Modal", &m_showAnalyzerModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.3f, 1.0f), "Automated Flag Scan Complete");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Total Areas Scanned: %zu", m_analyzerStats.totalScanned);
        ImGui::Spacing();
        ImGui::Text("  - Low Headroom (< 72u): %zu Crouch flags assigned", m_analyzerStats.crouchCount);
        ImGui::Text("  - Narrow Passages (< 48u): %zu Precise flags assigned", m_analyzerStats.preciseCount);
        ImGui::Text("  - Step Obstacles (> 18u): %zu Jump flags assigned", m_analyzerStats.jumpCount);
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Total Areas Modified: %zu", m_analyzerStats.totalModified);
        ImGui::Spacing();

        if (ImGui::Button("Close##AnalyzerBtn", ImVec2(100, 26))) {
            m_showAnalyzerModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void EditorUI::RenderOptimizeModal() {
    if (m_showOptimizeModal) {
        ImGui::OpenPopup("Mesh Optimization Results##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Mesh Optimization Results##Modal", &m_showOptimizeModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 0.3f, 1.0f), "Coplanar Mesh Simplification Complete");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Initial NavArea Count: %zu", m_optimizeStats.initialAreaCount);
        ImGui::Text("Merged Area Pairs:     %zu", m_optimizeStats.mergedCount);
        ImGui::Text("Final NavArea Count:   %zu", m_optimizeStats.finalAreaCount);
        ImGui::Spacing();
        if (m_optimizeStats.initialAreaCount > 0 && m_optimizeStats.mergedCount > 0) {
            float pct = (static_cast<float>(m_optimizeStats.mergedCount) / static_cast<float>(m_optimizeStats.initialAreaCount)) * 100.0f;
            ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Area Count Reduced by: %.1f%%", pct);
        } else {
            ImGui::TextColored(ImVec4(0.85f, 0.85f, 0.85f, 1.0f), "Mesh is already maximally simplified.");
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Close##OptimizeBtn", ImVec2(100, 26))) {
            m_showOptimizeModal = false;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

