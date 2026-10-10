#include "waypoint/waypoint_graph.h"
#include "waypoint/compressor.h"
#include "bsp/bsp_file.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

WaypointGraph::WaypointGraph() = default;

void WaypointGraph::Clear() {
    m_nodes.clear();
    m_nextId = 1;
    m_mapName = "unknown";
    m_author = "NavStudio";
    m_loadedPath.clear();
}

const WaypointNode* WaypointGraph::GetNodeByID(uint32_t id) const {
    for (const auto& node : m_nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

WaypointNode* WaypointGraph::GetNodeByID(uint32_t id) {
    for (auto& node : m_nodes) {
        if (node.id == id) return &node;
    }
    return nullptr;
}

WaypointNode* WaypointGraph::AddNode(const Vector3& origin, uint32_t flags, float radius) {
    WaypointNode node;
    node.id = m_nextId++;
    node.origin = origin;
    node.flags = flags;
    node.radius = radius;
    m_nodes.push_back(node);
    return &m_nodes.back();
}

bool WaypointGraph::RemoveNode(uint32_t id) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [id](const WaypointNode& n) {
        return n.id == id;
    });
    if (it == m_nodes.end()) return false;

    int16_t removedIdx = static_cast<int16_t>(id);
    m_nodes.erase(it);

    // Remove any connections referencing this node across the graph
    for (auto& n : m_nodes) {
        n.RemoveConnection(removedIdx);
    }
    return true;
}

int WaypointGraph::FindNearestNode(const Vector3& pos, float maxDist) const {
    int bestIdx = -1;
    float bestDistSq = maxDist * maxDist;

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        Vector3 diff = m_nodes[i].origin - pos;
        float dsq = diff.Dot(diff);
        if (dsq < bestDistSq) {
            bestDistSq = dsq;
            bestIdx = static_cast<int>(i);
        }
    }
    return bestIdx;
}

bool WaypointGraph::ConnectNodes(uint32_t fromId, uint32_t toId, bool bidirectional, uint16_t connFlags) {
    WaypointNode* from = GetNodeByID(fromId);
    WaypointNode* to = GetNodeByID(toId);
    if (!from || !to || fromId == toId) return false;

    bool ok1 = from->AddConnection(static_cast<int16_t>(toId), connFlags);
    bool ok2 = true;
    if (bidirectional) {
        ok2 = to->AddConnection(static_cast<int16_t>(fromId), connFlags);
    }
    return ok1 || ok2;
}

bool WaypointGraph::DisconnectNodes(uint32_t fromId, uint32_t toId, bool bidirectional) {
    WaypointNode* from = GetNodeByID(fromId);
    WaypointNode* to = GetNodeByID(toId);
    if (!from || !to) return false;

    bool ok1 = from->RemoveConnection(static_cast<int16_t>(toId));
    bool ok2 = false;
    if (bidirectional) {
        ok2 = to->RemoveConnection(static_cast<int16_t>(fromId));
    }
    return ok1 || ok2;
}

size_t WaypointGraph::AutoLinkNodes(float maxDist, const BSPFile* bsp) {
    size_t created = 0;
    float maxDistSq = maxDist * maxDist;

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        WaypointNode& from = m_nodes[i];
        if (from.GetFreeSlotCount() == 0) continue;

        struct CandLink {
            uint32_t id;
            float distSq;
            float dz;
        };
        std::vector<CandLink> candidates;

        for (size_t j = 0; j < m_nodes.size(); ++j) {
            if (i == j) continue;
            const WaypointNode& to = m_nodes[j];
            if (from.HasConnectionTo(static_cast<int16_t>(to.id))) continue;

            Vector3 diff = to.origin - from.origin;
            float dsq = diff.Dot(diff);
            if (dsq > maxDistSq) continue;
            if (std::abs(diff.z) > 45.0f) continue;

            // Check line of sight with BSP
            if (bsp && bsp->IsLoaded()) {
                Vector3 p1 = from.origin + Vector3(0.0f, 0.0f, 18.0f);
                Vector3 p2 = to.origin + Vector3(0.0f, 0.0f, 18.0f);
                BSPTraceResult tr;
                if (bsp->TraceWorld(p1, p2, HULL_POINT, &tr)) {
                    if (tr.fraction < 0.98f || tr.startsolid || tr.allsolid) {
                        continue;
                    }
                }
            }

            candidates.push_back({ to.id, dsq, diff.z });
        }

        std::sort(candidates.begin(), candidates.end(), [](const CandLink& a, const CandLink& b) {
            return a.distSq < b.distSq;
        });

        for (const auto& cand : candidates) {
            if (from.GetFreeSlotCount() == 0) break;
            uint16_t cFlags = (cand.dz > 18.0f) ? WPT_CONN_JUMP : WPT_CONN_NONE;
            if (ConnectNodes(from.id, cand.id, true, cFlags)) {
                ++created;
            }
        }
    }
    return created;
}

