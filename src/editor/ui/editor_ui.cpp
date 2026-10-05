#include "editor/ui/editor_ui.h"
#include <imgui.h>
#include "editor/commands/nav_commands.h"
#include <cstdio>
#include <cstring>
#include <algorithm>

EditorUI::EditorUI()
    : m_mouseOverUI(false)
    , m_requestQuit(false)
    , m_showHelpModal(false)
{
    m_searchFilter[0] = '\0';
    m_placeEditBuffer[0] = '\0';
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

    if (m_showHelpModal) {
        RenderHelpModal();
    }
}

void EditorUI::RenderMenuBar(EditorScene& scene, Camera& camera, CommandManager& cmdMgr) {
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Open BSP Map...", "Ctrl+O")) {
                m_openBSPRequested = "prompt";
            }
            if (ImGui::MenuItem("Open NAV Mesh...", "Ctrl+Shift+O")) {
                m_openNAVRequested = "prompt";
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save NAV Mesh", "Ctrl+S", false, scene.HasNAV())) {
                scene.SaveNAV();
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
    ImGui::SetNextWindowSize(ImVec2(160, 240), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Tool Palette")) {
        NavArea* sel = scene.GetSelectedArea();

        if (ImGui::Button("Select [V]", ImVec2(-1, 26))) {
            // Select mode
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

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
        bool crouch = (flags & NAV_MESH_CROUCH) != 0;
        bool jump = (flags & NAV_MESH_JUMP) != 0;
        bool precise = (flags & NAV_MESH_PRECISE) != 0;
        bool noJump = (flags & NAV_MESH_NO_JUMP) != 0;
        bool transient = (flags & NAV_MESH_TRANSIENT) != 0;

        bool changed = false;
        if (ImGui::Checkbox("Crouch", &crouch)) changed = true;
        if (ImGui::Checkbox("Jump", &jump)) changed = true;
        if (ImGui::Checkbox("Precise", &precise)) changed = true;
        if (ImGui::Checkbox("No Jump", &noJump)) changed = true;
        if (ImGui::Checkbox("Transient (Blocked)", &transient)) changed = true;

        if (changed) {
            uint8_t newFlags = 0;
            if (crouch) newFlags |= NAV_MESH_CROUCH;
            if (jump) newFlags |= NAV_MESH_JUMP;
            if (precise) newFlags |= NAV_MESH_PRECISE;
            if (noJump) newFlags |= NAV_MESH_NO_JUMP;
            if (transient) newFlags |= NAV_MESH_TRANSIENT;

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
