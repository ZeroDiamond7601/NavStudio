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
#include <queue>

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

WaypointNode* WaypointGraph::InsertNode(const WaypointNode& node) {
    auto it = std::find_if(m_nodes.begin(), m_nodes.end(), [&node](const WaypointNode& n) {
        return n.id == node.id;
    });
    if (it != m_nodes.end()) {
        *it = node;
        return &(*it);
    }
    m_nodes.push_back(node);
    if (node.id >= m_nextId) {
        m_nextId = node.id + 1;
    }
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

            const WaypointNode* toNode = GetNodeByID(cand.id);
            if (!toNode) continue;
            float candDist = std::sqrt(cand.distSq);

            // Relative Neighborhood Graph (RNG) / Gabriel rule:
            // Check if there exists an intermediate closer node 'mid' inside the lune between 'from' and 'to'.
            // If mid is closer to both 'from' and 'to' than candDist, then connecting 'from' directly to 'to'
            // is a redundant diagonal cross-link or hallway bypass that creates bot confusion and visual clutter.
            bool hasIntermediateNode = false;
            for (const auto& otherCand : candidates) {
                if (otherCand.id == cand.id) continue;
                if (otherCand.distSq >= cand.distSq) break; // candidates are sorted by distance
                const WaypointNode* midNode = GetNodeByID(otherCand.id);
                if (!midNode) continue;
                if (std::abs(midNode->origin.z - from.origin.z) > 36.0f) continue;

                float dFromMid = std::sqrt(otherCand.distSq);
                float dToMid = (toNode->origin - midNode->origin).Length();
                if (std::max(dFromMid, dToMid) < candDist * 0.94f) {
                    hasIntermediateNode = true;
                    break;
                }
            }

            if (hasIntermediateNode) {
                continue; // Prune diagonal cross-link or corridor bypass
            }

            uint16_t cFlags = (cand.dz > 18.0f) ? WPT_CONN_JUMP : WPT_CONN_NONE;
            if (ConnectNodes(from.id, cand.id, true, cFlags)) {
                ++created;
            }
        }
    }
    return created;
}