WaypointGraph::WaypointAnalysisStats WaypointGraph::AnalyzeGraph(const BSPFile* bsp, GameMod mod) {
    WaypointAnalysisStats stats;
    stats.totalScanned = m_nodes.size();

    bool hasBsp = (bsp && bsp->IsLoaded());

    for (auto& node : m_nodes) {
        bool nodeModified = false;

        // 1. BSP Geometry Analysis: Headroom / Crouch (CS-EBOT & NavMesh style)
        if (hasBsp) {
            Vector3 start(node.origin.x, node.origin.y, node.origin.z + 4.0f);
            Vector3 end(node.origin.x, node.origin.y, node.origin.z + 74.0f);
            BSPTraceResult tr;
            if (bsp->TraceWorld(start, end, HULL_POINT, &tr)) {
                if (!tr.startsolid && !tr.allsolid && tr.fraction < 1.0f) {
                    float clearance = tr.endpos.z - node.origin.z;
                    if (clearance < 72.0f && clearance >= 24.0f) {
                        if (!(node.flags & WPT_FLAG_CROUCH)) {
                            node.flags |= WPT_FLAG_CROUCH;
                            stats.crouchAssigned++;
                            nodeModified = true;
                        }
                    }
                }
            }

            // Fall hazard / high ledge check (> 150u drop without floor)
            Vector3 fallStart(node.origin.x, node.origin.y, node.origin.z + 8.0f);
            Vector3 fallEnd(node.origin.x, node.origin.y, node.origin.z - 200.0f);
            BSPTraceResult trFall;
            if (bsp->TraceWorld(fallStart, fallEnd, HULL_POINT, &trFall)) {
                if (trFall.fraction > 0.75f || (trFall.fraction == 1.0f && !trFall.startsolid)) {
                    if (!(node.flags & WPT_FLAG_FALLRISK)) {
                        node.flags |= WPT_FLAG_FALLRISK;
                        stats.fallRiskAssigned++;
                        nodeModified = true;
                    }
                }
            }

            // Line-of-sight & Camp / Sniper aim optimizer (YaPB style radial sightline analysis)
            bool isCamp = (node.flags & (WPT_FLAG_CAMP | WPT_FLAG_SNIPER | WPT_FLAG_HMCAMPMESH | WPT_FLAG_ZMHMCAMP)) != 0;
            if (isCamp && (node.campYaw == 0.0f && node.campPitch == 0.0f)) {
                float bestDist = 0.0f;
                float bestYaw = 0.0f;
                float eyeZ = node.origin.z + ((node.flags & WPT_FLAG_CROUCH) ? 18.0f : 24.0f);
                Vector3 eyePos(node.origin.x, node.origin.y, eyeZ);

                // Sample 16 horizontal directions (every 22.5 degrees)
                for (int a = 0; a < 16; ++a) {
                    float angDeg = a * 22.5f;
                    float rad = angDeg * (3.14159265f / 180.0f);
                    Vector3 rayDir(std::cos(rad), std::sin(rad), 0.0f);
                    Vector3 rayEnd = eyePos + rayDir * 2048.0f;

                    BSPTraceResult trSight;
                    if (bsp->TraceWorld(eyePos, rayEnd, HULL_POINT, &trSight)) {
                        float dist = (trSight.endpos - eyePos).Length();
                        if (dist > bestDist) {
                            bestDist = dist;
                            bestYaw = angDeg;
                        }
                    }
                }

                if (bestDist > 200.0f) {
                    node.campYaw = bestYaw;
                    node.campPitch = 0.0f;
                    stats.campAnglesCalculated++;
                    nodeModified = true;
                }
            }
        }

        // 2. Link & Connection Analysis: Step jumps & wall blockage (YaPB clean_paths style)
        for (int i = 0; i < WPT_MAX_CONNECTIONS; ++i) {
            int16_t targetId = node.connections[i];
            if (targetId < 0) continue;

            WaypointNode* target = GetNodeByID(static_cast<uint32_t>(targetId));
            if (!target) {
                node.connections[i] = -1;
                node.connectionFlags[i] = 0;
                stats.blockedLinksPruned++;
                nodeModified = true;
                continue;
            }

            // Step elevation requiring jump
            float dz = target->origin.z - node.origin.z;
            if (dz > 18.0f && dz <= 45.0f) {
                if (!(node.flags & WPT_FLAG_JUMP)) {
                    node.flags |= WPT_FLAG_JUMP;
                    stats.jumpAssigned++;
                    nodeModified = true;
                }
                node.connectionFlags[i] |= WPT_CONN_JUMP;
            }

            // Path blockage check (YaPB clean_paths_on_finish)
            if (hasBsp) {
                Vector3 p1 = node.origin + Vector3(0.0f, 0.0f, 18.0f);
                Vector3 p2 = target->origin + Vector3(0.0f, 0.0f, 18.0f);
                BSPTraceResult trPath;
                if (bsp->TraceWorld(p1, p2, HULL_POINT, &trPath)) {
                    if (!trPath.startsolid && !trPath.allsolid && trPath.fraction < 0.92f) {
                        node.connections[i] = -1;
                        node.connectionFlags[i] = 0;
                        stats.blockedLinksPruned++;
                        nodeModified = true;
                    }
                }
            }
        }

        // 3. Mod-Specific Analysis: Zombie Plague dead-end human camp mesh (CS-EBOT / SyPB style)
        if (mod == GameMod::ZombiePlague) {
            int activeConns = 0;
            for (int i = 0; i < WPT_MAX_CONNECTIONS; ++i) {
                if (node.connections[i] >= 0) activeConns++;
            }
            if (activeConns == 1 && !(node.flags & (WPT_FLAG_HMCAMPMESH | WPT_FLAG_ZMHMCAMP))) {
                node.flags |= WPT_FLAG_HMCAMPMESH;
                stats.zombieCampsAssigned++;
                nodeModified = true;
            }
        }

        if (nodeModified) {
            stats.totalModified++;
        }
    }

    return stats;
}


