#include "waypoint/waypoint_nav_converter.h"
#include <unordered_map>
#include <algorithm>
#include <cmath>

WaypointNavConverter::ConvertStats WaypointNavConverter::NavToWaypoints(
    const NavMesh& nav,
    WaypointGraph& outGraph,
    BotType botType,
    GameMod mod
) {
    ConvertStats stats;
    outGraph.Clear();
    outGraph.SetActiveBot(botType);
    outGraph.SetActiveMod(mod);

    if (!nav.IsLoaded()) return stats;

    const auto& areas = nav.GetAreas();
    std::unordered_map<uint32_t, uint32_t> areaToWp;

    // 1. Convert NavAreas to Waypoint Nodes
    for (const NavArea* area : areas) {
        if (!area) continue;

        Vector3 center = area->GetCenter();
        const NavExtent& ext = area->GetExtent();
        float width = std::max(ext.hi.x - ext.lo.x, ext.hi.y - ext.lo.y);
        float radius = std::clamp(width * 0.25f, 16.0f, 64.0f);

        uint32_t flags = WPT_FLAG_CROSSING;
        uint32_t attrs = area->GetAttributes();
        if (attrs & NAV_ATTR_CROUCH) flags |= WPT_FLAG_CROUCH;
        if (attrs & NAV_ATTR_JUMP) flags |= WPT_FLAG_JUMP;
        if (attrs & NAV_ATTR_PRECISE) flags |= WPT_FLAG_FALLCHECK;
        if (attrs & NAV_ATTR_NO_JUMP) flags |= WPT_FLAG_FALLRISK;

        // Mod-specific flag mappings
        if (mod == GameMod::ZombiePlague) {
            // Areas with very small area size (< 80) and 1 connection are dead-end hideouts -> Human Camp
            if (area->GetConnectionCount() <= 1 && width < 96.0f) {
                flags |= WPT_FLAG_HMCAMPMESH;
                ++stats.zombieCampsMapped;
            }
        }

        // Objective flag mapping from Nav Place names
        const std::string& place = area->GetPlaceName();
        if (!place.empty()) {
            std::string lowerPlace = place;
            for (char& c : lowerPlace) c = static_cast<char>(std::tolower(c));
            if (lowerPlace.find("bomb") != std::string::npos || lowerPlace.find("site") != std::string::npos) {
                flags |= WPT_FLAG_GOAL;
            } else if (lowerPlace.find("hostage") != std::string::npos || lowerPlace.find("rescue") != std::string::npos) {
                flags |= WPT_FLAG_RESCUE;
            } else if (lowerPlace.find("ctspawn") != std::string::npos || lowerPlace.find("ct_spawn") != std::string::npos) {
                flags |= WPT_FLAG_TEAM_CT;
            } else if (lowerPlace.find("tspawn") != std::string::npos || lowerPlace.find("terrorist") != std::string::npos) {
                flags |= WPT_FLAG_TEAM_T;
            }
        }

        WaypointNode* node = outGraph.AddNode(center, flags, radius);
        if (node) {
            areaToWp[area->GetID()] = node->id;
            ++stats.waypointsCreated;
        }

        // Map tactical hiding spots inside area
        for (const auto& spot : area->GetHidingSpots()) {
            uint32_t spotFlags = WPT_FLAG_CAMP;
            if (spot.flags & (HIDING_GOOD_SNIPER | HIDING_IDEAL_SNIPER)) {
                spotFlags = WPT_FLAG_SNIPER;
                ++stats.sniperPointsMapped;
            } else {
                ++stats.campPointsMapped;
            }

            if (mod == GameMod::ZombiePlague) {
                spotFlags |= WPT_FLAG_ZMHMCAMP;
                ++stats.zombieCampsMapped;
            }

            WaypointNode* spotNode = outGraph.AddNode(spot.pos, spotFlags, 16.0f);
            if (spotNode && node) {
                outGraph.ConnectNodes(node->id, spotNode->id, true, WPT_CONN_NONE);
                ++stats.waypointsCreated;
                ++stats.connectionsCreated;
            }
        }
    }

    // 2. Convert Area Connections to Waypoint Links
    for (const NavArea* area : areas) {
        if (!area) continue;
        auto itFrom = areaToWp.find(area->GetID());
        if (itFrom == areaToWp.end()) continue;
        uint32_t fromWp = itFrom->second;

        for (int dir = 0; dir < NUM_NAV_DIRECTIONS; ++dir) {
            const auto& connectList = area->GetAdjacentList(static_cast<NavDirType>(dir));
            for (const auto& conn : connectList) {
                const NavArea* adj = conn.area;
                if (!adj) continue;
                auto itTo = areaToWp.find(adj->GetID());
                if (itTo == areaToWp.end()) continue;
                uint32_t toWp = itTo->second;

                uint16_t connFlags = WPT_CONN_NONE;
                // Detect step elevation requiring a jump
                float zDiff = adj->GetCenter().z - area->GetCenter().z;
                if (zDiff > 18.0f && zDiff <= 55.0f) {
                    connFlags |= WPT_CONN_JUMP;
                } else if (adj->HasAttributes(NAV_ATTR_CROUCH) || area->HasAttributes(NAV_ATTR_CROUCH)) {
                    connFlags |= WPT_CONN_CROUCH;
                }

                // Check if connection is bidirectional
                bool isBi = adj->IsConnected(area, static_cast<NavDirType>((dir + 2) % NUM_NAV_DIRECTIONS));
                if (outGraph.ConnectNodes(fromWp, toWp, isBi, connFlags)) {
                    ++stats.connectionsCreated;
                }
            }
        }
    }

    // 3. Convert Ladders (Enforcing 0-radius for precise bot climbing)
    for (const NavLadder* ladder : nav.GetLadders()) {
        if (!ladder) continue;

        WaypointNode* bottomNode = outGraph.AddNode(ladder->bottom, WPT_FLAG_LADDER, 0.0f);
        WaypointNode* topNode = outGraph.AddNode(ladder->top, WPT_FLAG_LADDER, 0.0f);

        if (bottomNode && topNode) {
            outGraph.ConnectNodes(bottomNode->id, topNode->id, true, WPT_CONN_NONE);
            ++stats.waypointsCreated;
            ++stats.waypointsCreated;
            ++stats.connectionsCreated;
            ++stats.laddersConverted;

            // Link bottom to nearest area waypoint
            if (ladder->bottomArea) {
                auto itB = areaToWp.find(ladder->bottomArea->GetID());
                if (itB != areaToWp.end()) {
                    outGraph.ConnectNodes(bottomNode->id, itB->second, true, WPT_CONN_NONE);
                    ++stats.connectionsCreated;
                }
            }

            // Link top to nearest area waypoint
            if (ladder->topForwardArea) {
                auto itT = areaToWp.find(ladder->topForwardArea->GetID());
                if (itT != areaToWp.end()) {
                    outGraph.ConnectNodes(topNode->id, itT->second, true, WPT_CONN_NONE);
                    ++stats.connectionsCreated;
                }
            }
        }
    }

    return stats;
}

