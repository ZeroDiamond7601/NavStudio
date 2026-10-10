#include "editor/ui/editor_ui.h"
#include "editor/scene/editor_scene.h"
#include "editor/commands/command_manager.h"
#include "editor/ui/file_dialog.h"
#include "waypoint/waypoint_types.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>

void EditorUI::RenderWaypointInspector(EditorScene& scene, CommandManager& /*cmdMgr*/) {
    uint32_t selId = scene.GetSelectedWaypointID();
    WaypointNode* node = scene.GetWaypoints().GetNode(selId);
    if (!node) {
        scene.SelectWaypoint(0);
        return;
    }

    const char* botNames[] = { "CS-EBOT", "SyPB", "YaPB", "POD-Bot mm" };
    const char* modNames[] = { "Standard CS", "Zombie Plague", "Deathmatch" };
    int curBot = static_cast<int>(scene.GetWaypoints().GetActiveBot());
    int curMod = static_cast<int>(scene.GetWaypoints().GetActiveMod());

    ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "[WAYPOINT #%u]", node->id);
    ImGui::SameLine();
    ImGui::TextDisabled("(%s - %s)", botNames[std::clamp(curBot, 0, 3)], modNames[std::clamp(curMod, 0, 2)]);
    ImGui::Separator();

    // Coordinates
    float pos[3] = { node->origin.x, node->origin.y, node->origin.z };
    if (ImGui::DragFloat3("Origin (X/Y/Z)", pos, 1.0f)) {
        node->origin = Vector3(pos[0], pos[1], pos[2]);
        scene.RebuildWaypointRenderer();
    }

    // Radius
    if (ImGui::SliderFloat("Radius", &node->radius, 0.0f, 255.0f, "%.0f units")) {
        scene.RebuildWaypointRenderer();
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Navigation tolerance zone around node (0-255 units)");
    }

    const float rPresets[] = { 0.0f, 8.0f, 16.0f, 32.0f, 48.0f, 64.0f, 80.0f, 96.0f, 128.0f };
    float rBtnW = (ImGui::GetContentRegionAvail().x - 16.0f) / 5.0f;
    for (int i = 0; i < 5; ++i) {
        if (i > 0) ImGui::SameLine(0.0f, 4.0f);
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "%.0f##insp_rad%d", rPresets[i], i);
        if (std::abs(node->radius - rPresets[i]) < 0.1f) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.50f, 0.15f, 1.0f));
        if (ImGui::Button(lbl, ImVec2(rBtnW, 20))) {
            node->radius = rPresets[i];
            scene.RebuildWaypointRenderer();
        }
        if (std::abs(node->radius - rPresets[i]) < 0.1f) ImGui::PopStyleColor();
    }
    for (int i = 5; i < 9; ++i) {
        if (i > 5) ImGui::SameLine(0.0f, 4.0f);
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "%.0f##insp_rad%d", rPresets[i], i);
        if (std::abs(node->radius - rPresets[i]) < 0.1f) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.50f, 0.15f, 1.0f));
        if (ImGui::Button(lbl, ImVec2(rBtnW, 20))) {
            node->radius = rPresets[i];
            scene.RebuildWaypointRenderer();
        }
        if (std::abs(node->radius - rPresets[i]) < 0.1f) ImGui::PopStyleColor();
    }
    if (ImGui::Button("Calculate Wayzone (Raycast)", ImVec2(-1, 22))) {
        if (scene.HasBSP()) {
            scene.GetWaypoints().CalculateWayzone(node->id, &scene.GetBSP());
            scene.RebuildWaypointRenderer();
            scene.ShowToast("Calculated wayzone radius for Waypoint #" + std::to_string(node->id));
        }
    }

    // Camp Pitch & Yaw
    ImGui::Spacing();
    ImGui::Text("Aim / Camp Orientation:");
    if (ImGui::SliderFloat("Pitch", &node->campPitch, -89.0f, 89.0f, "%.1f deg")) {
        scene.RebuildWaypointRenderer();
    }
    if (ImGui::SliderFloat("Yaw", &node->campYaw, 0.0f, 360.0f, "%.1f deg")) {
        scene.RebuildWaypointRenderer();
    }
    if (ImGui::Button("0 deg (E)", ImVec2(50, 20))) { node->campYaw = 0.0f; scene.RebuildWaypointRenderer(); }
    ImGui::SameLine();
    if (ImGui::Button("90 deg (N)", ImVec2(50, 20))) { node->campYaw = 90.0f; scene.RebuildWaypointRenderer(); }
    ImGui::SameLine();
    if (ImGui::Button("180 deg (W)", ImVec2(50, 20))) { node->campYaw = 180.0f; scene.RebuildWaypointRenderer(); }
    ImGui::SameLine();
    if (ImGui::Button("270 deg (S)", ImVec2(50, 20))) { node->campYaw = 270.0f; scene.RebuildWaypointRenderer(); }

    int meshVal = static_cast<int>(node->mesh);
    if (ImGui::SliderInt("Mesh Group", &meshVal, 0, 255)) {
        node->mesh = static_cast<uint8_t>(meshVal);
    }
    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Camp mesh cluster group identifier");
    }

    ImGui::Spacing();
    ImGui::Separator();

    auto FlagBox = [&](const char* label, uint32_t flag, const char* tip = nullptr) {
        bool checked = (node->flags & flag) != 0;
        if (ImGui::Checkbox(label, &checked)) {
            if (checked) node->flags |= flag;
            else node->flags &= ~flag;
            scene.RebuildWaypointRenderer();
        }
        if (tip && ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tip);
        }
    };

    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Tactical & Mission Objectives:");
    FlagBox("Mission Goal (Bomb/Hostage)", WPT_FLAG_GOAL, "Bomb target or hostage holding point");
    FlagBox("Camping Perch", WPT_FLAG_CAMP, "Ambush and defensive perch");
    FlagBox("Sniper Nest", WPT_FLAG_SNIPER, "Long-range sniper vantage point");
    FlagBox("Hostage Rescue Zone", WPT_FLAG_RESCUE, "CT rescue extraction point");
    FlagBox("Crouch / Duck", WPT_FLAG_CROUCH, "Requires ducking through low passage");
    FlagBox("Ladder Climb", WPT_FLAG_LADDER, "Ladder navigation point");
    FlagBox("Jump Required", WPT_FLAG_JUMP, "Requires single jump across obstacle");
    FlagBox("Double Jump Boost", WPT_FLAG_DJUMP, "Requires teammate boost / double jump");
    FlagBox("Elevator / Lift", WPT_FLAG_LIFT, "Wait for elevator trigger");
    FlagBox("Use Button Trigger", WPT_FLAG_USEBUTTON, "Bot presses button or switch");
    FlagBox("Terrorist Exclusive", WPT_FLAG_TERRORIST, "Only T team may pathfind here");
    FlagBox("CT Exclusive", WPT_FLAG_COUNTER, "Only CT team may pathfind here");
    FlagBox("Avoid Danger Point", WPT_FLAG_AVOID, "Bots avoid this node unless necessary");

    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.4f, 1.0f), "CS-EBOT & Zombie Plague Flags:");
    FlagBox("Zombie Only", WPT_FLAG_ZOMBIEONLY, "Restricted exclusively to zombie players");
    FlagBox("Human Only", WPT_FLAG_HUMANONLY, "Restricted exclusively to human players");
    FlagBox("Zombie Rush / Push", WPT_FLAG_ZOMBIEPUSH, "Aggressive directional zombie rush");
    FlagBox("Zombie / Human Camp", WPT_FLAG_ZMHMCAMP, "Barricade defense perch for humans/zombies");
    FlagBox("Human Camp Mesh", WPT_FLAG_HMCAMPMESH, "Human defense mesh cluster node");
    FlagBox("Fall Check (Ground)", WPT_FLAG_FALLCHECK, "Check for solid ground before proceeding");
    FlagBox("Wait Until Condition", WPT_FLAG_WAITUNTIL, "Wait for platform/door condition");
    FlagBox("Helicopter Evac", WPT_FLAG_HELICOPTER, "Zombie escape helicopter extraction point");
    FlagBox("Fall Risk Hazard", WPT_FLAG_FALLRISK, "High ledge - prevents evasive strafing");
    FlagBox("Specific Gravity", WPT_FLAG_SPECIFICGRAVITY, "Low/custom gravity jump requirement");
    FlagBox("Only One Bot", WPT_FLAG_ONLYONE, "Only 1 bot allowed at once to prevent jamming");

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Outgoing Connections (Max 8):");
    int activeLinks = 0;
    for (int i = 0; i < WPT_MAX_CONNECTIONS; ++i) {
        if (node->connections[i] >= 0) activeLinks++;
    }
    ImGui::TextDisabled("Active Links: %d / %d", activeLinks, WPT_MAX_CONNECTIONS);

    for (int i = 0; i < WPT_MAX_CONNECTIONS; ++i) {
        int16_t targetId = node->connections[i];
        if (targetId < 0) continue;

        ImGui::PushID(i);
        char linkLabel[64];
        std::snprintf(linkLabel, sizeof(linkLabel), "-> #%d", targetId);
        if (ImGui::Button(linkLabel, ImVec2(70, 20))) {
            scene.SelectWaypoint(static_cast<uint32_t>(targetId));
            scene.RebuildWaypointRenderer();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to select target waypoint #%d", targetId);

        ImGui::SameLine();
        bool jumpConn = (node->connectionFlags[i] & WPT_CONN_JUMP) != 0;
        if (ImGui::Checkbox("Jump", &jumpConn)) {
            if (jumpConn) node->connectionFlags[i] |= WPT_CONN_JUMP;
            else node->connectionFlags[i] &= ~WPT_CONN_JUMP;
            scene.RebuildWaypointRenderer();
        }

        ImGui::SameLine();
        bool djumpConn = (node->connectionFlags[i] & WPT_CONN_DOUBLE) != 0;
        if (ImGui::Checkbox("DJump", &djumpConn)) {
            if (djumpConn) node->connectionFlags[i] |= WPT_CONN_DOUBLE;
            else node->connectionFlags[i] &= ~WPT_CONN_DOUBLE;
            scene.RebuildWaypointRenderer();
        }

        ImGui::SameLine();
        if (ImGui::SmallButton("X")) {
            node->connections[i] = -1;
            node->connectionFlags[i] = 0;
            scene.RebuildWaypointRenderer();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove connection");

        ImGui::PopID();
    }

    static int addTargetId = 0;
    static bool addBidi = true;
    ImGui::Spacing();
    ImGui::SetNextItemWidth(80);
    ImGui::InputInt("##TargetWptInput", &addTargetId, 0);
    ImGui::SameLine();
    ImGui::Checkbox("2-Way", &addBidi);
    ImGui::SameLine();
    if (ImGui::Button("Link Node", ImVec2(-1, 22))) {
        if (addTargetId >= 0 && addTargetId != static_cast<int>(node->id)) {
            if (scene.GetWaypoints().GetNode(static_cast<uint32_t>(addTargetId))) {
                scene.GetWaypoints().AddConnection(node->id, static_cast<uint32_t>(addTargetId), WPT_CONN_NONE, addBidi);
                scene.RebuildWaypointRenderer();
            } else {
                scene.ShowToast("Target waypoint ID does not exist!");
            }
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    uint32_t cachedWpt = scene.GetCachedWaypointID();
    if (ImGui::Button("Cache this Waypoint", ImVec2(-1, 22))) {
        scene.CacheWaypoint(node->id);
    }
    if (cachedWpt != 0 && cachedWpt != node->id) {
        char linkCachedLbl[64];
        std::snprintf(linkCachedLbl, sizeof(linkCachedLbl), "Link with Cached (#%u)", cachedWpt);
        if (ImGui::Button(linkCachedLbl, ImVec2(-1, 22))) {
            scene.CreateConnectionToCached(scene.GetWaypointConnectType());
        }
    }

    if (ImGui::Button("Snap to Floor [Space]", ImVec2(-1, 24))) {
        scene.SnapSelectedWaypointToFloor();
    }

    if (ImGui::Button("Deselect [Escape]", ImVec2(-1, 24))) {
        scene.SelectWaypoint(0);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.25f, 0.2f, 1.0f));
    if (ImGui::Button("Delete Waypoint [Delete]", ImVec2(-1, 24))) {
        scene.DeleteSelectedWaypoint();
    }
    ImGui::PopStyleColor();
}

void EditorUI::RenderWaypointGlobalInspector(EditorScene& scene, CommandManager& /*cmdMgr*/) {
    ImGui::TextColored(ImVec4(0.95f, 0.65f, 0.2f, 1.0f), "Bot Waypoints Overview");
    ImGui::Separator();

    const auto& graph = scene.GetWaypoints();
    std::string path = scene.GetWaypointPath();
    if (path.empty()) path = "(New / Unsaved Graph)";
    size_t slash = path.find_last_of("/\\");
    std::string filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;

    ImGui::Text("File: %s", filename.c_str());
    ImGui::Text("Format: %s", graph.GetActiveBot() == BotType::EBot ? "E-Bot (.ewp)" :
                              graph.GetActiveBot() == BotType::SyPB ? "SyPB (.spt)" :
                              graph.GetActiveBot() == BotType::YaPB ? "YaPB (.pwf)" : "POD-Bot (.wpt)");
    ImGui::Text("Game Mod: %s", graph.GetActiveMod() == GameMod::ZombiePlague ? "Zombie Plague" :
                                graph.GetActiveMod() == GameMod::Deathmatch ? "Deathmatch" : "Standard CS");
    ImGui::Spacing();
    ImGui::Separator();

    size_t totalNodes = graph.GetNodeCount();
    size_t totalLinks = 0;
    size_t campCount = 0, sniperCount = 0, ladderCount = 0, crouchCount = 0, jumpCount = 0, zmCampCount = 0;
    for (const auto& n : graph.GetNodes()) {
        for (int c = 0; c < 8; ++c) {
            if (n.connections[c] > 0) totalLinks++;
        }
        if (n.flags & WPT_FLAG_CAMP) campCount++;
        if (n.flags & WPT_FLAG_SNIPER) sniperCount++;
        if (n.flags & WPT_FLAG_LADDER) ladderCount++;
        if (n.flags & WPT_FLAG_CROUCH) crouchCount++;
        if (n.flags & WPT_FLAG_JUMP) jumpCount++;
        if (n.flags & (WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH)) zmCampCount++;
    }

    ImGui::Text("Total Waypoints: %zu", totalNodes);
    ImGui::Text("Total Connections: %zu", totalLinks);

    ImGui::TextDisabled("  - Normal Walk: %zu", totalNodes - campCount - sniperCount - ladderCount);
    if (crouchCount > 0) ImGui::TextDisabled("  - Crouch: %zu", crouchCount);
    if (jumpCount > 0) ImGui::TextDisabled("  - Jump: %zu", jumpCount);
    if (ladderCount > 0) ImGui::TextDisabled("  - Ladders: %zu", ladderCount);
    if (campCount > 0) ImGui::TextDisabled("  - Camp Perches: %zu", campCount);
    if (sniperCount > 0) ImGui::TextDisabled("  - Snipers: %zu", sniperCount);
    if (zmCampCount > 0) ImGui::TextDisabled("  - Zombie Camps: %zu", zmCampCount);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Global Actions:");

    if (ImGui::Button("Auto-Analyze Flags & Crouch...", ImVec2(-1, 24))) {
        m_waypointAnalyzerStats = scene.AutoAnalyzeWaypoints();
        m_analyzerTargetWaypoints = true;
        m_showAnalyzerModal = true;
    }
    if (ImGui::Button("Calculate All Wayzone Radii", ImVec2(-1, 24))) {
        size_t done = scene.GetWaypoints().CalculateAllWayzones(scene.HasBSP() ? &scene.GetBSP() : nullptr);
        scene.ShowToast("Calculated wayzone radii for " + std::to_string(done) + " waypoints!");
    }
    if (ImGui::Button("Auto-Link Unconnected Nodes", ImVec2(-1, 24))) {
        size_t created = scene.GetWaypoints().AutoLinkNodes(150.0f, scene.HasBSP() ? &scene.GetBSP() : nullptr);
        scene.ShowToast("Created " + std::to_string(created) + " connections!");
        scene.RebuildWaypointRenderer();
    }
    if (ImGui::Button("Fix & Prune Blocked Links", ImVec2(-1, 24))) {
        size_t fixed = scene.GetWaypoints().FixWaypoints(scene.HasBSP() ? &scene.GetBSP() : nullptr);
        scene.ShowToast("Pruned / fixed " + std::to_string(fixed) + " invalid links!");
        scene.RebuildWaypointRenderer();
    }
    if (ImGui::Button("Export Bot Waypoints...", ImVec2(-1, 24))) {
        m_showWaypointExportModal = true;
    }
}

void EditorUI::RenderWaypointExportModal(EditorScene& scene) {
    if (m_showWaypointExportModal) {
        ImGui::OpenPopup("Export Bot Waypoints##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 310), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Export Bot Waypoints##Modal", &m_showWaypointExportModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Export Navigation Mesh to Bot Waypoints");
        ImGui::Separator();
        ImGui::Spacing();

        const char* botNames[] = {
            "CS-EBOT (.ewp) [v127 LZSS]",
            "SyPB (.spt / .pwf) [v125]",
            "YaPB (.pwf) [v7 LZSS]",
            "POD-Bot mm (.wpt) [v6 Uncompressed]"
        };
        const char* modNames[] = {
            "Standard CS (Bomb / Hostage / VIP)",
            "Zombie Plague (Camp Meshes, Zombie Jump)",
            "Deathmatch / Roam (Free Flow)"
        };

        ImGui::Text("Target Bot Engine:");
        ImGui::SetNextItemWidth(-1);
        if (ImGui::Combo("##ExportBotCombo", &m_waypointExportBot, botNames, 4)) {
            std::string ext = (m_waypointExportBot == 0) ? ".ewp" : (m_waypointExportBot == 1) ? ".spt" : (m_waypointExportBot == 2) ? ".pwf" : ".wpt";
            if (m_waypointExportPath[0] == '\0' && scene.HasBSP()) {
                std::string base = scene.GetBSPPath();
                size_t dot = base.find_last_of('.');
                if (dot != std::string::npos) base = base.substr(0, dot);
                std::string p = base + ext;
                std::strncpy(m_waypointExportPath, p.c_str(), sizeof(m_waypointExportPath) - 1);
            }
        }

        ImGui::Spacing();
        ImGui::Text("Game Mod Rules:");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##ExportModCombo", &m_waypointExportMod, modNames, 3);

        ImGui::Spacing();
        ImGui::Text("Output File Path:");
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 90.0f);
        ImGui::InputText("##ExportPathInput", m_waypointExportPath, sizeof(m_waypointExportPath));
        ImGui::SameLine();
        if (ImGui::Button("Browse...", ImVec2(80, 24))) {
            const char* filter = (m_waypointExportBot == 0) ? "EBot Waypoints (*.ewp)\0*.ewp\0All Files (*.*)\0*.*\0" :
                                 (m_waypointExportBot == 1) ? "SyPB Waypoints (*.spt;*.pwf)\0*.spt;*.pwf\0All Files (*.*)\0*.*\0" :
                                 (m_waypointExportBot == 2) ? "YaPB Waypoints (*.pwf)\0*.pwf\0All Files (*.*)\0*.*\0" :
                                                              "POD-Bot Waypoints (*.wpt)\0*.wpt\0All Files (*.*)\0*.*\0";
            const char* defExt = (m_waypointExportBot == 0) ? "ewp" : (m_waypointExportBot == 1) ? "spt" : (m_waypointExportBot == 2) ? "pwf" : "wpt";
            std::string selected = FileDialog::SaveFile(filter, defExt, "Export Bot Waypoints");
            if (!selected.empty()) {
                std::strncpy(m_waypointExportPath, selected.c_str(), sizeof(m_waypointExportPath) - 1);
                m_waypointExportPath[sizeof(m_waypointExportPath) - 1] = '\0';
            }
        }

        ImGui::Spacing();
        size_t wptCount = scene.GetWaypoints().GetNodeCount();
        if (wptCount == 0 && scene.HasNAV()) {
            ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "Note: No waypoints in graph. NAV mesh will be automatically converted during export!");
        } else {
            ImGui::Text("Graph currently contains %zu waypoints ready for export.", wptCount);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Export Waypoints", ImVec2(150, 28))) {
            if (m_waypointExportPath[0] == '\0') {
                scene.ShowToast("Please specify an export file path!");
            } else {
                BotType bot = static_cast<BotType>(m_waypointExportBot);
                GameMod mod = static_cast<GameMod>(m_waypointExportMod);
                if (scene.GetWaypoints().GetNodeCount() == 0 && scene.HasNAV()) {
                    scene.ConvertNavToWaypoints(bot, mod);
                }
                if (scene.SaveWaypoints(m_waypointExportPath, bot, mod)) {
                    scene.ShowToast("Exported " + std::to_string(scene.GetWaypoints().GetNodeCount()) + " waypoints successfully!");
                    m_showWaypointExportModal = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    scene.ShowToast("Failed to write waypoint file!");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 28))) {
            m_showWaypointExportModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderNavToWaypointModal(EditorScene& scene) {
    if (m_showNavToWaypointModal) {
        ImGui::OpenPopup("Convert NavMesh to Bot Waypoints##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(500, 260), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Convert NavMesh to Bot Waypoints##Modal", &m_showNavToWaypointModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Convert Valve Navigation Mesh to Bot Waypoint Graph");
        ImGui::Separator();
        ImGui::Spacing();

        const char* botNames[] = {
            "CS-EBOT (.ewp)",
            "SyPB (.spt / .pwf)",
            "YaPB (.pwf)",
            "POD-Bot mm (.wpt)"
        };
        const char* modNames[] = {
            "Standard CS (Bomb / Hostage / VIP)",
            "Zombie Plague (Camp Meshes, Zombie Boost)",
            "Deathmatch / Roam (Free Roam)"
        };

        ImGui::Text("Target Bot Engine:");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##ConvertBotCombo", &m_waypointExportBot, botNames, 4);

        ImGui::Spacing();
        ImGui::Text("Game Mod Rules:");
        ImGui::SetNextItemWidth(-1);
        ImGui::Combo("##ConvertModCombo", &m_waypointExportMod, modNames, 3);

        ImGui::Spacing();
        ImGui::TextWrapped("Converts all NavAreas, connections, and hiding spots into universal bot waypoint nodes with jump arcs, camp angles, and mod-specific flags.");

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Convert Now", ImVec2(130, 28))) {
            BotType bot = static_cast<BotType>(m_waypointExportBot);
            GameMod mod = static_cast<GameMod>(m_waypointExportMod);
            if (scene.ConvertNavToWaypoints(bot, mod)) {
                scene.ShowToast("Created " + std::to_string(scene.GetWaypoints().GetNodeCount()) + " waypoints from NAV!");
                m_showNavToWaypointModal = false;
                ImGui::CloseCurrentPopup();
            } else {
                scene.ShowToast("Conversion failed: No NAV mesh loaded.");
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(90, 28))) {
            m_showNavToWaypointModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderEBotGenModal(EditorScene& scene, CommandManager& /*cmdMgr*/) {
    if (m_showEBotGenModal) {
        ImGui::OpenPopup("Generate CS-EBOT Waypoints##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(520, 520), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Generate CS-EBOT Waypoints##Modal", &m_showEBotGenModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string mapName = scene.GetBSPName();
        if (mapName.empty()) mapName = "Unnamed Map";

        ImGui::Text("Map: %s", mapName.c_str());
        ImGui::TextDisabled("Automated waypoint graph generator ported from CS-EBOT with 8-directional BFS floor flooding, spawn seeding, func_ladder linking, and tactical sightline analysis.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        bool isGen = scene.IsGeneratingWaypoints();
        auto& progress = scene.GetWaypointGenProgress();

        if (isGen) {
            ImGui::Text("Generating bot waypoints in background thread...");
            ImGui::Spacing();
            ImGui::ProgressBar(progress.progress.load(), ImVec2(-1, 24));
            ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "%s", progress.statusMessage.c_str());
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Dismiss to Background", ImVec2(180, 28))) {
                m_showEBotGenModal = false;
                ImGui::CloseCurrentPopup();
            }
        } else {
            if (progress.completed) {
                if (progress.success) {
                    ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "Generation Complete in %.2f seconds!", progress.durationSeconds);
                    ImGui::BulletText("Total Waypoints: %zu", progress.waypointsCreated);
                    ImGui::BulletText("Connections Created: %zu", progress.connectionsCreated);
                    ImGui::BulletText("Ladder Nodes: %zu", progress.laddersCreated);
                    ImGui::BulletText("Camping Spots: %zu", progress.campPointsCreated);
                    ImGui::BulletText("Sniper Perches: %zu", progress.sniperPointsCreated);
                    if (progress.zombieCampsCreated > 0) {
                        ImGui::BulletText("Zombie Camps: %zu", progress.zombieCampsCreated);
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Generation Failed: %s", progress.errorMessage.c_str());
                }
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();
            }

            ImGui::Text("Density & Geometry Options:");
            ImGui::SliderFloat("Node Spacing", &m_ebotGenOptions.nodeSpacing, 75.0f, 200.0f, "%.0f units");
            ImGui::SliderFloat("Minimum Distance", &m_ebotGenOptions.minDistance, 40.0f, 150.0f, "%.0f units");
            ImGui::SliderFloat("Connection Radius", &m_ebotGenOptions.connectRadius, 100.0f, 260.0f, "%.0f units");
            ImGui::SliderFloat("Max Step Height", &m_ebotGenOptions.maxStepHeight, 8.0f, 32.0f, "%.0f units");
            ImGui::SliderFloat("Max Jump Height", &m_ebotGenOptions.maxJumpHeight, 20.0f, 64.0f, "%.0f units");
            ImGui::SliderFloat("Max Drop Height", &m_ebotGenOptions.maxDropHeight, 100.0f, 600.0f, "%.0f units");

            ImGui::Spacing();
            ImGui::Checkbox("Link Ladder Entities (func_ladder)", &m_ebotGenOptions.generateLadders);
            ImGui::Checkbox("Analyze Sightlines & Camps / Sniper Spots", &m_ebotGenOptions.generateCamps);

            ImGui::Spacing();
            const char* botNames[] = {
                "CS-EBOT (.ewp)",
                "SyPB (.spt / .pwf)",
                "YaPB (.pwf)",
                "POD-Bot mm (.wpt)"
            };
            const char* modNames[] = {
                "Standard CS (Bomb / Hostage / VIP)",
                "Zombie Plague (Camp Meshes, Zombie Boost)",
                "Deathmatch / Roam (Free Roam)"
            };

            int botIdx = static_cast<int>(m_ebotGenOptions.botType);
            ImGui::Text("Target Bot Format:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##EBotGenBotCombo", &botIdx, botNames, 4)) {
                m_ebotGenOptions.botType = static_cast<BotType>(botIdx);
            }

            int modIdx = static_cast<int>(m_ebotGenOptions.mod);
            ImGui::Spacing();
            ImGui::Text("Target Game Mod:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##EBotGenModCombo", &modIdx, modNames, 3)) {
                m_ebotGenOptions.mod = static_cast<GameMod>(modIdx);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            if (ImGui::Button("Generate Waypoints", ImVec2(160, 28))) {
                if (scene.HasBSP()) {
                    scene.StartEBotWaypointGeneration(m_ebotGenOptions);
                } else {
                    scene.ShowToast("Cannot generate: No BSP map loaded!");
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(90, 28))) {
                m_showEBotGenModal = false;
                ImGui::CloseCurrentPopup();
            }
        }

        ImGui::EndPopup();
    }
}