// --- E-Bot In-Game Waypoint Utilities & Optimization ---

size_t WaypointGraph::DeleteOrphanNodes() {
    if (m_nodes.empty()) return 0;

    // Collect all node IDs that have at least one outgoing or incoming link
    std::unordered_set<uint32_t> connectedNodeIds;
    for (const auto& n : m_nodes) {
        bool hasOutgoing = false;
        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            if (n.connections[c] > 0) {
                hasOutgoing = true;
                connectedNodeIds.insert(static_cast<uint32_t>(n.connections[c]));
            }
        }
        if (hasOutgoing) {
            connectedNodeIds.insert(n.id);
        }
    }

    size_t removedCount = 0;
    auto it = m_nodes.begin();
    while (it != m_nodes.end()) {
        if (connectedNodeIds.find(it->id) == connectedNodeIds.end()) {
            uint32_t delId = it->id;
            it = m_nodes.erase(it);
            ++removedCount;
            // Clean up any remaining dangling references to delId
            for (auto& remaining : m_nodes) {
                remaining.RemoveConnection(static_cast<int16_t>(delId));
            }
        } else {
            ++it;
        }
    }
    return removedCount;
}

size_t WaypointGraph::FixWaypoints(const BSPFile* bsp) {
    if (m_nodes.empty()) return 0;
    size_t fixes = 0;

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        bool isLadder = (m_nodes[i].flags & WPT_FLAG_LADDER) != 0;

        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            int16_t targetId = m_nodes[i].connections[c];
            if (targetId <= 0) continue;

            WaypointNode* target = GetNodeByID(static_cast<uint32_t>(targetId));
            if (!target || target->id == m_nodes[i].id) {
                m_nodes[i].connections[c] = -1;
                m_nodes[i].connectionFlags[c] = 0;
                ++fixes;
                continue;
            }

            bool targetLadder = (target->flags & WPT_FLAG_LADDER) != 0;
            float dz = target->origin.z - m_nodes[i].origin.z;

            // Height check (matching CS-EBOT FixWaypoints): cannot jump up > 72 units without ladder
            if (!isLadder && !targetLadder) {
                if (dz > 72.0f) {
                    m_nodes[i].connections[c] = -1;
                    m_nodes[i].connectionFlags[c] = 0;
                    ++fixes;
                    continue;
                }
                // Check if jump flag should be set for step > 18 units
                if (dz > 18.0f && dz <= 45.0f) {
                    if (!(m_nodes[i].connectionFlags[c] & WPT_CONN_JUMP)) {
                        m_nodes[i].connectionFlags[c] |= WPT_CONN_JUMP;
                        ++fixes;
                    }
                }
            }

            // Line of sight check if BSP geometry is loaded
            if (bsp && bsp->IsLoaded()) {
                Vector3 p1 = m_nodes[i].origin + Vector3(0.0f, 0.0f, 18.0f);
                Vector3 p2 = target->origin + Vector3(0.0f, 0.0f, 18.0f);
                BSPTraceResult tr;
                if (bsp->TraceWorld(p1, p2, HULL_POINT, &tr) && tr.fraction < 0.98f) {
                    // Blocked by world geometry - remove link
                    m_nodes[i].connections[c] = -1;
                    m_nodes[i].connectionFlags[c] = 0;
                    ++fixes;
                }
            }
        }
    }
    return fixes;
}