size_t WaypointNavConverter::WaypointsToNav(
    const WaypointGraph& graph,
    NavMesh& outNav
) {
    if (graph.IsEmpty()) return 0;

    outNav.Unload();
    const auto& nodes = graph.GetNodes();
    std::unordered_map<uint32_t, uint32_t> wpToArea;

    for (const auto& node : nodes) {
        float halfW = std::clamp(node.radius, 16.0f, 48.0f);
        NavExtent ext;
        ext.lo = Vector3(node.origin.x - halfW, node.origin.y - halfW, node.origin.z - 4.0f);
        ext.hi = Vector3(node.origin.x + halfW, node.origin.y + halfW, node.origin.z + 4.0f);

        NavArea* area = outNav.CreateArea(ext, node.origin.z, node.origin.z);
        if (area) {
            wpToArea[node.id] = area->GetID();

            if (node.flags & WPT_FLAG_CROUCH) {
                area->SetAttributes(area->GetAttributes() | NAV_ATTR_CROUCH);
            }
            if (node.flags & WPT_FLAG_JUMP) {
                area->SetAttributes(area->GetAttributes() | NAV_ATTR_JUMP);
            }
            if (node.flags & WPT_FLAG_FALLCHECK) {
                area->SetAttributes(area->GetAttributes() | NAV_ATTR_PRECISE);
            }
            if (node.flags & WPT_FLAG_FALLRISK) {
                area->SetAttributes(area->GetAttributes() | NAV_ATTR_NO_JUMP);
            }

            // Map place names from goal/spawn/rescue flags
            if (node.flags & WPT_FLAG_GOAL) {
                area->SetPlaceName("Bombsite");
            } else if (node.flags & WPT_FLAG_RESCUE) {
                area->SetPlaceName("HostageRescue");
            } else if (node.flags & WPT_FLAG_TEAM_CT) {
                area->SetPlaceName("CTSpawn");
            } else if (node.flags & WPT_FLAG_TEAM_T) {
                area->SetPlaceName("TSpawn");
            }

            // Map sniper & camp flags back to hiding spots
            if (node.flags & WPT_FLAG_SNIPER) {
                NavHidingSpot spot;
                spot.id = area->GetID() * 100 + 1;
                spot.pos = node.origin;
                spot.flags = HIDING_GOOD_SNIPER | HIDING_IDEAL_SNIPER;
                area->GetHidingSpots().push_back(spot);
            } else if (node.flags & (WPT_FLAG_CAMP | WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH)) {
                NavHidingSpot spot;
                spot.id = area->GetID() * 100 + 1;
                spot.pos = node.origin;
                spot.flags = HIDING_IN_COVER;
                area->GetHidingSpots().push_back(spot);
            }
        }
    }

    // Connect areas according to waypoint connections
    for (const auto& node : nodes) {
        auto itFrom = wpToArea.find(node.id);
        if (itFrom == wpToArea.end()) continue;
        uint32_t fromAreaId = itFrom->second;

        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            int16_t targetWpId = node.connections[c];
            if (targetWpId <= 0) continue;

            auto itTo = wpToArea.find(static_cast<uint32_t>(targetWpId));
            if (itTo == wpToArea.end()) continue;
            uint32_t toAreaId = itTo->second;

            outNav.ConnectAreas(fromAreaId, toAreaId, false);
        }
    }

    // Convert ladder waypoint pairs into NavLadder structures
    uint32_t ladderId = 1;
    for (const auto& node : nodes) {
        if (!(node.flags & WPT_FLAG_LADDER)) continue;

        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            int16_t targetId = node.connections[c];
            if (targetId <= 0) continue;
            const WaypointNode* targetNode = graph.GetNodeByID(static_cast<uint32_t>(targetId));
            if (!targetNode || !(targetNode->flags & WPT_FLAG_LADDER)) continue;

            // Only process bottom-to-top pair to avoid duplicates
            if (node.origin.z < targetNode->origin.z) {
                NavLadder* ladder = new NavLadder();
                ladder->id = ladderId++;
                ladder->bottom = node.origin;
                ladder->top = targetNode->origin;
                ladder->length = targetNode->origin.z - node.origin.z;
                ladder->width = std::max(32.0f, std::max(node.radius, targetNode->radius) * 2.0f);

                Vector3 delta = targetNode->origin - node.origin;
                if (std::abs(delta.x) > std::abs(delta.y)) {
                    ladder->dir = (delta.x > 0) ? NAV_DIR_EAST : NAV_DIR_WEST;
                } else {
                    ladder->dir = (delta.y > 0) ? NAV_DIR_SOUTH : NAV_DIR_NORTH;
                }

                auto itB = wpToArea.find(node.id);
                if (itB != wpToArea.end()) ladder->bottomArea = outNav.GetAreaByID(itB->second);

                auto itT = wpToArea.find(targetNode->id);
                if (itT != wpToArea.end()) ladder->topForwardArea = outNav.GetAreaByID(itT->second);

                outNav.GetLadders().push_back(ladder);
            }
        }
    }

    outNav.RebuildGrid();
    return outNav.GetAreas().size();
}
