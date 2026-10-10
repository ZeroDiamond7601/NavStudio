#include "editor/ui/editor_ui.h"
#include "editor/scene/editor_scene.h"
#include "editor/commands/command.h"
#include "editor/commands/waypoint_commands.h"
#include "editor/ui/file_dialog.h"
#include "waypoint/waypoint_types.h"
#include <imgui.h>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <string>

void EditorUI::RenderWaypointInspector(EditorScene& scene, CommandManager& cmdMgr) {
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

    // Coordinates with Precision Nudge Step
    ImGui::Text("Nudge Step:");
    const float wptStepPresets[] = { 1.0f, 4.0f, 8.0f, 16.0f, 32.0f };
    for (int s = 0; s < 5; ++s) {
        ImGui::SameLine();
        char sLbl[16];
        std::snprintf(sLbl, sizeof(sLbl), "%.0f##wpt_stp%d", wptStepPresets[s], s);
        if (m_nudgeStepLinear == wptStepPresets[s]) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.9f, 1.0f));
        if (ImGui::Button(sLbl, ImVec2(24, 18))) m_nudgeStepLinear = wptStepPresets[s];
        if (m_nudgeStepLinear == wptStepPresets[s]) ImGui::PopStyleColor();
    }

    float pos[3] = { node->origin.x, node->origin.y, node->origin.z };
    if (DrawNudgeFloat3("Origin", pos, m_nudgeStepLinear)) {
        Vector3 newPos(pos[0], pos[1], pos[2]);
        cmdMgr.ExecuteCommand(std::make_unique<CmdMoveWaypoint>(&scene, node->id, node->origin, newPos));
    }

    // Radius
    float oldRadius = node->radius;
    if (DrawNudgeFloat("Radius", &node->radius, 8.0f, 0.0f, 255.0f, "%.0f u")) {
        float newRadius = node->radius;
        node->radius = oldRadius;
        cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
            node->flags, node->flags,
            oldRadius, newRadius,
            node->mesh, node->mesh,
            node->campPitch, node->campPitch,
            node->campYaw, node->campYaw));
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
            float newRadius = rPresets[i];
            cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
                node->flags, node->flags,
                node->radius, newRadius,
                node->mesh, node->mesh,
                node->campPitch, node->campPitch,
                node->campYaw, node->campYaw));
        }
        if (std::abs(node->radius - rPresets[i]) < 0.1f) ImGui::PopStyleColor();
    }
    for (int i = 5; i < 9; ++i) {
        if (i > 5) ImGui::SameLine(0.0f, 4.0f);
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "%.0f##insp_rad%d", rPresets[i], i);
        if (std::abs(node->radius - rPresets[i]) < 0.1f) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.50f, 0.15f, 1.0f));
        if (ImGui::Button(lbl, ImVec2(rBtnW, 20))) {
            float newRadius = rPresets[i];
            cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
                node->flags, node->flags,
                node->radius, newRadius,
                node->mesh, node->mesh,
                node->campPitch, node->campPitch,
                node->campYaw, node->campYaw));
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
    float oldPitch = node->campPitch;
    if (DrawNudgeAngle("Pitch", &node->campPitch, m_nudgeStepAngular, -89.0f, 89.0f)) {
        float newPitch = node->campPitch;
        node->campPitch = oldPitch;
        cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
            node->flags, node->flags,
            node->radius, node->radius,
            node->mesh, node->mesh,
            oldPitch, newPitch,
            node->campYaw, node->campYaw));
    }
    float oldYaw = node->campYaw;
    if (DrawNudgeAngle("Yaw", &node->campYaw, m_nudgeStepAngular, 0.0f, 360.0f, true)) {
        float newYaw = node->campYaw;
        node->campYaw = oldYaw;
        cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
            node->flags, node->flags,
            node->radius, node->radius,
            node->mesh, node->mesh,
            node->campPitch, node->campPitch,
            oldYaw, newYaw));
    }
    if (ImGui::Button("0 deg (E)", ImVec2(50, 20))) {
        cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
            node->flags, node->flags, node->radius, node->radius, node->mesh, node->mesh,
            node->campPitch, node->campPitch, node->campYaw, 0.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("90 deg (N)", ImVec2(50, 20))) {
        cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
            node->flags, node->flags, node->radius, node->radius, node->mesh, node->mesh,
            node->campPitch, node->campPitch, node->campYaw, 90.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("180 deg (W)", ImVec2(50, 20))) {
        cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
            node->flags, node->flags, node->radius, node->radius, node->mesh, node->mesh,
            node->campPitch, node->campPitch, node->campYaw, 180.0f));
    }
    ImGui::SameLine();
    if (ImGui::Button("270 deg (S)", ImVec2(50, 20))) {
        cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
            node->flags, node->flags, node->radius, node->radius, node->mesh, node->mesh,
            node->campPitch, node->campPitch, node->campYaw, 270.0f));
    }

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
            uint32_t oldFlags = node->flags;
            uint32_t newFlags = checked ? (oldFlags | flag) : (oldFlags & ~flag);
            cmdMgr.ExecuteCommand(std::make_unique<CmdSetWaypointProps>(&scene, node->id,
                oldFlags, newFlags,
                node->radius, node->radius,
                node->mesh, node->mesh,
                node->campPitch, node->campPitch,
                node->campYaw, node->campYaw));
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
        bool crouchConn = (node->connectionFlags[i] & WPT_CONN_CROUCH) != 0;
        if (ImGui::Checkbox("Crouch", &crouchConn)) {
            if (crouchConn) node->connectionFlags[i] |= WPT_CONN_CROUCH;
            else node->connectionFlags[i] &= ~WPT_CONN_CROUCH;
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

    // Ladder and Ghost Bot shortcuts
    if (ImGui::Button("Toggle Ladder Rung (Zero-Radius)", ImVec2(-1, 22))) {
        if (node->flags & WPT_FLAG_LADDER) {
            node->flags &= ~WPT_FLAG_LADDER;
            node->radius = 32.0f;
        } else {
            node->flags |= WPT_FLAG_LADDER;
            node->radius = 0.0f; // Critical zero-radius enforcement for ladders
        }
        scene.RebuildWaypointRenderer();
    }

    if (ImGui::Button("Set as Ghost Bot Start Point", ImVec2(-1, 22))) {
        m_ghostBotInputStart = static_cast<int>(node->id);
        m_showGhostBotModal = true;
    }
    if (ImGui::Button("Set as Ghost Bot Goal Point", ImVec2(-1, 22))) {
        m_ghostBotInputGoal = static_cast<int>(node->id);
        m_showGhostBotModal = true;
    }

    if (ImGui::Button("Start Pen Path from this Node", ImVec2(-1, 22))) {
        scene.SetPenToolActive(true);
    }

    if (ImGui::Button("Duplicate Waypoint [Shift+D]", ImVec2(-1, 24))) {
        scene.DuplicateSelectedWaypoints();
    }

    if (ImGui::Button("Connect Mode [C]", ImVec2(-1, 24))) {
        scene.ToggleWaypointConnectMode();
    }

    if (ImGui::Button("Snap to Floor [Space]", ImVec2(-1, 24))) {
        scene.SnapSelectedWaypointToFloor();
    }

    if (ImGui::Button("Deselect [Escape]", ImVec2(-1, 24))) {
        scene.ClearWaypointSelection();
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
    ImGui::Text("Interactive Drawing & Simulation:");

    bool isPen = scene.IsPenToolActive();
    if (isPen) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.9f, 0.5f, 0.1f, 1.0f));
    if (ImGui::Button(isPen ? "Pen Tool Active (Click floor to place & chain)" : "Pen / Breadcrumb Tool (Chain Drawing)", ImVec2(-1, 26))) {
        scene.TogglePenTool();
    }
    if (isPen) ImGui::PopStyleColor();

    if (isPen) {
        if (ImGui::Button("Finish / End Pen Stroke", ImVec2(-1, 22))) {
            scene.EndPenStroke();
        }
    }

    if (ImGui::Button("Ghost Bot Simulation & Path Auditor...", ImVec2(-1, 26))) {
        m_showGhostBotModal = true;
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("BSP Map Extractions:");

    if (ImGui::Button("Snap Objectives from BSP Entities", ImVec2(-1, 24))) {
        scene.SnapObjectivesFromEntities();
    }
    if (ImGui::Button("Extract Ladders from BSP (func_ladder)", ImVec2(-1, 24))) {
        scene.AssignLaddersFromBSP();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Global Graph Actions:");

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
    if (ImGui::Button("Generate Parkour & Jump Paths...", ImVec2(-1, 24))) {
        m_showParkourModal = true;
    }
    if (ImGui::Button("Optimize Waypoint Graph...", ImVec2(-1, 24))) {
        m_showWaypointOptimizeModal = true;
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
                    if (progress.parkourLinksCreated > 0) {
                        ImGui::BulletText("Parkour Jump Links: %zu", progress.parkourLinksCreated);
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
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Larger spacing (120-160u) with wide wayzone radii creates fewer nodes, saving pathfinding computation and reducing bot confusion.");
            }
            ImGui::SliderFloat("Minimum Distance", &m_ebotGenOptions.minDistance, 40.0f, 150.0f, "%.0f units");
            ImGui::SliderFloat("Connection Radius", &m_ebotGenOptions.connectRadius, 100.0f, 260.0f, "%.0f units");
            ImGui::SliderFloat("Max Step Height", &m_ebotGenOptions.maxStepHeight, 8.0f, 32.0f, "%.0f units");
            ImGui::SliderFloat("Max Jump Height", &m_ebotGenOptions.maxJumpHeight, 20.0f, 64.0f, "%.0f units");
            ImGui::SliderFloat("Max Drop Height", &m_ebotGenOptions.maxDropHeight, 100.0f, 600.0f, "%.0f units");

            ImGui::Spacing();
            ImGui::Checkbox("Link Ladder Entities (func_ladder)", &m_ebotGenOptions.generateLadders);
            ImGui::Checkbox("Analyze Sightlines & Camps / Sniper Spots", &m_ebotGenOptions.generateCamps);
            ImGui::Checkbox("Generate Parkour & Ledge Jumps (Crates, Chasms, Drops)", &m_ebotGenOptions.generateParkour);

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

void EditorUI::RenderWaypointOptimizeModal(EditorScene& scene, CommandManager& /*cmdMgr*/) {
    if (m_showWaypointOptimizeModal) {
        ImGui::OpenPopup("Optimize Bot Waypoint Graph##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(540, 520), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Optimize Bot Waypoint Graph##Modal", &m_showWaypointOptimizeModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "Waypoint Topology & Geometry Optimizer");
        ImGui::TextDisabled("Prunes redundant corridor waypoints, merges overlapping nodes, restores one-way ground links, and validates line-of-sight.");
        ImGui::Separator();
        ImGui::Spacing();

        if (m_waypointOptStats.durationSeconds > 0.0) {
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "Last Optimization Completed in %.3f seconds!", m_waypointOptStats.durationSeconds);
            ImGui::BulletText("Overlapping Nodes Merged: %zu", m_waypointOptStats.overlappingMerged);
            ImGui::BulletText("Collinear Corridor Nodes Pruned: %zu", m_waypointOptStats.collinearPruned);
            ImGui::BulletText("Blocked Collision Links Pruned: %zu", m_waypointOptStats.blockedLinksPruned);
            ImGui::BulletText("One-Way Flat Links Restored to 2-Way: %zu", m_waypointOptStats.oneWayLinksFixed);
            ImGui::BulletText("Dead-End Orphan Nodes Removed: %zu", m_waypointOptStats.orphansRemoved);
            ImGui::BulletText("Wayzone Radii Recalculated: %zu", m_waypointOptStats.wayzonesCalculated);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
        }

        ImGui::Text("Optimization Rules & Passes:");
        ImGui::Checkbox("Merge Overlapping Nodes", &m_waypointOptOptions.mergeOverlapping);
        if (m_waypointOptOptions.mergeOverlapping) {
            ImGui::SliderFloat("Merge Distance Threshold", &m_waypointOptOptions.mergeDistance, 10.0f, 50.0f, "%.0f units");
        }

        ImGui::Spacing();
        ImGui::Checkbox("Prune Co-linear Redundant Nodes (Corridors)", &m_waypointOptOptions.pruneCollinear);
        if (m_waypointOptOptions.pruneCollinear) {
            ImGui::SliderFloat("Angle Tolerance Deviation", &m_waypointOptOptions.collinearMaxAngle, 5.0f, 30.0f, "%.0f deg");
        }

        ImGui::Spacing();
        ImGui::Checkbox("Prune Blocked Links (BSP Collision Traces)", &m_waypointOptOptions.pruneBlockedLinks);
        ImGui::Checkbox("Restore Flat Ground One-Way Links to Two-Way", &m_waypointOptOptions.fixOneWayLinks);
        ImGui::Checkbox("Eliminate Disconnected Orphan Islands", &m_waypointOptOptions.pruneOrphans);
        ImGui::Checkbox("Recalculate Optimal Wayzone Radii (Raycasts)", &m_waypointOptOptions.recalculateWayzones);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        auto& task = scene.GetWaypointTaskProgress();
        if (task.isRunning.load()) {
            ImGui::Spacing();
            ImGui::ProgressBar(task.progress.load(), ImVec2(-1, 24));
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", task.statusMessage.c_str());
            ImGui::Spacing();
        }

        bool running = task.isRunning.load();
        if (running) {
            ImGui::BeginDisabled();
            ImGui::Button("Optimizing...", ImVec2(160, 28));
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button("Run Optimizer Now", ImVec2(160, 28))) {
                if (scene.HasWaypoints()) {
                    scene.StartAsyncOptimizeWaypoints(m_waypointOptOptions);
                } else {
                    scene.ShowToast("No waypoints loaded in graph!");
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90, 28))) {
            m_showWaypointOptimizeModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderNavOrWaypointPromptModal(EditorScene& scene) {
    if (scene.HasPendingNavOrWptChoice()) {
        ImGui::OpenPopup("Select Navigation Mode##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(560, 360), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Select Navigation Mode##Modal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        std::string mapName = scene.GetPromptBspName();
        if (mapName.empty()) mapName = "Current Map";

        ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.2f, 1.0f), "Multiple Navigation Data Files Found for %s", mapName.c_str());
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextWrapped("The BSP map has both a Valve Navigation Mesh (.nav) and Bot Waypoints available on disk. Which navigation layer would you like to edit?");
        ImGui::Spacing();

        // Card 1: Valve NavMesh
        ImGui::BeginChild("##NavCard", ImVec2(250, 110), true);
        ImGui::TextColored(ImVec4(0.2f, 0.7f, 1.0f, 1.0f), "[ Valve Navigation Mesh ]");
        ImGui::BulletText("Areas: %zu", scene.GetPromptNavAreaCount());
        ImGui::BulletText("Format: Valve .NAV");
        std::string navFile = scene.GetPromptNavPath();
        size_t nSlash = navFile.find_last_of("/\\");
        if (nSlash != std::string::npos) navFile = navFile.substr(nSlash + 1);
        ImGui::TextDisabled("File: %s", navFile.c_str());
        ImGui::EndChild();

        ImGui::SameLine();

        // Card 2: Bot Waypoints
        ImGui::BeginChild("##WptCard", ImVec2(250, 110), true);
        ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.4f, 1.0f), "[ Bot Waypoint Graph ]");
        ImGui::BulletText("Waypoints: %zu", scene.GetPromptWptNodeCount());
        ImGui::BulletText("Format: CS-EBOT / SyPB / YaPB");
        std::string wptFile = scene.GetPromptWptPath();
        size_t wSlash = wptFile.find_last_of("/\\");
        if (wSlash != std::string::npos) wptFile = wptFile.substr(wSlash + 1);
        ImGui::TextDisabled("File: %s", wptFile.c_str());
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Action Buttons
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.45f, 0.85f, 1.0f));
        if (ImGui::Button("Load NavMesh Only (.nav)", ImVec2(165, 30))) {
            scene.ChooseLoadNavOnly();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.50f, 0.15f, 1.0f));
        if (ImGui::Button("Load Waypoints Only", ImVec2(165, 30))) {
            scene.ChooseLoadWptOnly();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.70f, 0.35f, 1.0f));
        if (ImGui::Button("Load Both (Dual Layer)", ImVec2(175, 30))) {
            scene.ChooseLoadBoth();
            ImGui::CloseCurrentPopup();
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();
        if (ImGui::Button("Cancel / Dismiss", ImVec2(-1, 22))) {
            scene.DismissNavOrWptChoice();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderParkourModal(EditorScene& scene, CommandManager& /*cmdMgr*/) {
    if (m_showParkourModal) {
        ImGui::OpenPopup("Generate Parkour & Jump Paths##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Generate Parkour & Jump Paths##Modal", &m_showParkourModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Parkour & Jump Trajectory Generator");
        ImGui::TextDisabled("Scans existing waypoints and 3D map geometry to detect jumpable crates, elevated ledges, chasm leaps, and safe drop-down shortcuts.");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Parkour Detection Options:");
        ImGui::SliderFloat("Max Jump Distance", &m_parkourOptions.maxJumpDist, 100.0f, 300.0f, "%.0f units");
        ImGui::SliderFloat("Max Jump Height", &m_parkourOptions.maxJumpHeight, 20.0f, 65.0f, "%.0f units");
        ImGui::SliderFloat("Safe Drop Height", &m_parkourOptions.maxDropHeight, 100.0f, 500.0f, "%.0f units");

        ImGui::Spacing();
        ImGui::Checkbox("Crate & Ledge Climbs (18u < dz <= 55u)", &m_parkourOptions.detectCrateClimbs);
        ImGui::Checkbox("Chasm & Gap Leaps (Voids between platforms)", &m_parkourOptions.detectChasmLeaps);
        ImGui::Checkbox("Drop-Down Shortcuts (Safe one-way drops)", &m_parkourOptions.detectDropShortcuts);
        ImGui::Checkbox("Double-Jump Assists (Ledges 60u - 130u)", &m_parkourOptions.detectDoubleJumps);

        if (m_parkourStats.totalParkourLinks > 0) {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.2f, 1.0f, 0.3f, 1.0f), "Results (in %.3f seconds):", m_parkourStats.durationSeconds);
            ImGui::BulletText("Total Parkour Links Created: %zu", m_parkourStats.totalParkourLinks);
            ImGui::BulletText("Crate / Ledge Climbs: %zu", m_parkourStats.jumpUpsCreated);
            ImGui::BulletText("Chasm / Gap Leaps: %zu", m_parkourStats.gapJumpsCreated);
            ImGui::BulletText("Drop-Down Shortcuts: %zu", m_parkourStats.dropJumpsCreated);
            if (m_parkourStats.doubleJumpsCreated > 0) {
                ImGui::BulletText("Double-Jump Boosts: %zu", m_parkourStats.doubleJumpsCreated);
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        auto& task = scene.GetWaypointTaskProgress();
        if (task.isRunning.load()) {
            ImGui::Spacing();
            ImGui::ProgressBar(task.progress.load(), ImVec2(-1, 24));
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "%s", task.statusMessage.c_str());
            ImGui::Spacing();
        }

        bool running = task.isRunning.load();
        if (running) {
            ImGui::BeginDisabled();
            ImGui::Button("Generating...", ImVec2(180, 28));
            ImGui::EndDisabled();
        } else {
            if (ImGui::Button("Generate Parkour Paths", ImVec2(180, 28))) {
                scene.StartAsyncGenerateParkour(m_parkourOptions);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(90, 28))) {
            m_showParkourModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderWaypointMultiInspector(EditorScene& scene, CommandManager& /*cmdMgr*/) {
    const auto& selIds = scene.GetSelectedWaypointIDs();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Multi-Waypoint Selection: %zu Nodes", selIds.size());
    ImGui::Separator();
    ImGui::Spacing();

    // Batch Radius
    ImGui::Text("Batch Wayzone Radius:");
    static float batchRadius = 48.0f;
    if (ImGui::SliderFloat("##BatchRadiusSlider", &batchRadius, 0.0f, 255.0f, "%.0f units")) {
        scene.BatchSetWaypointRadius(batchRadius);
    }
    const float rPresets[] = { 0.0f, 16.0f, 32.0f, 48.0f, 64.0f, 96.0f };
    for (int i = 0; i < 6; ++i) {
        if (i > 0) ImGui::SameLine();
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "%.0f##b_rad%d", rPresets[i], i);
        if (ImGui::Button(lbl, ImVec2(40, 20))) {
            batchRadius = rPresets[i];
            scene.BatchSetWaypointRadius(batchRadius);
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Batch Flag Assignment:");

    auto FlagBtn = [&](const char* label, uint32_t flag) {
        if (ImGui::Button(label, ImVec2(-1, 22))) {
            scene.BatchSetWaypointFlags(flag, true);
        }
    };

    if (ImGui::TreeNode("Tactical & Role Flags")) {
        FlagBtn("Set WPT_FLAG_CAMP", WPT_FLAG_CAMP);
        FlagBtn("Set WPT_FLAG_SNIPER", WPT_FLAG_SNIPER);
        FlagBtn("Set WPT_FLAG_CROUCH", WPT_FLAG_CROUCH);
        FlagBtn("Set WPT_FLAG_JUMP", WPT_FLAG_JUMP);
        FlagBtn("Set WPT_FLAG_LADDER (0 radius)", WPT_FLAG_LADDER);
        FlagBtn("Set WPT_FLAG_GOAL (Bomb/Rescue)", WPT_FLAG_GOAL);
        FlagBtn("Set WPT_FLAG_RESCUE", WPT_FLAG_RESCUE);
        FlagBtn("Set WPT_FLAG_TEAM_CT", WPT_FLAG_COUNTER);
        FlagBtn("Set WPT_FLAG_TEAM_T", WPT_FLAG_TERRORIST);
        ImGui::TreePop();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Batch Precision Nudge:");
    float batchStep = m_nudgeStepLinear;
    float nBtnW = 42.0f;
    ImGui::Text("Nudge Position (+/- %.0fu):", batchStep);
    auto BatchNudge = [&](float dx, float dy, float dz) {
        scene.NudgeSelection(dx, dy, dz);
    };
    if (ImGui::Button("-X##bn", ImVec2(nBtnW, 20))) BatchNudge(-batchStep, 0.0f, 0.0f);
    ImGui::SameLine();
    if (ImGui::Button("+X##bn", ImVec2(nBtnW, 20))) BatchNudge(batchStep, 0.0f, 0.0f);
    ImGui::SameLine(0, 10.0f);
    if (ImGui::Button("-Y##bn", ImVec2(nBtnW, 20))) BatchNudge(0.0f, -batchStep, 0.0f);
    ImGui::SameLine();
    if (ImGui::Button("+Y##bn", ImVec2(nBtnW, 20))) BatchNudge(0.0f, batchStep, 0.0f);
    ImGui::SameLine(0, 10.0f);
    if (ImGui::Button("-Z##bn", ImVec2(nBtnW, 20))) BatchNudge(0.0f, 0.0f, -batchStep);
    ImGui::SameLine();
    if (ImGui::Button("+Z##bn", ImVec2(nBtnW, 20))) BatchNudge(0.0f, 0.0f, batchStep);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Align Coordinates (Hammer Style):");
    float aBtnW = (ImGui::GetContentRegionAvail().x - 8.0f) / 3.0f;
    if (ImGui::Button("Min X##aw", ImVec2(aBtnW, 20))) scene.AlignSelectedWaypoints(EditorScene::WaypointAlignMode::MinX);
    ImGui::SameLine();
    if (ImGui::Button("Center X##aw", ImVec2(aBtnW, 20))) scene.AlignSelectedWaypoints(EditorScene::WaypointAlignMode::CenterX);
    ImGui::SameLine();
    if (ImGui::Button("Max X##aw", ImVec2(aBtnW, 20))) scene.AlignSelectedWaypoints(EditorScene::WaypointAlignMode::MaxX);

    if (ImGui::Button("Min Y##aw", ImVec2(aBtnW, 20))) scene.AlignSelectedWaypoints(EditorScene::WaypointAlignMode::MinY);
    ImGui::SameLine();
    if (ImGui::Button("Center Y##aw", ImVec2(aBtnW, 20))) scene.AlignSelectedWaypoints(EditorScene::WaypointAlignMode::CenterY);
    ImGui::SameLine();
    if (ImGui::Button("Max Y##aw", ImVec2(aBtnW, 20))) scene.AlignSelectedWaypoints(EditorScene::WaypointAlignMode::MaxY);

    float zBtnW = (ImGui::GetContentRegionAvail().x - 4.0f) * 0.5f;
    if (ImGui::Button("Floor Z (Lowest)##aw", ImVec2(zBtnW, 20))) scene.AlignSelectedWaypoints(EditorScene::WaypointAlignMode::FloorZ);
    ImGui::SameLine();
    if (ImGui::Button("Average Z (Flatten)##aw", ImVec2(zBtnW, 20))) scene.AlignSelectedWaypoints(EditorScene::WaypointAlignMode::AverageZ);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Batch Operations:");

    if (ImGui::Button("Duplicate Selected Waypoints [Shift+D]", ImVec2(-1, 24))) {
        scene.DuplicateSelectedWaypoints();
    }
    if (selIds.size() == 2) {
        if (ImGui::Button("Bridge Intermediate Waypoints [B]", ImVec2(-1, 24))) {
            scene.BridgeSelectedWaypoints();
        }
        if (ImGui::Button("Create Ladder Pair (Enforce 0-radius)", ImVec2(-1, 24))) {
            scene.CreateLadderPairFromSelected();
        }
    }
    if (ImGui::Button("Connect Selected in Consecutive Chain", ImVec2(-1, 24))) {
        scene.BatchConnectSelectedWaypoints(true);
    }
    if (ImGui::Button("Snap Selected to BSP Floor [Space]", ImVec2(-1, 24))) {
        scene.BatchSnapWaypointsToFloor();
    }
    if (ImGui::Button("Invert Selection", ImVec2(-1, 22))) {
        scene.InvertWaypointSelection();
    }
    if (ImGui::Button("Clear Selection [Esc]", ImVec2(-1, 22))) {
        scene.ClearWaypointSelection();
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
    char delLabel[64];
    std::snprintf(delLabel, sizeof(delLabel), "Delete %zu Selected Waypoints [Delete]", selIds.size());
    if (ImGui::Button(delLabel, ImVec2(-1, 26))) {
        scene.BatchDeleteWaypoints();
    }
    ImGui::PopStyleColor();
}

void EditorUI::RenderWaypointConnectionInspector(EditorScene& scene, CommandManager& /*cmdMgr*/) {
    const auto& conn = scene.GetSelectedWaypointConnection();
    if (!conn.valid()) return;

    auto* fromNode = scene.GetWaypoints().GetNode(conn.fromId);
    auto* toNode = scene.GetWaypoints().GetNode(conn.toId);
    if (!fromNode || !toNode) {
        scene.ClearSelectedWaypointConnection();
        return;
    }

    bool isTwoWay = toNode->HasConnectionTo(static_cast<int16_t>(conn.fromId));
    float dist = (toNode->origin - fromNode->origin).Length();

    ImGui::TextColored(ImVec4(0.2f, 0.9f, 1.0f, 1.0f), "[WAYPOINT LINK INSPECTOR]");
    ImGui::Separator();
    ImGui::Spacing();

    ImGui::Text("From Node: #%u (%.1f, %.1f, %.1f)", fromNode->id, fromNode->origin.x, fromNode->origin.y, fromNode->origin.z);
    ImGui::SameLine();
    char fLbl[32];
    std::snprintf(fLbl, sizeof(fLbl), "Select##from_%u", fromNode->id);
    if (ImGui::SmallButton(fLbl)) {
        scene.SelectWaypoint(fromNode->id, false);
    }

    ImGui::Text("To Node:   #%u (%.1f, %.1f, %.1f)", toNode->id, toNode->origin.x, toNode->origin.y, toNode->origin.z);
    ImGui::SameLine();
    char tLbl[32];
    std::snprintf(tLbl, sizeof(tLbl), "Select##to_%u", toNode->id);
    if (ImGui::SmallButton(tLbl)) {
        scene.SelectWaypoint(toNode->id, false);
    }

    ImGui::Text("Link Distance: %.1f units", dist);
    if (isTwoWay) {
        ImGui::TextColored(ImVec4(1.0f, 0.9f, 0.2f, 1.0f), "Topology: Bidirectional <-> (2-Way)");
    } else {
        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Topology: Unidirectional -> (1-Way Outgoing)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.3f, 0.85f, 1.0f, 1.0f), "Traversal Flags:");

    uint16_t curFlags = WPT_CONN_NONE;
    for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
        if (fromNode->connections[c] == static_cast<int16_t>(toNode->id)) {
            curFlags = fromNode->connectionFlags[c];
            break;
        }
    }

    bool fJump = (curFlags & WPT_CONN_JUMP) != 0;
    bool fDouble = (curFlags & WPT_CONN_DOUBLE) != 0;
    bool fCrouch = (curFlags & WPT_CONN_CROUCH) != 0;
    bool fVisible = (curFlags & WPT_CONN_VISIBLE) != 0;

    bool flagsChanged = false;
    if (ImGui::Checkbox("Requires Jump (WPT_CONN_JUMP)", &fJump)) flagsChanged = true;
    if (ImGui::Checkbox("Requires Double Jump (WPT_CONN_DOUBLE)", &fDouble)) flagsChanged = true;
    if (ImGui::Checkbox("Requires Crouch Ducking (WPT_CONN_CROUCH)", &fCrouch)) flagsChanged = true;
    if (ImGui::Checkbox("Requires Clear Line-of-Sight (WPT_CONN_VISIBLE)", &fVisible)) flagsChanged = true;

    if (flagsChanged) {
        uint16_t newFlags = WPT_CONN_NONE;
        if (fJump) newFlags |= WPT_CONN_JUMP;
        if (fDouble) newFlags |= WPT_CONN_DOUBLE;
        if (fCrouch) newFlags |= WPT_CONN_CROUCH;
        if (fVisible) newFlags |= WPT_CONN_VISIBLE;
        scene.SetSelectedWaypointConnectionFlags(newFlags);
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Link Operations:");

    if (ImGui::Button(isTwoWay ? "Convert to 1-Way (Unidirectional)" : "Convert to 2-Way (Bidirectional)", ImVec2(-1, 24))) {
        scene.ToggleSelectedWaypointConnectionBidirectional();
    }

    if (ImGui::Button("Reverse Traversal Direction (R)", ImVec2(-1, 24))) {
        scene.ReverseSelectedWaypointConnection();
    }

    if (ImGui::Button("Deselect Link (Esc)", ImVec2(-1, 22))) {
        scene.ClearSelectedWaypointConnection();
    }

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.15f, 0.15f, 1.0f));
    if (ImGui::Button("Delete Connection Link (Del / Backspace)", ImVec2(-1, 26))) {
        scene.DeleteSelectedWaypointConnection();
    }
    ImGui::PopStyleColor();
}

void EditorUI::RenderGhostBotModal(EditorScene& scene) {
    if (m_showGhostBotModal) {
        ImGui::OpenPopup("Ghost Bot Simulation & Path Auditor##Modal");
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(620, 560), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Ghost Bot Simulation & Path Auditor##Modal", &m_showGhostBotModal, ImGuiWindowFlags_None)) {
        ImGui::TextColored(ImVec4(0.1f, 0.9f, 1.0f, 1.0f), "Interactive Ghost Bot Simulation & A* Path Auditing");
        ImGui::TextDisabled("Simulates autonomous player locomotion along the bot waypoint graph with real-time clearance, jump, and ladder auditing.");
        ImGui::Separator();
        ImGui::Spacing();

        // Start & Goal inputs
        ImGui::Text("Path Endpoints:");
        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("Start Waypoint ID", &m_ghostBotInputStart);
        ImGui::SameLine();
        if (ImGui::Button("Use Selected##Start")) {
            if (scene.GetSelectedWaypointID() != 0) m_ghostBotInputStart = (int)scene.GetSelectedWaypointID();
        }

        ImGui::SetNextItemWidth(120);
        ImGui::InputInt("Goal Waypoint ID", &m_ghostBotInputGoal);
        ImGui::SameLine();
        if (ImGui::Button("Use Selected##Goal")) {
            if (scene.GetSelectedWaypointID() != 0) m_ghostBotInputGoal = (int)scene.GetSelectedWaypointID();
        }

        ImGui::Spacing();

        if (ImGui::Button("Run A* Search & Start Simulation", ImVec2(240, 28))) {
            scene.StartGhostBotSimulation(static_cast<uint32_t>(m_ghostBotInputStart), static_cast<uint32_t>(m_ghostBotInputGoal));
        }

        if (scene.IsGhostBotActive()) {
            ImGui::SameLine();
            if (ImGui::Button("Stop Simulation", ImVec2(120, 28))) {
                scene.StopGhostBotSimulation();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // Playback controls
            if (scene.IsGhostBotPaused()) {
                if (ImGui::Button("Resume Playback", ImVec2(130, 24))) {
                    scene.TogglePauseGhostBot();
                }
            } else {
                if (ImGui::Button("Pause Playback", ImVec2(130, 24))) {
                    scene.TogglePauseGhostBot();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reset to Start", ImVec2(110, 24))) {
                scene.ResetGhostBotSimulation();
            }
            ImGui::SameLine();
            if (ImGui::Button("Step Forward", ImVec2(110, 24))) {
                scene.StepGhostBotSimulation();
            }

            bool loop = scene.GetGhostBotLoop();
            if (ImGui::Checkbox("Loop Traversal", &loop)) {
                scene.SetGhostBotLoop(loop);
            }

            float spd = scene.GetGhostBotSpeedMultiplier();
            ImGui::SameLine();
            ImGui::SetNextItemWidth(140);
            if (ImGui::SliderFloat("Speed Multiplier", &spd, 0.25f, 5.0f, "%.2fx")) {
                scene.SetGhostBotSpeedMultiplier(spd);
            }

            // Path stats
            const auto& audit = scene.GetGhostBotAudit();
            ImGui::Spacing();
            ImGui::Text("Traversal Metrics:");
            ImGui::BulletText("Path Waypoints: %zu nodes", scene.GetGhostBotPath().size());
            ImGui::BulletText("Total Distance: %.1f units", audit.totalDistance);
            ImGui::BulletText("Estimated Run Time: %.2f seconds", audit.estimatedDurationSec);
            ImGui::BulletText("Current Step: %zu / %zu", scene.GetGhostBotCurrentStep() + 1, scene.GetGhostBotPath().size());

            if (!audit.warnings.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.35f, 0.2f, 1.0f), "Audit Warnings (%zu found):", audit.warnings.size());
                ImGui::BeginChild("##AuditWarns", ImVec2(-1, 80), true);
                for (const auto& w : audit.warnings) {
                    ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "  - %s", w.c_str());
                }
                ImGui::EndChild();
            }

            // Step Details Table
            ImGui::Spacing();
            ImGui::Text("Step-by-Step Traversal Audit:");
            if (ImGui::BeginTable("##StepAuditTable", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY, ImVec2(-1, 140))) {
                ImGui::TableSetupColumn("Step", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableSetupColumn("From -> To", ImGuiTableColumnFlags_WidthFixed, 90);
                ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 70);
                ImGui::TableSetupColumn("Distance", ImGuiTableColumnFlags_WidthFixed, 65);
                ImGui::TableSetupColumn("Delta Z", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Status / Notes", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (size_t s = 0; s < audit.steps.size(); ++s) {
                    const auto& step = audit.steps[s];
                    ImGui::TableNextRow();
                    if (s == scene.GetGhostBotCurrentStep()) {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, ImColor(30, 80, 140, 180));
                    }

                    ImGui::TableSetColumnIndex(0);
                    ImGui::Text("%zu", s + 1);

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("#%u -> #%u", step.fromId, step.toId);

                    ImGui::TableSetColumnIndex(2);
                    if (step.isLadder) ImGui::TextColored(ImVec4(0.8f, 0.5f, 0.1f, 1.0f), "Ladder");
                    else if (step.isJump) ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "Jump");
                    else if (step.isCrouch) ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Crouch");
                    else ImGui::Text("Walk");

                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%.0fu", step.distance);

                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%+.0fu", step.deltaZ);

                    ImGui::TableSetColumnIndex(5);
                    if (!step.warning.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.2f, 1.0f), "%s", step.warning.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Clear");
                    }
                }
                ImGui::EndTable();
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Close", ImVec2(90, 26))) {
            m_showGhostBotModal = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

void EditorUI::RenderWaypointTaskModal(EditorScene& scene) {
    auto& task = scene.GetWaypointTaskProgress();
    if (!task.isRunning.load()) return;

    ImGui::OpenPopup("Waypoint Processing##Modal");
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(480, 190), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Waypoint Processing##Modal", nullptr, ImGuiWindowFlags_NoResize)) {
        ImGui::TextColored(ImVec4(0.2f, 0.85f, 1.0f, 1.0f), "%s", task.taskName.c_str());
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        float progress = task.progress.load();
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;

        char progressText[32];
        std::snprintf(progressText, sizeof(progressText), "%.0f%%", progress * 100.0f);
        ImGui::ProgressBar(progress, ImVec2(-1, 26), progressText);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.45f, 0.85f, 1.0f, 1.0f), "%s", task.statusMessage.c_str());

        int dotCount = static_cast<int>(ImGui::GetTime() * 4.0) % 4;
        std::string dots = std::string(dotCount, '.');
        ImGui::TextDisabled("Processing waypoint graph and geometry validation%s", dots.c_str());

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Run in Background", ImVec2(160, 26))) {
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
}