WaypointGraph::WaypointAnalysisStats WaypointGraph::AnalyzeGraph(const BSPFile* bsp, GameMod mod, WaypointProgressCallback progressCb) {
    WaypointAnalysisStats stats;
    stats.totalScanned = m_nodes.size();
    if (m_nodes.empty()) return stats;

    bool hasBsp = (bsp && bsp->IsLoaded());
    size_t scanned = 0;

    for (auto& node : m_nodes) {
        if (progressCb && (scanned % 50 == 0 || scanned + 1 == m_nodes.size())) {
            float frac = static_cast<float>(scanned) / static_cast<float>(m_nodes.size());
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Analyzing node %zu / %zu (%.0f%%)", scanned + 1, m_nodes.size(), frac * 100.0f);
            progressCb(frac, buf);
        }
        scanned++;
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

    if (progressCb) progressCb(1.0f, "Analysis complete!");
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

size_t WaypointGraph::CalculateAllWayzones(const BSPFile* bsp, WaypointProgressCallback progressCb) {
    if (!bsp || !bsp->IsLoaded() || m_nodes.empty()) return 0;
    size_t count = 0;
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        CalculateWayzone(m_nodes[i].id, bsp);
        ++count;
        if (progressCb && (count % 20 == 0 || count == m_nodes.size())) {
            float frac = static_cast<float>(count) / static_cast<float>(m_nodes.size());
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Calculating wayzone radii: %zu / %zu (%.0f%%)", count, m_nodes.size(), frac * 100.0f);
            progressCb(frac, buf);
        }
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

WaypointGraph::WaypointOptimizeStats WaypointGraph::OptimizeGraph(const BSPFile* bsp, const WaypointOptimizeOptions& options, WaypointProgressCallback progressCb) {
    WaypointOptimizeStats stats;
    auto tStart = std::chrono::high_resolution_clock::now();

    if (m_nodes.empty()) return stats;

    // Pass 1: Merge Overlapping Nodes (closer than mergeDistance)
    if (progressCb) progressCb(0.05f, "Pass 1/6: Merging overlapping nodes...");
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
    if (progressCb) progressCb(0.20f, "Pass 2/6: Pruning blocked links with collision traces...");
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
    if (progressCb) progressCb(0.40f, "Pass 3/6: Restoring ground one-way links...");
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

    // Pass 3b: Prune Planar Crossing Links and Redundant Diagonal Chords
    if (progressCb) progressCb(0.50f, "Pass 3b/6: Pruning crossing links and redundant diagonals...");
    if (options.pruneCrossingLinks || options.pruneRedundantDiagonals) {
        struct EdgeInfo {
            uint32_t u, v;
            Vector3 p1, p2;
            float len;
        };
        std::vector<EdgeInfo> edges;
        for (const auto& n : m_nodes) {
            for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                int16_t tid = n.connections[c];
                if (tid > 0 && static_cast<uint32_t>(tid) > n.id) {
                    const WaypointNode* target = GetNodeByID(static_cast<uint32_t>(tid));
                    if (target) {
                        float l = (target->origin - n.origin).Length();
                        edges.push_back({ n.id, target->id, n.origin, target->origin, l });
                    }
                }
            }
        }

        auto CCW = [](float ax, float ay, float bx, float by, float cx, float cy) {
            return (cy - ay) * (bx - ax) > (by - ay) * (cx - ax);
        };
        auto SegmentsIntersect2D = [&](const Vector3& a, const Vector3& b, const Vector3& c, const Vector3& d) -> bool {
            bool c1 = CCW(a.x, a.y, c.x, c.y, d.x, d.y) != CCW(b.x, b.y, c.x, c.y, d.x, d.y);
            bool c2 = CCW(a.x, a.y, b.x, b.y, c.x, c.y) != CCW(a.x, a.y, b.x, b.y, d.x, d.y);
            return c1 && c2;
        };

        // 1. Detect intersecting crossing edges (e.g. "X" criss-crosses across floors)
        if (options.pruneCrossingLinks) {
            std::vector<std::pair<uint32_t, uint32_t>> toPrune;
            for (size_t e1 = 0; e1 < edges.size(); ++e1) {
                for (size_t e2 = e1 + 1; e2 < edges.size(); ++e2) {
                    const auto& a = edges[e1];
                    const auto& b = edges[e2];
                    if (a.u == b.u || a.u == b.v || a.v == b.u || a.v == b.v) continue;
                    if (std::abs(a.p1.z - b.p1.z) > 40.0f) continue;

                    if (SegmentsIntersect2D(a.p1, a.p2, b.p1, b.p2)) {
                        // Between the crossing pair, prune the longer edge
                        if (a.len >= b.len) {
                            toPrune.push_back({ a.u, a.v });
                        } else {
                            toPrune.push_back({ b.u, b.v });
                        }
                    }
                }
            }

            for (const auto& pr : toPrune) {
                DisconnectNodes(pr.first, pr.second, true);
                stats.crossingLinksPruned++;
            }
        }

        // 2. Detect quad diagonal chords: in any quadrilateral A-B-C-D-A, if diagonal A-C or B-D exists, prune it
        if (options.pruneRedundantDiagonals) {
            std::vector<std::pair<uint32_t, uint32_t>> diagToPrune;
            for (const auto& nA : m_nodes) {
                for (int cA = 0; cA < WPT_MAX_CONNECTIONS; ++cA) {
                    int16_t idB = nA.connections[cA];
                    if (idB <= 0 || static_cast<uint32_t>(idB) <= nA.id) continue;
                    const WaypointNode* nB = GetNodeByID(static_cast<uint32_t>(idB));
                    if (!nB) continue;

                    for (int cA2 = 0; cA2 < WPT_MAX_CONNECTIONS; ++cA2) {
                        int16_t idC = nA.connections[cA2];
                        if (idC <= 0 || idC == idB) continue;
                        const WaypointNode* nC = GetNodeByID(static_cast<uint32_t>(idC));
                        if (!nC) continue;

                        if (nB->HasConnectionTo(idC)) {
                            // Triangle A-B-C: check if AC is a redundant diagonal shortcut across B
                            float dAB = (nB->origin - nA.origin).Length();
                            float dBC = (nC->origin - nB->origin).Length();
                            float dAC = (nC->origin - nA.origin).Length();

                            if (dAC > dAB * 1.15f && dAC > dBC * 1.15f && (dAB + dBC) < dAC * 1.50f) {
                                diagToPrune.push_back({ nA.id, static_cast<uint32_t>(idC) });
                            }
                        }
                    }
                }
            }

            for (const auto& pr : diagToPrune) {
                DisconnectNodes(pr.first, pr.second, true);
                stats.diagonalChordsPruned++;
            }
        }
    }

    // Pass 4: Prune Redundant Co-linear Nodes along straight corridors
    if (progressCb) progressCb(0.65f, "Pass 4/6: Pruning collinear corridor nodes...");
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

                if (len1 > 1.0f && len2 > 1.0f && (len1 + len2) <= 380.0f) {
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

                            nodeA->radius = std::max(nodeA->radius, mid.radius);
                            nodeC->radius = std::max(nodeC->radius, mid.radius);

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
    if (progressCb) progressCb(0.75f, "Pass 5/6: Pruning dead-end orphan nodes...");
    if (options.pruneOrphans) {
        stats.orphansRemoved = DeleteOrphanNodes();
    }

    // Pass 6: Recalculate Optimal Wayzone Radii
    if (options.recalculateWayzones) {
        if (progressCb) progressCb(0.85f, "Pass 6/6: Recalculating optimal wayzone radii...");
        auto subCb = [&](float p, const std::string& msg) {
            if (progressCb) progressCb(0.85f + p * 0.14f, msg);
        };
        stats.wayzonesCalculated = CalculateAllWayzones(bsp, subCb);
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    stats.durationSeconds = std::chrono::duration<double>(tEnd - tStart).count();
    stats.totalModified = stats.overlappingMerged + stats.collinearPruned + stats.blockedLinksPruned + stats.oneWayLinksFixed + stats.orphansRemoved + stats.crossingLinksPruned + stats.diagonalChordsPruned;

    if (progressCb) progressCb(1.0f, "Optimization complete!");
    return stats;
}

WaypointGraph::WaypointParkourStats WaypointGraph::GenerateParkour(const BSPFile* bsp, const WaypointParkourOptions& options, WaypointProgressCallback progressCb) {
    auto tStart = std::chrono::high_resolution_clock::now();
    WaypointParkourStats stats;
    if (m_nodes.empty()) return stats;

    const float cellSize = options.maxJumpDist;
    struct Hash3D {
        int x, y, z;
        bool operator==(const Hash3D& o) const { return x == o.x && y == o.y && z == o.z; }
    };
    struct Hasher3D {
        size_t operator()(const Hash3D& k) const {
            return (std::hash<int>()(k.x) * 73856093) ^
                   (std::hash<int>()(k.y) * 19349663) ^
                   (std::hash<int>()(k.z) * 83492791);
        }
    };
    std::unordered_map<Hash3D, std::vector<size_t>, Hasher3D> grid;
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        int gx = static_cast<int>(std::floor(m_nodes[i].origin.x / cellSize));
        int gy = static_cast<int>(std::floor(m_nodes[i].origin.y / cellSize));
        int gz = static_cast<int>(std::floor(m_nodes[i].origin.z / cellSize));
        grid[{gx, gy, gz}].push_back(i);
    }

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        if (progressCb && (i % 25 == 0 || i + 1 == m_nodes.size())) {
            float frac = static_cast<float>(i) / static_cast<float>(m_nodes.size());
            char buf[64];
            std::snprintf(buf, sizeof(buf), "Analyzing parkour trajectories: node %zu / %zu (%.0f%%)", i + 1, m_nodes.size(), frac * 100.0f);
            progressCb(frac, buf);
        }
        WaypointNode& nodeA = m_nodes[i];
        int gx = static_cast<int>(std::floor(nodeA.origin.x / cellSize));
        int gy = static_cast<int>(std::floor(nodeA.origin.y / cellSize));
        int gz = static_cast<int>(std::floor(nodeA.origin.z / cellSize));

        for (int dx = -1; dx <= 1; ++dx) {
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dzCell = -1; dzCell <= 1; ++dzCell) {
                    auto it = grid.find({gx + dx, gy + dy, gz + dzCell});
                    if (it == grid.end()) continue;

                    for (size_t j : it->second) {
                        if (i == j) continue;
                        WaypointNode& nodeB = m_nodes[j];

                        Vector3 diff = nodeB.origin - nodeA.origin;
                        float dist2D = std::hypot(diff.x, diff.y);
                        float dz = diff.z;

                        if (dist2D < options.minJumpDist || dist2D > options.maxJumpDist) continue;

                        // Case 1: Ledge / Crate Jump-Up (18u < dz <= maxJumpHeight)
                        if (options.detectCrateClimbs && dz > 18.0f && dz <= options.maxJumpHeight && dist2D <= 180.0f) {
                            if (!nodeA.HasConnectionTo(static_cast<int16_t>(nodeB.id))) {
                                bool canJump = true;
                                if (bsp && bsp->IsLoaded()) {
                                    float apexZ = nodeB.origin.z + 28.0f;
                                    Vector3 midPt = (nodeA.origin + nodeB.origin) * 0.5f;
                                    midPt.z = apexZ;
                                    BSPTraceResult tr1, tr2;
                                    bsp->TraceWorld(nodeA.origin + Vector3(0.0f, 0.0f, 36.0f), midPt, HULL_POINT, &tr1);
                                    bsp->TraceWorld(midPt, nodeB.origin + Vector3(0.0f, 0.0f, 36.0f), HULL_POINT, &tr2);
                                    if (tr1.fraction < 0.95f || tr2.fraction < 0.95f || tr1.startsolid || tr2.startsolid) {
                                        canJump = false;
                                    }
                                }

                                if (canJump) {
                                    uint16_t cflags = WPT_CONN_JUMP;
                                    if (dz > 45.0f) cflags |= WPT_CONN_CROUCH;
                                    nodeA.AddConnection(static_cast<int16_t>(nodeB.id), cflags);
                                    nodeA.flags |= WPT_FLAG_JUMP;

                                    if (dz <= options.maxDropHeight && !nodeB.HasConnectionTo(static_cast<int16_t>(nodeA.id))) {
                                        nodeB.AddConnection(static_cast<int16_t>(nodeA.id), WPT_CONN_NONE);
                                    }
                                    ++stats.jumpUpsCreated;
                                    ++stats.totalParkourLinks;
                                }
                            }
                        }

                        // Case 2: Chasm / Gap Jump across Voids (|dz| <= 36u, void below)
                        if (options.detectChasmLeaps && std::abs(dz) <= 36.0f && dist2D >= 64.0f && dist2D <= options.maxJumpDist) {
                            if (!nodeA.HasConnectionTo(static_cast<int16_t>(nodeB.id))) {
                                bool isGap = false;
                                if (bsp && bsp->IsLoaded()) {
                                    Vector3 midPt = (nodeA.origin + nodeB.origin) * 0.5f;
                                    BSPTraceResult floorTr;
                                    bsp->TraceWorld(midPt + Vector3(0.0f, 0.0f, 10.0f), midPt - Vector3(0.0f, 0.0f, 180.0f), HULL_POINT, &floorTr);
                                    if (floorTr.fraction >= 0.80f || (midPt.z - floorTr.endpos.z) > 48.0f) {
                                        BSPTraceResult trA, trB;
                                        Vector3 apexPt = midPt + Vector3(0.0f, 0.0f, 24.0f);
                                        bsp->TraceWorld(nodeA.origin + Vector3(0.0f, 0.0f, 36.0f), apexPt, HULL_POINT, &trA);
                                        bsp->TraceWorld(apexPt, nodeB.origin + Vector3(0.0f, 0.0f, 36.0f), HULL_POINT, &trB);
                                        if (trA.fraction >= 0.95f && trB.fraction >= 0.95f && !trA.startsolid && !trB.startsolid) {
                                            isGap = true;
                                        }
                                    }
                                }

                                if (isGap) {
                                    nodeA.AddConnection(static_cast<int16_t>(nodeB.id), WPT_CONN_JUMP);
                                    nodeB.AddConnection(static_cast<int16_t>(nodeA.id), WPT_CONN_JUMP);
                                    nodeA.flags |= WPT_FLAG_JUMP;
                                    nodeB.flags |= WPT_FLAG_JUMP;
                                    ++stats.gapJumpsCreated;
                                    stats.totalParkourLinks += 2;
                                }
                            }
                        }

                        // Case 3: Drop-Down Parkour Shortcut (-maxDropHeight <= dz < -45u)
                        if (options.detectDropShortcuts && dz < -45.0f && dz >= -options.maxDropHeight && dist2D <= 160.0f) {
                            if (!nodeA.HasConnectionTo(static_cast<int16_t>(nodeB.id))) {
                                bool canDrop = true;
                                if (bsp && bsp->IsLoaded()) {
                                    BSPTraceResult losTr;
                                    bsp->TraceWorld(nodeA.origin + Vector3(0.0f, 0.0f, 18.0f), nodeB.origin + Vector3(0.0f, 0.0f, 18.0f), HULL_POINT, &losTr);
                                    if (losTr.fraction < 0.95f || losTr.startsolid) canDrop = false;
                                }
                                if (canDrop) {
                                    nodeA.AddConnection(static_cast<int16_t>(nodeB.id), WPT_CONN_JUMP);
                                    ++stats.dropJumpsCreated;
                                    ++stats.totalParkourLinks;
                                }
                            }
                        }

                        // Case 4: Double Jump / Boost Assist
                        if (options.detectDoubleJumps && dz > options.maxJumpHeight && dz <= 130.0f && dist2D <= 75.0f) {
                            if (!nodeA.HasConnectionTo(static_cast<int16_t>(nodeB.id))) {
                                bool canBoost = true;
                                if (bsp && bsp->IsLoaded()) {
                                    BSPTraceResult losTr;
                                    bsp->TraceWorld(nodeA.origin + Vector3(0.0f, 0.0f, 36.0f), nodeB.origin + Vector3(0.0f, 0.0f, 36.0f), HULL_POINT, &losTr);
                                    if (losTr.fraction < 0.90f || losTr.startsolid) canBoost = false;
                                }
                                if (canBoost) {
                                    nodeA.AddConnection(static_cast<int16_t>(nodeB.id), WPT_CONN_DOUBLE);
                                    nodeA.flags |= WPT_FLAG_DJUMP;
                                    ++stats.doubleJumpsCreated;
                                    ++stats.totalParkourLinks;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    auto tEnd = std::chrono::high_resolution_clock::now();
    stats.durationSeconds = std::chrono::duration<double>(tEnd - tStart).count();
    if (progressCb) progressCb(1.0f, "Parkour links generated!");
    return stats;
}

bool WaypointGraph::FindPath(uint32_t startId, uint32_t goalId, std::vector<uint32_t>& outPath, float* outTotalCost) const {
    outPath.clear();
    if (outTotalCost) *outTotalCost = 0.0f;
    if (startId == 0 || goalId == 0) return false;
    if (startId == goalId) {
        if (GetNodeByID(startId)) {
            outPath.push_back(startId);
            return true;
        }
        return false;
    }

    const WaypointNode* startNode = GetNodeByID(startId);
    const WaypointNode* goalNode = GetNodeByID(goalId);
    if (!startNode || !goalNode) return false;

    struct NodeRecord {
        uint32_t id;
        float fScore;
        bool operator>(const NodeRecord& other) const { return fScore > other.fScore; }
    };

    std::priority_queue<NodeRecord, std::vector<NodeRecord>, std::greater<NodeRecord>> openSet;
    std::unordered_map<uint32_t, float> gScore;
    std::unordered_map<uint32_t, uint32_t> cameFrom;

    auto Heuristic = [](const Vector3& a, const Vector3& b) -> float {
        return (a - b).Length();
    };

    gScore[startId] = 0.0f;
    openSet.push({ startId, Heuristic(startNode->origin, goalNode->origin) });

    while (!openSet.empty()) {
        NodeRecord current = openSet.top();
        openSet.pop();

        if (current.id == goalId) {
            uint32_t curr = goalId;
            while (curr != 0) {
                outPath.push_back(curr);
                auto it = cameFrom.find(curr);
                if (it != cameFrom.end()) curr = it->second;
                else break;
            }
            std::reverse(outPath.begin(), outPath.end());
            if (outTotalCost) *outTotalCost = gScore[goalId];
            return true;
        }

        auto gIt = gScore.find(current.id);
        const WaypointNode* node = GetNodeByID(current.id);
        if (!node) continue;

        float currG = (gIt != gScore.end()) ? gIt->second : 0.0f;
        if (current.fScore > currG + Heuristic(node->origin, goalNode->origin) + 0.1f) {
            continue;
        }

        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            int16_t neighborId16 = node->connections[c];
            if (neighborId16 <= 0) continue;
            uint32_t neighborId = static_cast<uint32_t>(neighborId16);

            const WaypointNode* neighbor = GetNodeByID(neighborId);
            if (!neighbor) continue;

            uint16_t connFlags = node->connectionFlags[c];
            float edgeDist = (neighbor->origin - node->origin).Length();
            float edgeCost = edgeDist;

            // Movement penalties: jumps cost slightly more, crouching has lower speed (higher cost)
            if (connFlags & WPT_CONN_JUMP) {
                edgeCost += 25.0f;
            }
            if ((connFlags & WPT_CONN_CROUCH) || (neighbor->flags & WPT_FLAG_CROUCH)) {
                edgeCost *= 1.4f;
            }
            if (neighbor->flags & WPT_FLAG_LADDER) {
                edgeCost *= 1.2f;
            }

            float tentativeG = currG + edgeCost;
            auto neighGIt = gScore.find(neighborId);
            if (neighGIt == gScore.end() || tentativeG < neighGIt->second) {
                cameFrom[neighborId] = current.id;
                gScore[neighborId] = tentativeG;
                float f = tentativeG + Heuristic(neighbor->origin, goalNode->origin);
                openSet.push({ neighborId, f });
            }
        }
    }

    return false;
}

WaypointPathAudit WaypointGraph::AuditPath(const std::vector<uint32_t>& path, const class BSPFile* bsp) const {
    WaypointPathAudit audit;
    if (path.size() < 2) {
        audit.success = (path.size() == 1);
        return audit;
    }

    audit.success = true;
    const float kRunSpeed = 250.0f;    // Standard CS running speed (units/sec)
    const float kCrouchSpeed = 90.0f;  // Standard CS crouch speed (units/sec)
    const float kLadderSpeed = 150.0f; // Climbing speed (units/sec)

    for (size_t i = 0; i + 1 < path.size(); ++i) {
        uint32_t fromId = path[i];
        uint32_t toId = path[i + 1];
        const WaypointNode* nodeA = GetNodeByID(fromId);
        const WaypointNode* nodeB = GetNodeByID(toId);

        WaypointPathStep step;
        step.fromId = fromId;
        step.toId = toId;

        if (!nodeA || !nodeB) {
            step.warning = "Invalid waypoint ID in path";
            audit.warnings.push_back(step.warning);
            audit.steps.push_back(step);
            continue;
        }

        step.fromPos = nodeA->origin;
        step.toPos = nodeB->origin;
        step.distance = (nodeB->origin - nodeA->origin).Length();
        step.deltaZ = nodeB->origin.z - nodeA->origin.z;

        // Find connection flags
        for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
            if (nodeA->connections[c] == static_cast<int16_t>(toId)) {
                step.connFlags = nodeA->connectionFlags[c];
                break;
            }
        }

        step.isJump = (step.connFlags & WPT_CONN_JUMP) != 0 || (step.deltaZ > 18.0f);
        step.isCrouch = (step.connFlags & WPT_CONN_CROUCH) != 0 || (nodeB->flags & WPT_FLAG_CROUCH) != 0;
        step.isLadder = (nodeA->flags & WPT_FLAG_LADDER) != 0 && (nodeB->flags & WPT_FLAG_LADDER) != 0;

        // Step travel time estimate
        float speed = kRunSpeed;
        if (step.isLadder) speed = kLadderSpeed;
        else if (step.isCrouch) speed = kCrouchSpeed;
        float stepTime = step.distance / std::max(10.0f, speed);
        if (step.isJump) stepTime += 0.35f;

        // LOS Collision Validation with BSP
        if (bsp && bsp->IsLoaded()) {
            BSPTraceResult tr;
            Vector3 startProbe = nodeA->origin + Vector3(0.0f, 0.0f, step.isCrouch ? 18.0f : 36.0f);
            Vector3 endProbe = nodeB->origin + Vector3(0.0f, 0.0f, step.isCrouch ? 18.0f : 36.0f);
            bsp->TraceWorld(startProbe, endProbe, HULL_POINT, &tr);
            if (tr.fraction < 0.95f || tr.startsolid) {
                step.warning = "Path segment obstructed by level geometry (LOS fraction: " + std::to_string(tr.fraction) + ")";
                audit.warnings.push_back("Step " + std::to_string(i + 1) + ": " + step.warning);
            }
        }

        // Height warning check
        if (step.deltaZ > 55.0f && !step.isLadder) {
            std::string w = "Elevation rise (" + std::to_string(step.deltaZ) + "u) exceeds standard jump height";
            step.warning = step.warning.empty() ? w : (step.warning + " | " + w);
            audit.warnings.push_back("Step " + std::to_string(i + 1) + ": " + w);
        } else if (step.deltaZ < -350.0f) {
            std::string w = "High drop (" + std::to_string(-step.deltaZ) + "u) causes severe falling damage";
            step.warning = step.warning.empty() ? w : (step.warning + " | " + w);
            audit.warnings.push_back("Step " + std::to_string(i + 1) + ": " + w);
        }

        audit.totalDistance += step.distance;
        audit.estimatedDurationSec += stepTime;
        audit.steps.push_back(step);
    }

    return audit;
}