void WaypointGraph::CalculateWayzone(uint32_t nodeId, const BSPFile* bsp) {
    WaypointNode* node = GetNodeByID(nodeId);
    if (!node) return;

    // CS-EBOT CalculateWayzone rule: ladder, goal, camp, rescue, crouch must have radius 0
    if (node->flags & (WPT_FLAG_LADDER | WPT_FLAG_GOAL | WPT_FLAG_CAMP | WPT_FLAG_RESCUE | WPT_FLAG_CROUCH)) {
        node->radius = 0.0f;
        return;
    }

    // If connected to ladder or jump point, radius must be 0
    for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
        int16_t targetId = node->connections[c];
        if (targetId <= 0) continue;
        const WaypointNode* target = GetNodeByID(static_cast<uint32_t>(targetId));
        if (target && (target->flags & (WPT_FLAG_LADDER | WPT_FLAG_JUMP))) {
            node->radius = 0.0f;
            return;
        }
    }

    if (!bsp || !bsp->IsLoaded()) {
        node->radius = 32.0f;
        return;
    }

    // Radial raycasting across 16 horizontal directions (every 22.5 degrees)
    // Testing clearance up to 128 units in steps of 16 units
    float finalRadius = 128.0f;
    Vector3 origin = node->origin + Vector3(0.0f, 0.0f, 18.0f);

    for (int step = 32; step <= 128; step += 16) {
        float testDist = static_cast<float>(step);
        bool blocked = false;

        for (int ang = 0; ang < 16; ++ang) {
            float rad = ang * (2.0f * 3.14159265f / 16.0f);
            Vector3 dir(std::cos(rad), std::sin(rad), 0.0f);
            Vector3 targetPt = origin + dir * testDist;

            BSPTraceResult tr;
            if (bsp->TraceWorld(origin, targetPt, HULL_POINT, &tr) && tr.fraction < 1.0f) {
                blocked = true;
                finalRadius = std::max(0.0f, testDist - 16.0f);
                break;
            }

            // Downward floor test: verify ground doesn't drop off into void
            Vector3 dropStart = targetPt;
            Vector3 dropEnd = dropStart - Vector3(0.0f, 0.0f, testDist + 45.0f);
            BSPTraceResult dropTr;
            if (!bsp->TraceWorld(dropStart, dropEnd, HULL_POINT, &dropTr) || dropTr.fraction >= 1.0f) {
                blocked = true;
                finalRadius = std::max(0.0f, testDist - 16.0f);
                break;
            }
        }

        if (blocked) break;
    }

    node->radius = std::clamp(finalRadius, 0.0f, 128.0f);
}

