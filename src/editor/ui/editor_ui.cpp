#include "editor/ui/editor_ui.h"
#include <imgui.h>
#include "editor/commands/nav_commands.h"
#include "editor/ui/file_dialog.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

EditorUI::EditorUI()
    : m_mouseOverUI(false)
    , m_requestQuit(false)
    , m_showHelpModal(false)
    , m_showOpenPathModal(false)
    , m_openPathType(0)
{
    m_searchFilter[0] = '\0';
    m_placeEditBuffer[0] = '\0';
    m_openPathBuffer[0] = '\0';
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
                scene.LoadBSP(path);
            }
        } else if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
            std::string path = FileDialog::OpenFile(FileDialog::kNAVFilter, "Open Navigation Mesh");
            if (!path.empty()) {
                scene.LoadNAV(path);
            }
        } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false) && scene.HasNAV()) {
            if (!scene.GetNAVPath().empty()) {
                scene.SaveNAV();
            } else {
                std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh");
                if (!path.empty()) {
                    scene.SaveNAV(path);
                }
            }
        } else if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false) && scene.HasNAV()) {
            std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh As");
            if (!path.empty()) {
                scene.SaveNAV(path);
            }
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

    if (!scene.HasBSP() && !scene.HasNAV()) {
        RenderWelcomeOverlay(scene);
    }

    if (m_showHelpModal) {
        RenderHelpModal();
    }

    if (m_showOpenPathModal) {
        RenderOpenPathModal(scene);
    }
}

