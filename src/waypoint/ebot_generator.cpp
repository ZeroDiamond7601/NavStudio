#include "waypoint/ebot_generator.h"
#include <chrono>
#include <cmath>
#include <queue>
#include <unordered_set>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

EBotGenerateResult EBotGenerator::Generate(
    const BSPFile& bsp,
    WaypointGraph& outGraph,
    const EBotGenerateOptions& options,
    EBotProgressCallback progress
) {
    EBotGenerateResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    if (!bsp.IsLoaded()) {
        result.success = false;
        result.errorMessage = "BSP map is not loaded.";
        return result;
    }

    outGraph.Clear();
    outGraph.SetActiveBot(options.botType);
    outGraph.SetActiveMod(options.mod);
    outGraph.SetMapName(bsp.GetMapName());
    outGraph.SetAuthor("NavStudio EBot Generator");

    if (progress) progress(0.05f, "Seeding entities and player spawns...");

    std::queue<uint32_t> openQueue;
    std::vector<Vector3> seedOrigins;
    std::vector<uint32_t> seedFlags;

    // 1. Gather player spawns and map objectives
    const auto& entities = bsp.GetEntities();
    for (const auto& ent : entities) {
        std::string cname = ent.classname;
        uint32_t flag = WPT_FLAG_NONE;

        if (cname == "info_player_start") {
            flag = WPT_FLAG_COUNTER;
        } else if (cname == "info_player_deathmatch") {
            flag = WPT_FLAG_TERRORIST;
        } else if (cname == "info_vip_start") {
            flag = WPT_FLAG_COUNTER | WPT_FLAG_CROSSING;
        } else if (cname == "info_bomb_target" || cname == "func_bomb_target") {
            flag = WPT_FLAG_GOAL;
        } else if (cname == "hostage_entity") {
            flag = WPT_FLAG_GOAL;
        } else if (cname == "info_hostage_rescue" || cname == "func_hostage_rescue") {
            flag = WPT_FLAG_RESCUE;
        } else if (cname == "func_vip_safetyzone") {
            flag = WPT_FLAG_RESCUE;
        } else if (cname == "func_buyzone" || cname == "armoury_entity") {
            flag = WPT_FLAG_CROSSING;
        }

        if (flag != WPT_FLAG_NONE) {
            Vector3 origin;
            if (ent.GetOrigin(origin)) {
                seedOrigins.push_back(origin);
                seedFlags.push_back(flag);
            }
        }
    }

    // Trace seeds down to floor
    for (size_t i = 0; i < seedOrigins.size(); ++i) {
        Vector3 start = seedOrigins[i] + Vector3(0.0f, 0.0f, 16.0f);
        Vector3 end = seedOrigins[i] - Vector3(0.0f, 0.0f, 180.0f);
        BSPTraceResult tr;
        if (bsp.TraceWorld(start, end, HULL_POINT, &tr) && !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f) {
            Vector3 groundPos = tr.endpos + Vector3(0.0f, 0.0f, 8.0f);

            // Check if another waypoint is already within min distance
            bool tooClose = false;
            for (const auto& existing : outGraph.GetNodes()) {
                if ((existing.origin - groundPos).Length() < options.minDistance) {
                    tooClose = true;
                    break;
                }
            }

            if (!tooClose) {
                WaypointNode* node = outGraph.AddNode(groundPos, seedFlags[i], 24.0f);
                if (node) {
                    openQueue.push(node->id);
                }
            }
        }
    }

    if (outGraph.IsEmpty()) {
        result.success = false;
        result.errorMessage = "No valid player spawn or objective entities found in map.";
        return result;
    }

    // 2. BFS Flood Expansion across walkable geometry
    if (progress) progress(0.20f, "Flooding walkable geometry...");

    const int kNumDirs = 8;
    const float kAngles[kNumDirs] = {
        0.0f,
        static_cast<float>(M_PI * 0.25),
        static_cast<float>(M_PI * 0.50),
        static_cast<float>(M_PI * 0.75),
        static_cast<float>(M_PI),
        static_cast<float>(M_PI * 1.25),
        static_cast<float>(M_PI * 1.50),
        static_cast<float>(M_PI * 1.75)
    };

    size_t processedCount = 0;
    const size_t maxAllowedNodes = 4096;

    while (!openQueue.empty() && outGraph.GetNodeCount() < maxAllowedNodes) {
        uint32_t currId = openQueue.front();
        openQueue.pop();
        ++processedCount;

        WaypointNode* currNode = outGraph.GetNodeByID(currId);
        if (!currNode) continue;
        Vector3 currOrigin = currNode->origin;

        if (progress && (processedCount % 50 == 0)) {
            float p = 0.20f + std::min(0.50f, static_cast<float>(outGraph.GetNodeCount()) / 3000.0f);
            progress(p, "Generated " + std::to_string(outGraph.GetNodeCount()) + " waypoints...");
        }

        for (int d = 0; d < kNumDirs; ++d) {
            Vector3 dir(std::cos(kAngles[d]), std::sin(kAngles[d]), 0.0f);
            Vector3 candPos = currOrigin + dir * options.nodeSpacing;

            // Trace to floor
            Vector3 trStart = candPos + Vector3(0.0f, 0.0f, options.maxStepHeight + 8.0f);
            Vector3 trEnd = candPos - Vector3(0.0f, 0.0f, options.maxDropHeight);
            BSPTraceResult trGround;
            if (!bsp.TraceWorld(trStart, trEnd, HULL_POINT, &trGround) || trGround.startsolid || trGround.allsolid || trGround.fraction >= 1.0f) {
                continue;
            }

            // Check walkable slope
            if (trGround.planeNormal.z < 0.7071f) {
                continue;
            }

            Vector3 groundPos = trGround.endpos + Vector3(0.0f, 0.0f, 8.0f);
            float zDiff = groundPos.z - currOrigin.z;

            // Elevation limits
            if (zDiff > options.maxJumpHeight || zDiff < -options.maxDropHeight) {
                continue;
            }

            // Clearance headroom checks
            BSPTraceResult trHead;
            bsp.TraceWorld(groundPos, groundPos + Vector3(0.0f, 0.0f, 72.0f), HULL_POINT, &trHead);
            float headroom = trHead.fraction * 72.0f;
            if (headroom < 36.0f) {
                continue; // Cannot even crouch
            }
            bool needCrouch = (headroom < 68.0f);

            // Path obstacle ray trace at feet and waist
            BSPTraceResult trFeet, trWaist;
            if (!bsp.TraceWorld(currOrigin + Vector3(0.0f, 0.0f, 16.0f), groundPos + Vector3(0.0f, 0.0f, 16.0f), HULL_POINT, &trFeet) ||
                trFeet.fraction < 0.95f || trFeet.startsolid || trFeet.allsolid) {
                continue; // Foot obstacle / wall
            }

            if (!needCrouch) {
                if (!bsp.TraceWorld(currOrigin + Vector3(0.0f, 0.0f, 36.0f), groundPos + Vector3(0.0f, 0.0f, 36.0f), HULL_POINT, &trWaist) ||
                    trWaist.fraction < 0.95f || trWaist.startsolid || trWaist.allsolid) {
                    continue; // Waist obstacle / wall
                }
            }

            // Check proximity to existing nodes
            uint32_t nearbyId = 0;
            float nearestDist = 999999.0f;
            for (const auto& node : outGraph.GetNodes()) {
                float dist = (node.origin - groundPos).Length();
                if (dist < nearestDist) {
                    nearestDist = dist;
                    nearbyId = node.id;
                }
            }

            if (nearestDist < options.minDistance) {
                // Node already exists within threshold; connect to it if line of sight is clear
                if (nearbyId != 0 && nearbyId != currId) {
                    bool isBi = (std::abs(zDiff) <= options.maxStepHeight);
                    uint16_t cFlags = (zDiff > options.maxStepHeight) ? WPT_CONN_JUMP : WPT_CONN_NONE;
                    outGraph.ConnectNodes(currId, nearbyId, isBi, cFlags);
                }
                continue;
            }

            // Spawn new node
            uint32_t nodeFlags = WPT_FLAG_CROSSING;
            if (needCrouch) nodeFlags |= WPT_FLAG_CROUCH;
            if (zDiff > options.maxStepHeight) nodeFlags |= WPT_FLAG_JUMP;

            WaypointNode* newNode = outGraph.AddNode(groundPos, nodeFlags, 24.0f);
            if (newNode) {
                bool isBi = (zDiff >= -options.maxJumpHeight);
                uint16_t cFlags = (zDiff > options.maxStepHeight) ? WPT_CONN_JUMP : WPT_CONN_NONE;
                outGraph.ConnectNodes(currId, newNode->id, isBi, cFlags);
                openQueue.push(newNode->id);
            }
        }
    }

    // 3. Parse func_ladder entities
    if (options.generateLadders) {
        if (progress) progress(0.75f, "Generating ladder pathways...");
        for (const auto& ent : entities) {
            if (ent.classname == "func_ladder") {
                Vector3 origin;
                if (!ent.GetOrigin(origin)) continue;

                Vector3 bMin = origin - Vector3(16.0f, 16.0f, 64.0f);
                Vector3 bMax = origin + Vector3(16.0f, 16.0f, 64.0f);

                Vector3 bottomPos(origin.x, origin.y, bMin.z + 8.0f);
                Vector3 topPos(origin.x, origin.y, bMax.z - 8.0f);

                WaypointNode* bNode = outGraph.AddNode(bottomPos, WPT_FLAG_LADDER, 16.0f);
                WaypointNode* tNode = outGraph.AddNode(topPos, WPT_FLAG_LADDER, 16.0f);

                if (bNode && tNode) {
                    outGraph.ConnectNodes(bNode->id, tNode->id, true, WPT_CONN_NONE);
                    result.laddersCreated++;

                    // Connect to nearest ground nodes
                    int nearBottom = outGraph.FindNearestNode(bottomPos, 140.0f);
                    if (nearBottom > 0 && static_cast<uint32_t>(nearBottom) != bNode->id && static_cast<uint32_t>(nearBottom) != tNode->id) {
                        outGraph.ConnectNodes(static_cast<uint32_t>(nearBottom), bNode->id, true, WPT_CONN_NONE);
                    }
                    int nearTop = outGraph.FindNearestNode(topPos, 140.0f);
                    if (nearTop > 0 && static_cast<uint32_t>(nearTop) != bNode->id && static_cast<uint32_t>(nearTop) != tNode->id) {
                        outGraph.ConnectNodes(tNode->id, static_cast<uint32_t>(nearTop), true, WPT_CONN_NONE);
                    }
                }
            }
        }
    }

    // 4. Auto-Linking of nearby adjacent nodes
    if (progress) progress(0.85f, "Auto-linking neighboring waypoints...");
    outGraph.AutoLinkNodes(options.connectRadius);

    // 5. Automated Tactical and Camp Sightline Analysis
    if (options.generateCamps) {
        if (progress) progress(0.92f, "Analyzing camp perches and sightlines...");
        outGraph.AnalyzeGraph(&bsp, options.mod);
    }

    // 6. Compute final statistics
    result.waypointsCreated = outGraph.GetNodeCount();
    for (const auto& n : outGraph.GetNodes()) {
        for (int c = 0; c < 8; ++c) {
            if (n.connections[c] > 0) result.connectionsCreated++;
        }
        if (n.flags & WPT_FLAG_CAMP) result.campPointsCreated++;
        if (n.flags & WPT_FLAG_SNIPER) result.sniperPointsCreated++;
        if (n.flags & (WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH)) result.zombieCampsCreated++;
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.durationSeconds = std::chrono::duration<double>(endTime - startTime).count();
    result.success = (result.waypointsCreated > 0);

    if (progress) {
        progress(1.0f, "Completed in " + std::to_string(result.durationSeconds) + "s! Generated " +
                       std::to_string(result.waypointsCreated) + " waypoints.");
    }

    return result;
}