size_t WaypointGraph::CalculateAllWayzones(const BSPFile* bsp) {
    size_t count = 0;
    for (auto& n : m_nodes) {
        CalculateWayzone(n.id, bsp);
        ++count;
    }
    return count;
}

bool WaypointGraph::ValidateNodes(std::vector<std::string>* outWarnings) {
    if (outWarnings) outWarnings->clear();
    std::unordered_map<uint32_t, size_t> incomingCount;

    for (const auto& n : m_nodes) {
        incomingCount[n.id] = 0;
    }

    bool allValid = true;
    for (const auto& n : m_nodes) {
        int activeOut = 0;
        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            int16_t targetId = n.connections[c];
            if (targetId <= 0) continue;

            if (targetId == static_cast<int16_t>(n.id)) {
                if (outWarnings) outWarnings->push_back("Waypoint #" + std::to_string(n.id) + " connects to itself!");
                allValid = false;
                continue;
            }

            const WaypointNode* target = GetNodeByID(static_cast<uint32_t>(targetId));
            if (!target) {
                if (outWarnings) outWarnings->push_back("Waypoint #" + std::to_string(n.id) + " links to non-existent node #" + std::to_string(targetId));
                allValid = false;
            } else {
                incomingCount[target->id]++;
                activeOut++;
            }
        }

        if (activeOut == 0 && incomingCount[n.id] == 0) {
            if (outWarnings) outWarnings->push_back("Waypoint #" + std::to_string(n.id) + " is completely disconnected (orphan).");
            allValid = false;
        }
    }

    return allValid;
}

