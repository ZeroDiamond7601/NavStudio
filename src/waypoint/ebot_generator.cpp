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

    // 1. Gather player spawns, map objectives, doors, buttons, and ladders
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
        } else if (cname == "info_hostage_rescue" || cname == "func_hostage_rescue" || cname == "func_vip_safetyzone") {
            flag = WPT_FLAG_RESCUE;
        } else if (cname == "func_buyzone" || cname == "armoury_entity") {
            flag = WPT_FLAG_CROSSING;
        } else if (cname == "func_door" || cname == "func_door_rotating" || cname == "func_button") {
            flag = WPT_FLAG_CROSSING;
        }

        if (flag != WPT_FLAG_NONE) {
            Vector3 origin;
            if (ent.GetOrigin(origin)) {
                seedOrigins.push_back(origin);
                seedFlags.push_back(flag);
            } else {
                // Brush entities (*1, *2, etc.) bounds midpoint
                std::string modelStr = ent.GetString("model");
                if (!modelStr.empty() && modelStr[0] == '*') {
                    int modelIdx = std::atoi(modelStr.c_str() + 1);
                    const dmodel_t* mod = bsp.GetModel(modelIdx);
                    if (mod) {
                        Vector3 mid((mod->mins.x + mod->maxs.x) * 0.5f,
                                    (mod->mins.y + mod->maxs.y) * 0.5f,
                                    (mod->mins.z + mod->maxs.z) * 0.5f);
                        seedOrigins.push_back(mid);
                        seedFlags.push_back(flag);
                    }
                }
            }
        }
    }

    // Also sample flat walkable BSP brush faces to guarantee seeds across all rooms
    for (int i = 0; i < bsp.GetFaceCount(); ++i) {
        const dface_t* face = bsp.GetFace(i);
        if (!face || face->planenum < 0 || face->planenum >= bsp.GetPlaneCount()) continue;
        const dplane_t* plane = bsp.GetPlane(face->planenum);
        if (plane && plane->normal.z >= 0.7071f) {
            Vector3 poly[32];
            int vcount = bsp.GetFacePolygon(i, poly, 32);
            if (vcount >= 3) {
                Vector3 centroid(0.0f, 0.0f, 0.0f);
                for (int v = 0; v < vcount; ++v) {
                    centroid = centroid + poly[v];
                }
                centroid = centroid * (1.0f / static_cast<float>(vcount));
                seedOrigins.push_back(centroid + Vector3(0.0f, 0.0f, 16.0f));
                seedFlags.push_back(WPT_FLAG_CROSSING);
            }
        }
    }

    // Fast 2D spatial hash grid for O(1) proximity queries
    struct SpatialKey {
        int gx, gy;
        bool operator==(const SpatialKey& o) const { return gx == o.gx && gy == o.gy; }
    };
    struct SpatialHash {
        size_t operator()(const SpatialKey& k) const {
            return std::hash<int>()(k.gx) ^ (std::hash<int>()(k.gy) << 16);
        }
    };

    std::unordered_map<SpatialKey, std::vector<uint32_t>, SpatialHash> spatialGrid;
    float cellSize = options.nodeSpacing > 0.0f ? options.nodeSpacing : 60.0f;

    auto AddToSpatialGrid = [&](uint32_t nodeId, const Vector3& pos) {
        int gx = static_cast<int>(std::floor(pos.x / cellSize));
        int gy = static_cast<int>(std::floor(pos.y / cellSize));
        spatialGrid[{gx, gy}].push_back(nodeId);
    };

    auto FindNearestInSpatialGrid = [&](const Vector3& pos, float maxRadius, uint32_t& outId) -> float {
        int minGx = static_cast<int>(std::floor((pos.x - maxRadius) / cellSize));
        int maxGx = static_cast<int>(std::floor((pos.x + maxRadius) / cellSize));
        int minGy = static_cast<int>(std::floor((pos.y - maxRadius) / cellSize));
        int maxGy = static_cast<int>(std::floor((pos.y + maxRadius) / cellSize));
        float bestDist = 999999.0f;
        outId = 0;
        for (int gx = minGx; gx <= maxGx; ++gx) {
            for (int gy = minGy; gy <= maxGy; ++gy) {
                auto it = spatialGrid.find({gx, gy});
                if (it != spatialGrid.end()) {
                    for (uint32_t id : it->second) {
                        const WaypointNode* n = outGraph.GetNodeByID(id);
                        if (n) {
                            float d = (n->origin - pos).Length();
                            if (d < bestDist) {
                                bestDist = d;
                                outId = id;
                            }
                        }
                    }
                }
            }
        }
        return bestDist;
    };

    // Trace seeds down to floor and initialize seed waypoints
    for (size_t i = 0; i < seedOrigins.size(); ++i) {
        Vector3 start = seedOrigins[i] + Vector3(0.0f, 0.0f, 36.0f);
        Vector3 end = seedOrigins[i] - Vector3(0.0f, 0.0f, 350.0f);
        BSPTraceResult tr;
        if (bsp.TraceWorld(start, end, HULL_POINT, &tr) && !tr.startsolid && !tr.allsolid && tr.fraction < 1.0f) {
            Vector3 groundPos = tr.endpos + Vector3(0.0f, 0.0f, 18.0f);

            uint32_t existingId = 0;
            float dist = FindNearestInSpatialGrid(groundPos, options.minDistance, existingId);
            if (dist >= options.minDistance) {
                WaypointNode* node = outGraph.AddNode(groundPos, seedFlags[i], 24.0f);
                if (node) {
                    openQueue.push(node->id);
                    AddToSpatialGrid(node->id, groundPos);
                }
            }
        }
    }

    if (outGraph.IsEmpty()) {
        result.success = false;
        result.errorMessage = "No valid player spawn or objective entities found in map.";
        return result;
    }

    // 2. BFS Whole-Map Flood Expansion across all reachable geometry
    if (progress) progress(0.15f, "Exploring and meshing walkable map geometry...");

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
    const size_t maxAllowedNodes = 8192;

    while (!openQueue.empty() && outGraph.GetNodeCount() < maxAllowedNodes) {
        uint32_t currId = openQueue.front();
        openQueue.pop();
        ++processedCount;

        WaypointNode* currNode = outGraph.GetNodeByID(currId);
        if (!currNode) continue;
        Vector3 currOrigin = currNode->origin;
        float currFloorZ = currOrigin.z - 18.0f;

        if (progress && (processedCount % 100 == 0)) {
            float p = 0.15f + std::min(0.60f, static_cast<float>(outGraph.GetNodeCount()) / 4000.0f);
            progress(p, "Generated " + std::to_string(outGraph.GetNodeCount()) + " waypoints...");
        }

        for (int d = 0; d < kNumDirs; ++d) {
            Vector3 dir(std::cos(kAngles[d]), std::sin(kAngles[d]), 0.0f);
            Vector3 candPos = currOrigin + dir * options.nodeSpacing;

            // Trace downward from jump elevation relative to floor
            Vector3 trStart = Vector3(candPos.x, candPos.y, currFloorZ + options.maxJumpHeight + 4.0f);
            Vector3 trEnd   = Vector3(candPos.x, candPos.y, currFloorZ - options.maxDropHeight);
            BSPTraceResult trGround;
            if (!bsp.TraceWorld(trStart, trEnd, HULL_POINT, &trGround) || trGround.fraction >= 1.0f || trGround.startsolid || trGround.allsolid) {
                continue;
            }

            // Reject overly steep non-walkable surfaces
            if (trGround.planeNormal.z < 0.7071f) {
                continue;
            }

            Vector3 floorPos = trGround.endpos;
            float zDiff = floorPos.z - currFloorZ;

            // Elevation limits
            if (zDiff > options.maxJumpHeight || zDiff < -options.maxDropHeight) {
                continue;
            }

            // Headroom clearance check with HULL_POINT
            BSPTraceResult trHead;
            bsp.TraceWorld(floorPos + Vector3(0.0f, 0.0f, 2.0f), floorPos + Vector3(0.0f, 0.0f, 74.0f), HULL_POINT, &trHead);
            float headroom = (trHead.fraction < 1.0f && !trHead.startsolid && !trHead.allsolid)
                ? (trHead.endpos.z - floorPos.z)
                : 74.0f;

            if (headroom < 36.0f) {
                continue; // Cannot fit even crouched
            }
            bool needCrouch = (headroom < 68.0f);

            // Path obstacle ray trace at feet and waist
            float stepZ = std::max(currFloorZ, floorPos.z);
            BSPTraceResult trFeet, trWaist;
            bsp.TraceWorld(Vector3(currOrigin.x, currOrigin.y, stepZ + 18.0f),
                           Vector3(floorPos.x, floorPos.y, stepZ + 18.0f),
                           HULL_POINT, &trFeet);
            if (trFeet.fraction < 0.95f || trFeet.startsolid || trFeet.allsolid) {
                continue; // Foot obstacle / step wall
            }

            if (!needCrouch) {
                bsp.TraceWorld(Vector3(currOrigin.x, currOrigin.y, stepZ + 45.0f),
                               Vector3(floorPos.x, floorPos.y, stepZ + 45.0f),
                               HULL_POINT, &trWaist);
                if (trWaist.fraction < 0.95f || trWaist.startsolid || trWaist.allsolid) {
                    continue; // Waist / doorway obstacle
                }
            }

            Vector3 nodePos = floorPos + Vector3(0.0f, 0.0f, 18.0f);

            // Fast spatial proximity query
            uint32_t nearbyId = 0;
            float nearestDist = FindNearestInSpatialGrid(nodePos, options.minDistance, nearbyId);

            if (nearestDist < options.minDistance) {
                // Node already exists within threshold; connect if reachable
                if (nearbyId != 0 && nearbyId != currId) {
                    bool isBi = (std::abs(zDiff) <= options.maxStepHeight);
                    uint16_t cFlags = (zDiff > options.maxStepHeight) ? WPT_CONN_JUMP : WPT_CONN_NONE;
                    if (needCrouch) cFlags |= WPT_CONN_CROUCH;
                    outGraph.ConnectNodes(currId, nearbyId, isBi, cFlags);
                }
                continue;
            }

            // Spawn new node
            uint32_t nodeFlags = WPT_FLAG_CROSSING;
            if (needCrouch) nodeFlags |= WPT_FLAG_CROUCH;
            if (zDiff > options.maxStepHeight) nodeFlags |= WPT_FLAG_JUMP;

            WaypointNode* newNode = outGraph.AddNode(nodePos, nodeFlags, 24.0f);
            if (newNode) {
                bool isBi = (zDiff >= -options.maxJumpHeight);
                uint16_t cFlags = (zDiff > options.maxStepHeight) ? WPT_CONN_JUMP : WPT_CONN_NONE;
                if (needCrouch) cFlags |= WPT_CONN_CROUCH;
                outGraph.ConnectNodes(currId, newNode->id, isBi, cFlags);
                AddToSpatialGrid(newNode->id, nodePos);
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
                bool hasOrigin = ent.GetOrigin(origin);
                Vector3 bMin, bMax;

                if (!hasOrigin) {
                    std::string modelStr = ent.GetString("model");
                    if (!modelStr.empty() && modelStr[0] == '*') {
                        int modelIdx = std::atoi(modelStr.c_str() + 1);
                        const dmodel_t* mod = bsp.GetModel(modelIdx);
                        if (mod) {
                            origin = Vector3((mod->mins.x + mod->maxs.x) * 0.5f,
                                             (mod->mins.y + mod->maxs.y) * 0.5f,
                                             (mod->mins.z + mod->maxs.z) * 0.5f);
                            bMin = Vector3(mod->mins.x, mod->mins.y, mod->mins.z);
                            bMax = Vector3(mod->maxs.x, mod->maxs.y, mod->maxs.z);
                            hasOrigin = true;
                        }
                    }
                } else {
                    bMin = origin - Vector3(16.0f, 16.0f, 64.0f);
                    bMax = origin + Vector3(16.0f, 16.0f, 64.0f);
                }

                if (!hasOrigin) continue;

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
    if (progress) progress(0.82f, "Auto-linking neighboring waypoints...");
    outGraph.AutoLinkNodes(options.connectRadius);

    // 5. Line-of-sight validation and link pruning
    if (progress) progress(0.88f, "Pruning blocked pathways and validating geometry...");
    outGraph.FixWaypoints(&bsp);
    outGraph.DeleteOrphanNodes();

    // 6. Calculate optimal wayzones for all nodes
    if (progress) progress(0.92f, "Calculating wayzone radii...");
    for (const auto& node : outGraph.GetNodes()) {
        outGraph.CalculateWayzone(node.id, &bsp);
    }

    // 7. Automated Tactical and Camp Sightline Analysis
    if (options.generateCamps) {
        if (progress) progress(0.96f, "Analyzing camp perches and sightlines...");
        outGraph.AnalyzeGraph(&bsp, options.mod);
    }

    // 8. Compute final statistics
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
        progress(1.0f, "Completed in " + std::to_string(result.durationSeconds).substr(0, 4) + "s! Generated " +
                       std::to_string(result.waypointsCreated) + " waypoints.");
    }

    return result;
}