void EditorUI::RenderMenuBar(EditorScene& scene, Camera& camera, CommandManager& cmdMgr) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open BSP Map...", "Ctrl+O")) {
                std::string path = FileDialog::OpenFile(FileDialog::kBSPFilter, "Open GoldSrc BSP Map");
                if (!path.empty()) {
                    scene.LoadBSP(path);
                }
            }
            if (ImGui::MenuItem("Open NAV Mesh...", "Ctrl+Shift+O")) {
                std::string path = FileDialog::OpenFile(FileDialog::kNAVFilter, "Open Navigation Mesh");
                if (!path.empty()) {
                    scene.LoadNAV(path);
                }
            }
            if (ImGui::MenuItem("Open File from Path...", nullptr)) {
                m_showOpenPathModal = true;
                m_openPathBuffer[0] = '\0';
                m_openPathStatusMessage.clear();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save NAV Mesh", "Ctrl+S", false, scene.HasNAV())) {
                if (!scene.GetNAVPath().empty()) {
                    scene.SaveNAV();
                } else {
                    std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh");
                    if (!path.empty()) {
                        scene.SaveNAV(path);
                    }
                }
            }
            if (ImGui::MenuItem("Save NAV Mesh As...", "Ctrl+Shift+S", false, scene.HasNAV())) {
                std::string path = FileDialog::SaveFile(FileDialog::kNAVFilter, "nav", "Save Navigation Mesh As");
                if (!path.empty()) {
                    scene.SaveNAV(path);
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Exit", "Alt+F4")) {
                m_requestQuit = true;
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

            ImGui::Separator();
            if (ImGui::BeginMenu("BSP Shading Mode")) {
                if (ImGui::MenuItem("Solid / Shaded", nullptr, scene.GetBSPMode() == BSP_RENDER_SOLID)) {
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
            if (ImGui::MenuItem("Snap Selected Area to Floor", "S", false, sel != nullptr && scene.HasBSP())) {
                cmdMgr.ExecuteCommand(std::make_unique<CmdSnapAreaToFloor>(&scene, sel->GetID()));
            }

            if (ImGui::MenuItem("Focus on Selection", "F", false, sel != nullptr)) {
                camera.FocusOn(sel->GetCenter());
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
    ImGui::SetNextWindowSize(ImVec2(180, 340), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Tool Palette")) {
        ImGui::Text("File Actions:");
        if (ImGui::Button("Open BSP Map...", ImVec2(-1, 26))) {
            std::string path = FileDialog::OpenFile(FileDialog::kBSPFilter, "Open GoldSrc BSP Map");
            if (!path.empty()) {
                scene.LoadBSP(path);
            }
        }
        if (ImGui::Button("Open NAV Mesh...", ImVec2(-1, 26))) {
            std::string path = FileDialog::OpenFile(FileDialog::kNAVFilter, "Open Navigation Mesh");
            if (!path.empty()) {
                scene.LoadNAV(path);
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

        ImGui::Text("Edit Tools:");
        NavArea* sel = scene.GetSelectedArea();

        if (ImGui::Button("Select [V]", ImVec2(-1, 26))) {
            // Select mode
        }

        if (ImGui::Button("Snap to Floor [S]", ImVec2(-1, 26))) {
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
        ImGui::Text("BSP View:");
        if (ImGui::RadioButton("Solid", scene.GetBSPMode() == BSP_RENDER_SOLID)) {
            scene.SetBSPMode(BSP_RENDER_SOLID);
        }
        if (ImGui::RadioButton("Wireframe", scene.GetBSPMode() == BSP_RENDER_WIREFRAME)) {
            scene.SetBSPMode(BSP_RENDER_WIREFRAME);
        }
        if (ImGui::RadioButton("Ghost (X-Ray)", scene.GetBSPMode() == BSP_RENDER_GHOST)) {
            scene.SetBSPMode(BSP_RENDER_GHOST);
        }
    }
    ImGui::End();
}


void EditorUI::RenderHierarchy(EditorScene& scene, Camera& camera) {
    ImGui::SetNextWindowSize(ImVec2(240, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Area Hierarchy")) {
        if (!scene.HasNAV()) {
            ImGui::TextDisabled("No NAV mesh loaded.");
            ImGui::End();
            return;
        }

        ImGui::InputTextWithHint("##Search", "Search Area ID or Place...", m_searchFilter, sizeof(m_searchFilter));
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

            bool isSelected = (scene.GetSelectedAreaID() == id);
            if (ImGui::Selectable(label, isSelected)) {
                scene.SelectArea(id);
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
    ImGui::End();
}

void EditorUI::RenderInspector(EditorScene& scene, Camera& camera, CommandManager& cmdMgr) {
    ImGui::SetNextWindowSize(ImVec2(280, 450), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Property Inspector")) {
        NavArea* area = scene.GetSelectedArea();
        if (!area) {
            ImGui::TextDisabled("No area selected.\nClick an area in the 3D viewport or hierarchy.");
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
        ImGui::Text("Coordinates & Dimensions:");

        const NavExtent& extent = area->GetExtent();
        float width = extent.hi.x - extent.lo.x;
        float length = extent.hi.y - extent.lo.y;

        ImGui::BulletText("X: [%.1f, %.1f] (W: %.1f)", extent.lo.x, extent.hi.x, width);
        ImGui::BulletText("Y: [%.1f, %.1f] (L: %.1f)", extent.lo.y, extent.hi.y, length);
        ImGui::BulletText("Z (NE): %.1f", area->GetNEZ());
        ImGui::BulletText("Z (SW): %.1f", area->GetSWZ());
        ImGui::BulletText("Center: (%.1f, %.1f, %.1f)", area->GetCenter().x, area->GetCenter().y, area->GetCenter().z);

        ImGui::Spacing();
        if (ImGui::Button("Snap to Floor [S]", ImVec2(-1, 26))) {
            if (scene.HasBSP()) {
                cmdMgr.ExecuteCommand(std::make_unique<CmdSnapAreaToFloor>(&scene, id));
            }
        }

        if (ImGui::Button("Focus Viewport [F]", ImVec2(-1, 26))) {
            camera.FocusOn(area->GetCenter());
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Connections:");

        static const char* dirNames[] = { "North", "East", "South", "West" };
        for (int d = 0; d < 4; ++d) {
            const auto& conns = area->GetAdjacentList(static_cast<NavDirType>(d));
            if (conns.empty()) continue;

            ImGui::BulletText("%s (%zu):", dirNames[d], conns.size());
            for (const auto& conn : conns) {
                if (!conn.area) continue;
                uint32_t targetId = conn.area->GetID();
                bool twoWay = conn.area->IsConnected(area);

                ImGui::Indent();
                char connLabel[64];
                std::snprintf(connLabel, sizeof(connLabel), "-> Area #%u (%s)", targetId, twoWay ? "2-Way" : "1-Way");
                if (ImGui::SmallButton(connLabel)) {
                    scene.SelectArea(targetId);
                }
                ImGui::Unindent();
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
        const char* bspName = scene.HasBSP() ? scene.GetBSP().GetMapName().c_str() : "No Map Loaded";
        size_t areaCount = scene.HasNAV() ? scene.GetNAV().GetAreaCount() : 0;
        uint32_t selId = scene.GetSelectedAreaID();

        Vector3 camPos = camera.GetPosition();

        if (selId != 0) {
            ImGui::Text("Map: %s | NavAreas: %zu | Selected: #%u | Camera: (%.1f, %.1f, %.1f) | Speed: %.0f",
                bspName, areaCount, selId, camPos.x, camPos.y, camPos.z, camera.GetSpeed());
        } else {
            ImGui::Text("Map: %s | NavAreas: %zu | No Selection | Camera: (%.1f, %.1f, %.1f) | Speed: %.0f",
                bspName, areaCount, camPos.x, camPos.y, camPos.z, camera.GetSpeed());
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void EditorUI::RenderHelpModal() {
    ImGui::OpenPopup("Controls and Shortcuts");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 360));

    if (ImGui::BeginPopupModal("Controls and Shortcuts", &m_showHelpModal, ImGuiWindowFlags_NoResize)) {
        ImGui::Text("Camera Navigation:");
        ImGui::BulletText("W / A / S / D: Fly forward / backward / left / right");
        ImGui::BulletText("E / Q: Fly up / down");
        ImGui::BulletText("Right-Click + Drag: Look around");
        ImGui::BulletText("Mouse Wheel (Hold Right-Click): Change camera speed");
        ImGui::BulletText("Alt + Left-Click + Drag: Orbit selected area");
        ImGui::BulletText("F: Focus camera on selected area");
        ImGui::BulletText("Home: Reset camera to origin");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Editing Shortcuts:");
        ImGui::BulletText("Left-Click: Select NavArea in 3D viewport");
        ImGui::BulletText("Double-Click Hierarchy: Focus camera on area");
        ImGui::BulletText("S: Snap selected area elevation to floor");
        ImGui::BulletText("Ctrl+Z / Ctrl+Y: Undo / Redo");
        ImGui::BulletText("Ctrl+S: Save current navigation mesh");

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
    ImGui::SetNextWindowSize(ImVec2(480, 270), ImGuiCond_Always);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoSavedSettings;

    if (ImGui::Begin("NavStudio - Welcome", nullptr, flags)) {
        ImGui::TextWrapped("No GoldSrc BSP map or Navigation Mesh is currently loaded.");
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Drag & drop a .bsp or .nav file directly onto this window,");
        ImGui::TextDisabled("or choose an action below to get started:");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Open BSP Map (.bsp)...", ImVec2(-1, 32))) {
            std::string path = FileDialog::OpenFile(FileDialog::kBSPFilter, "Open GoldSrc BSP Map");
            if (!path.empty()) {
                scene.LoadBSP(path);
            }
        }

        if (ImGui::Button("Open NAV Mesh (.nav)...", ImVec2(-1, 32))) {
            std::string path = FileDialog::OpenFile(FileDialog::kNAVFilter, "Open Navigation Mesh");
            if (!path.empty()) {
                scene.LoadNAV(path);
            }
        }

        if (ImGui::Button("Enter File Path Directly...", ImVec2(-1, 30))) {
            m_showOpenPathModal = true;
            m_openPathBuffer[0] = '\0';
            m_openPathStatusMessage.clear();
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Loading a BSP map automatically searches for and loads matching .nav.");
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
                std::string lowerP = p;
                std::transform(lowerP.begin(), lowerP.end(), lowerP.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });

                bool ok = false;
                if (lowerP.length() >= 4 && lowerP.compare(lowerP.length() - 4, 4, ".bsp") == 0) {
                    ok = scene.LoadBSP(p);
                } else if (lowerP.length() >= 4 && lowerP.compare(lowerP.length() - 4, 4, ".nav") == 0) {
                    ok = scene.LoadNAV(p);
                } else {
                    ok = scene.LoadBSP(p) || scene.LoadNAV(p);
                }

                if (ok) {
                    m_openPathStatusMessage.clear();
                    m_showOpenPathModal = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    m_openPathStatusMessage = "Failed to load file. Please check path and file validity.";
                }
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