WaypointGraph::WaypointOptimizeStats WaypointGraph::OptimizeGraph(const BSPFile* bsp, const WaypointOptimizeOptions& options) {
    WaypointOptimizeStats stats;
    auto tStart = std::chrono::high_resolution_clock::now();

    if (m_nodes.empty()) return stats;

    // Pass 1: Merge Overlapping Nodes (closer than mergeDistance)
    if (options.mergeOverlapping && options.mergeDistance > 0.0f) {
        float mergeDistSq = options.mergeDistance * options.mergeDistance;
        std::unordered_set<uint32_t> toDelete;

        for (size_t i = 0; i < m_nodes.size(); ++i) {
            uint32_t keepId = m_nodes[i].id;
            if (toDelete.count(keepId)) continue;
            Vector3 keepPos = m_nodes[i].origin;

            for (size_t j = i + 1; j < m_nodes.size(); ++j) {
                uint32_t checkId = m_nodes[j].id;
                if (toDelete.count(checkId)) continue;

                Vector3 diff = m_nodes[j].origin - keepPos;
                if (diff.Dot(diff) < mergeDistSq) {
                    // Don't merge if they have incompatible objective flags
                    uint32_t critFlags = WPT_FLAG_GOAL | WPT_FLAG_RESCUE | WPT_FLAG_LADDER;
                    if ((m_nodes[i].flags & critFlags) && (m_nodes[j].flags & critFlags) &&
                        (m_nodes[i].flags & critFlags) != (m_nodes[j].flags & critFlags)) {
                        continue;
                    }

                    // Merge flags and max radius
                    m_nodes[i].flags |= m_nodes[j].flags;
                    m_nodes[i].radius = std::max(m_nodes[i].radius, m_nodes[j].radius);

                    // Transfer outgoing connections from j to i
                    for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                        int16_t target = m_nodes[j].connections[c];
                        if (target > 0 && target != static_cast<int16_t>(keepId)) {
                            m_nodes[i].AddConnection(target, m_nodes[j].connectionFlags[c]);
                        }
                    }

                    // Redirect incoming connections pointing to checkId to point to keepId
                    for (auto& n : m_nodes) {
                        if (n.id == checkId || n.id == keepId) continue;
                        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                            if (n.connections[c] == static_cast<int16_t>(checkId)) {
                                n.connections[c] = static_cast<int16_t>(keepId);
                            }
                        }
                    }

                    toDelete.insert(checkId);
                    stats.overlappingMerged++;
                }
            }
        }

        if (!toDelete.empty()) {
            m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(), [&](const WaypointNode& n) {
                return toDelete.count(n.id) > 0;
            }), m_nodes.end());
            // Clean any remaining self-references or references to deleted
            for (auto& n : m_nodes) {
                for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                    if (n.connections[c] == static_cast<int16_t>(n.id) ||
                        (n.connections[c] > 0 && toDelete.count(static_cast<uint32_t>(n.connections[c])))) {
                        n.connections[c] = -1;
                        n.connectionFlags[c] = 0;
                    }
                }
            }
        }
    }

    // Pass 2: Prune Blocked Connections using BSP Collision Traces
    if (options.pruneBlockedLinks && bsp && bsp->IsLoaded()) {
        for (auto& n : m_nodes) {
            for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                int16_t targetId = n.connections[c];
                if (targetId <= 0) continue;

                const WaypointNode* target = GetNodeByID(static_cast<uint32_t>(targetId));
                if (!target) {
                    n.connections[c] = -1;
                    n.connectionFlags[c] = 0;
                    stats.blockedLinksPruned++;
                    continue;
                }

                // If jump or ladder link, allow higher clearance
                if (n.connectionFlags[c] & (WPT_CONN_JUMP | WPT_CONN_DOUBLE)) continue;
                if ((n.flags & WPT_FLAG_LADDER) || (target->flags & WPT_FLAG_LADDER)) continue;

                Vector3 start = n.origin + Vector3(0.0f, 0.0f, 18.0f);
                Vector3 end = target->origin + Vector3(0.0f, 0.0f, 18.0f);
                BSPTraceResult tr;
                if (bsp->TraceWorld(start, end, HULL_POINT, &tr)) {
                    if (tr.fraction < 0.95f || tr.startsolid || tr.allsolid) {
                        n.connections[c] = -1;
                        n.connectionFlags[c] = 0;
                        stats.blockedLinksPruned++;
                    }
                }
            }
        }
    }

    // Pass 3: Fix Flat-Ground One-Way Links
    if (options.fixOneWayLinks) {
        for (size_t i = 0; i < m_nodes.size(); ++i) {
            uint32_t fromId = m_nodes[i].id;
            Vector3 fromPos = m_nodes[i].origin;

            for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                int16_t targetId = m_nodes[i].connections[c];
                if (targetId <= 0) continue;

                WaypointNode* targetNode = GetNodeByID(static_cast<uint32_t>(targetId));
                if (!targetNode) continue;

                // If target doesn't connect back to us
                if (!targetNode->HasConnectionTo(static_cast<int16_t>(fromId))) {
                    float zDiff = std::abs(fromPos.z - targetNode->origin.z);
                    // If flat walkable surface and not an intentional jump drop
                    if (zDiff <= 18.0f && !(m_nodes[i].connectionFlags[c] & WPT_CONN_JUMP)) {
                        bool losOk = true;
                        if (bsp && bsp->IsLoaded()) {
                            BSPTraceResult tr;
                            bsp->TraceWorld(targetNode->origin + Vector3(0.0f, 0.0f, 18.0f),
                                            fromPos + Vector3(0.0f, 0.0f, 18.0f),
                                            HULL_POINT, &tr);
                            if (tr.fraction < 0.95f || tr.startsolid || tr.allsolid) losOk = false;
                        }
                        if (losOk) {
                            if (targetNode->AddConnection(static_cast<int16_t>(fromId), m_nodes[i].connectionFlags[c])) {
                                stats.oneWayLinksFixed++;
                            }
                        }
                    }
                }
            }
        }
    }

    // Pass 4: Prune Redundant Co-linear Nodes along straight corridors
    if (options.pruneCollinear) {
        float cosTol = std::cos(options.collinearMaxAngle * (3.14159265f / 180.0f));
        std::unordered_set<uint32_t> collinearToDelete;

        for (const auto& mid : m_nodes) {
            // Never prune nodes with tactical / mission objectives
            uint32_t preserveFlags = WPT_FLAG_GOAL | WPT_FLAG_RESCUE | WPT_FLAG_CAMP |
                                     WPT_FLAG_SNIPER | WPT_FLAG_LADDER | WPT_FLAG_USEBUTTON |
                                     WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH;
            if (mid.flags & preserveFlags) continue;

            // Check if node has exactly two active connections (pass-through)
            std::vector<int16_t> activeConns;
            for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                if (mid.connections[c] > 0) activeConns.push_back(mid.connections[c]);
            }

            if (activeConns.size() == 2) {
                uint32_t idA = static_cast<uint32_t>(activeConns[0]);
                uint32_t idC = static_cast<uint32_t>(activeConns[1]);
                if (collinearToDelete.count(idA) || collinearToDelete.count(idC)) continue;

                WaypointNode* nodeA = GetNodeByID(idA);
                WaypointNode* nodeC = GetNodeByID(idC);
                if (!nodeA || !nodeC || idA == idC) continue;

                Vector3 dir1 = (mid.origin - nodeA->origin);
                Vector3 dir2 = (nodeC->origin - mid.origin);
                float len1 = dir1.Length();
                float len2 = dir2.Length();

                if (len1 > 1.0f && len2 > 1.0f && (len1 + len2) <= 320.0f) {
                    dir1 = dir1 * (1.0f / len1);
                    dir2 = dir2 * (1.0f / len2);
                    float dot = dir1.Dot(dir2);

                    if (dot >= cosTol) {
                        // Check if line of sight exists directly from A to C
                        bool canBypass = true;
                        if (bsp && bsp->IsLoaded()) {
                            BSPTraceResult tr;
                            bsp->TraceWorld(nodeA->origin + Vector3(0.0f, 0.0f, 18.0f),
                                            nodeC->origin + Vector3(0.0f, 0.0f, 18.0f),
                                            HULL_POINT, &tr);
                            if (tr.fraction < 0.95f || tr.startsolid || tr.allsolid) {
                                canBypass = false;
                            }
                        }

                        if (canBypass) {
                            // Link A directly to C
                            bool aHasRev = nodeA->HasConnectionTo(static_cast<int16_t>(mid.id));
                            bool cHasRev = nodeC->HasConnectionTo(static_cast<int16_t>(mid.id));

                            nodeA->RemoveConnection(static_cast<int16_t>(mid.id));
                            nodeC->RemoveConnection(static_cast<int16_t>(mid.id));

                            nodeA->AddConnection(static_cast<int16_t>(idC), WPT_CONN_NONE);
                            if (aHasRev && cHasRev) {
                                nodeC->AddConnection(static_cast<int16_t>(idA), WPT_CONN_NONE);
                            }

                            collinearToDelete.insert(mid.id);
                            stats.collinearPruned++;
                        }
                    }
                }
            }
        }

        if (!collinearToDelete.empty()) {
            m_nodes.erase(std::remove_if(m_nodes.begin(), m_nodes.end(), [&](const WaypointNode& n) {
                return collinearToDelete.count(n.id) > 0;
            }), m_nodes.end());
            for (auto& n : m_nodes) {
                for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                    if (n.connections[c] > 0 && collinearToDelete.count(static_cast<uint32_t>(n.connections[c]))) {
                        n.connections[c] = -1;
                        n.connectionFlags[c] = 0;
                    }
                }
            }
        }
    }

    // Pass 5: Prune Dead-End Orphans
    if (options.pruneOrphans) {
        stats.orphansRemoved = DeleteOrphanNodes();
    }

    // Pass 6: Recalculate Optimal Wayzone Radii
    if (options.recalculateWayzones) {
        stats.wayzonesCalculated = CalculateAllWayzones(bsp);
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    stats.durationSeconds = std::chrono::duration<double>(tEnd - tStart).count();
    stats.totalModified = stats.overlappingMerged + stats.collinearPruned + stats.blockedLinksPruned + stats.oneWayLinksFixed + stats.orphansRemoved;

    return stats;
}

