#include "waypoint/waypoint_graph.h"
#include "waypoint/compressor.h"
#include "bsp/bsp_file.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
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

size_t WaypointGraph::AutoLinkNodes(float maxDist) {
    size_t created = 0;
    float maxDistSq = maxDist * maxDist;

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        for (size_t j = i + 1; j < m_nodes.size(); ++j) {
            Vector3 diff = m_nodes[j].origin - m_nodes[i].origin;
            float dsq = diff.Dot(diff);
            if (dsq <= maxDistSq) {
                // Check height differential (standard step/jump reachable < 45 units)
                if (std::abs(diff.z) <= 45.0f) {
                    if (ConnectNodes(m_nodes[i].id, m_nodes[j].id, true, WPT_CONN_NONE)) {
                        ++created;
                    }
                }
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

// --- High-Level Dispatch Load / Save ---

bool WaypointGraph::Load(const std::string& filepath) {
    std::string lowerPath = filepath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

    // 1. Try loader matching extension or header magic first
    if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".ewp") == 0) {
        if (LoadEBot(filepath)) return true;
    } else if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".spt") == 0) {
        if (LoadSyPB(filepath)) return true;
    } else if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".pwf") == 0) {
        FILE* f = std::fopen(filepath.c_str(), "rb");
        if (f) {
            char magic[8]{0};
            std::fread(magic, 1, 8, f);
            std::fclose(f);
            if (std::strncmp(magic, "EBOTWP", 6) == 0 && LoadEBot(filepath)) return true;
            if (std::strncmp(magic, "SyPB", 4) == 0 && LoadSyPB(filepath)) return true;
            if (std::strncmp(magic, "YaPB", 4) == 0 && LoadYaPB(filepath)) return true;
        }
        if (LoadYaPB(filepath)) return true;
        if (LoadPODBot(filepath)) return true;
    } else if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".wpt") == 0) {
        if (LoadPODBot(filepath)) return true;
    }

    // 2. Comprehensive fallback: sequentially try every parser
    if (LoadEBot(filepath)) return true;
    if (LoadYaPB(filepath)) return true;
    if (LoadSyPB(filepath)) return true;
    if (LoadPODBot(filepath)) return true;

    return false;
}

bool WaypointGraph::Save(const std::string& filepath, BotType bot, GameMod mod) {
    switch (bot) {
        case BotType::EBot: return SaveEBot(filepath, mod);
        case BotType::SyPB: return SaveSyPB(filepath, mod);
        case BotType::YaPB: return SaveYaPB(filepath, mod);
        case BotType::PODBot: return SavePODBot(filepath, mod);
    }
    return SaveEBot(filepath, mod);
}

// --- EBot (.ewp) Codec ---

#pragma pack(push, 1)
// Standard CS-EBOT v127 disk path (56 bytes, matching CS-EBOT's struct Path alignment)
struct EBotDiskPath {
    Vector3 origin;              // 12 bytes [0..11]
    uint32_t flags;              // 4 bytes  [12..15]
    uint8_t radius;              // 1 byte   [16]
    uint8_t mesh;                // 1 byte   [17]
    int16_t index[8];            // 16 bytes [18..33]
    uint16_t connectionFlags[8]; // 16 bytes [34..49]
    uint16_t _pad{0};            // 2 bytes  [50..51] alignment padding in CS-EBOT struct Path
    float gravity{0.0f};         // 4 bytes  [52..55]
};
static_assert(sizeof(EBotDiskPath) == 56, "EBotDiskPath must be 56 bytes to match CS-EBOT struct Path");

// Legacy packed 54-byte struct (for backwards compatibility if any legacy packed file exists)
struct EBotDiskPath54 {
    Vector3 origin;
    uint32_t flags;
    uint8_t radius;
    uint8_t mesh;
    int16_t index[8];
    uint16_t connectionFlags[8];
    float gravity;
};
static_assert(sizeof(EBotDiskPath54) == 54, "EBotDiskPath54 must be 54 bytes");

struct EBotLegacyPathOLD {
    int32_t pathNumber;
    int32_t flags;
    Vector3 origin;
    float radius;
    float campStartX;
    float campStartY;
    float campEndX;
    float campEndY;
    int16_t index[8];
    uint16_t connectionFlags[8];
    Vector3 connectionVelocity[8];
    int32_t distances[8];
    uint16_t standVis;
    uint16_t crouchVis;
};
#pragma pack(pop)

bool WaypointGraph::LoadEBot(const std::string& filepath) {
    FILE* f = std::fopen(filepath.c_str(), "rb");
    if (!f) return false;

    WaypointFileHeader hdr;
    if (std::fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fclose(f);
        return false;
    }

    Clear();
    m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
    m_author = std::string(hdr.author, strnlen(hdr.author, 31));
    m_activeBot = BotType::EBot;
    m_loadedPath = filepath;

    int numPoints = hdr.pointNumber;
    if (numPoints <= 0 || numPoints > 65536) {
        std::fclose(f);
        return false;
    }

    // Check version
    if (hdr.fileVersion == 127) { // Modern Compressed v127 (FV_WAYPOINT)
        std::fseek(f, 0, SEEK_END);
        long fileSz = std::ftell(f);
        long compSz = fileSz - static_cast<long>(sizeof(hdr));
        std::fseek(f, sizeof(hdr), SEEK_SET);

        if (compSz <= 0) {
            std::fclose(f);
            return false;
        }

        std::vector<uint8_t> compData(compSz);
        std::fread(compData.data(), 1, compSz, f);
        std::fclose(f);

        size_t expectedUncomp56 = numPoints * sizeof(EBotDiskPath);
        size_t expectedUncomp54 = numPoints * sizeof(EBotDiskPath54);
        std::vector<uint8_t> uncompData(expectedUncomp56 + 4096);
        size_t actualUncomp = 0;

        WaypointCompressor comp;
        bool decompOk = comp.Decode(compData.data(), compData.size(), uncompData.data(), uncompData.size(), actualUncomp);
        if (!decompOk || actualUncomp < expectedUncomp54) {
            // Fallback: raw uncompressed read if payload wasn't compressed
            if (compSz >= static_cast<long>(expectedUncomp56)) {
                std::memcpy(uncompData.data(), compData.data(), expectedUncomp56);
                actualUncomp = expectedUncomp56;
            } else if (compSz >= static_cast<long>(expectedUncomp54)) {
                std::memcpy(uncompData.data(), compData.data(), expectedUncomp54);
                actualUncomp = expectedUncomp54;
            } else {
                return false;
            }
        }

        bool is56 = (actualUncomp >= expectedUncomp56);
        const EBotDiskPath* diskPaths56 = reinterpret_cast<const EBotDiskPath*>(uncompData.data());
        const EBotDiskPath54* diskPaths54 = reinterpret_cast<const EBotDiskPath54*>(uncompData.data());

        m_nodes.resize(numPoints);
        for (int i = 0; i < numPoints; ++i) {
            m_nodes[i].id = static_cast<uint32_t>(i + 1);
            Vector3 org = is56 ? diskPaths56[i].origin : diskPaths54[i].origin;
            if (std::isnan(org.x) || std::isnan(org.y) || std::isnan(org.z) ||
                std::abs(org.x) > 65536.0f || std::abs(org.y) > 65536.0f || std::abs(org.z) > 65536.0f) {
                org = Vector3(0.0f, 0.0f, 0.0f);
            }
            m_nodes[i].origin = org;
            m_nodes[i].flags = is56 ? diskPaths56[i].flags : diskPaths54[i].flags;
            m_nodes[i].radius = static_cast<float>(is56 ? diskPaths56[i].radius : diskPaths54[i].radius);
            m_nodes[i].mesh = is56 ? diskPaths56[i].mesh : diskPaths54[i].mesh;
            m_nodes[i].gravity = is56 ? diskPaths56[i].gravity : diskPaths54[i].gravity;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = is56 ? diskPaths56[i].index[c] : diskPaths54[i].index[c];
                if (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) {
                    m_nodes[i].connections[c] = static_cast<int16_t>(rawIdx + 1);
                    m_nodes[i].connectionFlags[c] = is56 ? diskPaths56[i].connectionFlags[c] : diskPaths54[i].connectionFlags[c];
                } else {
                    m_nodes[i].connections[c] = -1;
                    m_nodes[i].connectionFlags[c] = 0;
                }
            }
        }
        m_nextId = static_cast<uint32_t>(numPoints + 1);
        return true;
    } else if (hdr.fileVersion == 126) { // Uncompressed raw EBotPath
        std::fseek(f, 0, SEEK_END);
        long dataSz = std::ftell(f) - static_cast<long>(sizeof(hdr));
        std::fseek(f, sizeof(hdr), SEEK_SET);

        bool is56 = (dataSz >= static_cast<long>(numPoints * sizeof(EBotDiskPath)));
        m_nodes.resize(numPoints);

        if (is56) {
            std::vector<EBotDiskPath> diskPaths(numPoints);
            std::fread(diskPaths.data(), sizeof(EBotDiskPath), numPoints, f);
            std::fclose(f);
            for (int i = 0; i < numPoints; ++i) {
                m_nodes[i].id = static_cast<uint32_t>(i + 1);
                m_nodes[i].origin = diskPaths[i].origin;
                m_nodes[i].flags = diskPaths[i].flags;
                m_nodes[i].radius = static_cast<float>(diskPaths[i].radius);
                m_nodes[i].mesh = diskPaths[i].mesh;
                m_nodes[i].gravity = diskPaths[i].gravity;
                for (int c = 0; c < 8; ++c) {
                    int16_t rawIdx = diskPaths[i].index[c];
                    m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                    m_nodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
                }
            }
        } else {
            std::vector<EBotDiskPath54> diskPaths(numPoints);
            std::fread(diskPaths.data(), sizeof(EBotDiskPath54), numPoints, f);
            std::fclose(f);
            for (int i = 0; i < numPoints; ++i) {
                m_nodes[i].id = static_cast<uint32_t>(i + 1);
                m_nodes[i].origin = diskPaths[i].origin;
                m_nodes[i].flags = diskPaths[i].flags;
                m_nodes[i].radius = static_cast<float>(diskPaths[i].radius);
                m_nodes[i].mesh = diskPaths[i].mesh;
                m_nodes[i].gravity = diskPaths[i].gravity;
                for (int c = 0; c < 8; ++c) {
                    int16_t rawIdx = diskPaths[i].index[c];
                    m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                    m_nodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
                }
            }
        }
        m_nextId = static_cast<uint32_t>(numPoints + 1);
        return true;
    } else { // Legacy SyPB / PODBot format (version < 126)
        std::vector<EBotLegacyPathOLD> diskPaths(numPoints);
        std::fread(diskPaths.data(), sizeof(EBotLegacyPathOLD), numPoints, f);
        std::fclose(f);

        m_nodes.resize(numPoints);
        for (int i = 0; i < numPoints; ++i) {
            m_nodes[i].id = static_cast<uint32_t>(i + 1);
            m_nodes[i].origin = diskPaths[i].origin;
            m_nodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            m_nodes[i].radius = diskPaths[i].radius;
            m_nodes[i].campYaw = diskPaths[i].campStartX;
            m_nodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                m_nodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
            }
        }
        m_nextId = static_cast<uint32_t>(numPoints + 1);
        return true;
    }
}

bool WaypointGraph::SaveEBot(const std::string& filepath, GameMod mod) {
    if (m_nodes.empty()) return false;

    WaypointFileHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::memcpy(hdr.magic, "EBOTWP!\0", 8);
    hdr.fileVersion = 127;
    hdr.pointNumber = static_cast<int32_t>(m_nodes.size());
    std::strncpy(hdr.mapName, m_mapName.c_str(), 31);
    std::strncpy(hdr.author, m_author.c_str(), 31);

    // Map unique runtime node IDs to zero-based contiguous file indices
    std::unordered_map<uint32_t, int16_t> idToIndex;
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        idToIndex[m_nodes[i].id] = static_cast<int16_t>(i);
    }

    std::vector<EBotDiskPath> diskPaths(m_nodes.size());
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        diskPaths[i].origin = m_nodes[i].origin;
        uint32_t f = m_nodes[i].flags;
        diskPaths[i].flags = f;
        diskPaths[i].radius = static_cast<uint8_t>(std::clamp(m_nodes[i].radius, 0.0f, 255.0f));
        diskPaths[i].mesh = m_nodes[i].mesh;
        diskPaths[i]._pad = 0;
        diskPaths[i].gravity = m_nodes[i].gravity;

        for (int c = 0; c < 8; ++c) {
            int16_t target = m_nodes[i].connections[c];
            int16_t diskIdx = -1;
            if (target > 0) {
                auto it = idToIndex.find(static_cast<uint32_t>(target));
                if (it != idToIndex.end() && it->second != static_cast<int16_t>(i)) {
                    diskIdx = it->second;
                }
            }
            diskPaths[i].index[c] = diskIdx;
            diskPaths[i].connectionFlags[c] = (diskIdx >= 0) ? m_nodes[i].connectionFlags[c] : 0;
        }
    }

    std::vector<uint8_t> compressedBytes;
    WaypointCompressor comp;
    const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(diskPaths.data());
    size_t rawSize = diskPaths.size() * sizeof(EBotDiskPath);

    if (!comp.Encode(rawBytes, rawSize, compressedBytes)) {
        return false;
    }

    FILE* f = std::fopen(filepath.c_str(), "wb");
    if (!f) return false;

    std::fwrite(&hdr, 1, sizeof(hdr), f);
    std::fwrite(compressedBytes.data(), 1, compressedBytes.size(), f);
    std::fclose(f);

    m_loadedPath = filepath;
    m_activeBot = BotType::EBot;
    m_activeMod = mod;
    return true;
}

// --- SyPB (.spt) Codec ---

bool WaypointGraph::LoadSyPB(const std::string& filepath) {
    FILE* f = std::fopen(filepath.c_str(), "rb");
    if (!f) return false;

    WaypointFileHeader hdr;
    if (std::fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fclose(f);
        return false;
    }

    Clear();
    m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
    m_author = std::string(hdr.author, strnlen(hdr.author, 31));
    m_activeBot = BotType::SyPB;
    m_loadedPath = filepath;

    int numPoints = hdr.pointNumber;
    if (numPoints <= 0 || numPoints > 65536) {
        std::fclose(f);
        return false;
    }

    std::fseek(f, 0, SEEK_END);
    long fileSz = std::ftell(f);
    long dataSz = fileSz - static_cast<long>(sizeof(hdr));
    std::fseek(f, sizeof(hdr), SEEK_SET);

    size_t perNodeSz = (numPoints > 0) ? (dataSz / numPoints) : 0;

    if (perNodeSz >= sizeof(EBotLegacyPathOLD)) { // 200 bytes
        std::vector<EBotLegacyPathOLD> diskPaths(numPoints);
        std::fread(diskPaths.data(), sizeof(EBotLegacyPathOLD), numPoints, f);
        std::fclose(f);

        m_nodes.resize(numPoints);
        for (int i = 0; i < numPoints; ++i) {
            m_nodes[i].id = static_cast<uint32_t>(i + 1);
            m_nodes[i].origin = diskPaths[i].origin;
            m_nodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            m_nodes[i].radius = diskPaths[i].radius;
            m_nodes[i].campYaw = diskPaths[i].campStartX;
            m_nodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints) ? (rawIdx + 1) : -1;
                m_nodes[i].connectionFlags[c] = diskPaths[i].connectionFlags[c];
            }
        }
    } else {
        std::vector<uint8_t> rawData(dataSz);
        std::fread(rawData.data(), 1, dataSz, f);
        std::fclose(f);

        m_nodes.resize(numPoints);
        // Fallback POD-Bot mm compatible parser
        for (int i = 0; i < numPoints; ++i) {
            m_nodes[i].id = static_cast<uint32_t>(i + 1);
            size_t offset = i * perNodeSz;
            if (offset + 24 <= static_cast<size_t>(dataSz)) {
                // If perNodeSz has pathNumber (104), origin is at offset + 8, otherwise offset + 4
                bool hasPathNum = (perNodeSz >= 104);
                size_t orgOff = offset + (hasPathNum ? 8 : 4);
                if (orgOff + 12 <= static_cast<size_t>(dataSz)) {
                    std::memcpy(&m_nodes[i].origin, &rawData[orgOff], sizeof(Vector3));
                }
            }
        }
    }

    m_nextId = static_cast<uint32_t>(numPoints + 1);
    return true;
}

bool WaypointGraph::SaveSyPB(const std::string& filepath, GameMod mod) {
    if (m_nodes.empty()) return false;

    WaypointFileHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::memcpy(hdr.magic, "SyPB!\0\0\0", 8);
    hdr.fileVersion = 125;
    hdr.pointNumber = static_cast<int32_t>(m_nodes.size());
    std::strncpy(hdr.mapName, m_mapName.c_str(), 31);
    std::strncpy(hdr.author, m_author.c_str(), 31);

    // Map unique runtime node IDs to zero-based contiguous file indices
    std::unordered_map<uint32_t, int16_t> idToIndex;
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        idToIndex[m_nodes[i].id] = static_cast<int16_t>(i);
    }

    std::vector<EBotLegacyPathOLD> diskPaths(m_nodes.size());
    std::memset(diskPaths.data(), 0, diskPaths.size() * sizeof(EBotLegacyPathOLD));

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        diskPaths[i].pathNumber = static_cast<int32_t>(i);
        uint32_t f = m_nodes[i].flags;
        if (mod == GameMod::Standard) {
            f &= ~(WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH | WPT_FLAG_ZOMBIEONLY | WPT_FLAG_HUMANONLY | WPT_FLAG_ZOMBIEPUSH | WPT_FLAG_HELICOPTER);
        }
        diskPaths[i].flags = static_cast<int32_t>(f);
        diskPaths[i].origin = m_nodes[i].origin;
        diskPaths[i].radius = m_nodes[i].radius;
        diskPaths[i].campStartX = m_nodes[i].campYaw;
        diskPaths[i].campStartY = m_nodes[i].campPitch;

        for (int c = 0; c < 8; ++c) {
            int16_t target = m_nodes[i].connections[c];
            int16_t diskTarget = -1;
            if (target > 0) {
                auto it = idToIndex.find(static_cast<uint32_t>(target));
                if (it != idToIndex.end() && it->second != static_cast<int16_t>(i)) {
                    diskTarget = it->second;
                }
            }
            diskPaths[i].index[c] = diskTarget;
            diskPaths[i].connectionFlags[c] = (diskTarget >= 0) ? m_nodes[i].connectionFlags[c] : 0;
            if (diskTarget >= 0) {
                Vector3 diff = m_nodes[diskTarget].origin - m_nodes[i].origin;
                diskPaths[i].distances[c] = static_cast<int32_t>(diff.Length());
            }
        }
    }

    FILE* f = std::fopen(filepath.c_str(), "wb");
    if (!f) return false;

    std::fwrite(&hdr, 1, sizeof(hdr), f);
    std::fwrite(diskPaths.data(), sizeof(EBotLegacyPathOLD), diskPaths.size(), f);
    std::fclose(f);

    m_loadedPath = filepath;
    m_activeBot = BotType::SyPB;
    m_activeMod = mod;
    return true;
}

// --- YaPB (.pwf) Codec ---

#pragma pack(push, 1)
struct YaPBDiskPath {
    int32_t flags;
    Vector3 origin;
    float radius;
    float campPitch;
    float campYaw;
    int16_t index[8];
    uint16_t connectionFlags[8];
};
#pragma pack(pop)

bool WaypointGraph::LoadYaPB(const std::string& filepath) {
    FILE* f = std::fopen(filepath.c_str(), "rb");
    if (!f) return false;

    WaypointFileHeader hdr;
    if (std::fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fclose(f);
        return false;
    }

    Clear();
    m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
    m_author = std::string(hdr.author, strnlen(hdr.author, 31));
    m_activeBot = BotType::YaPB;
    m_loadedPath = filepath;

    int numPoints = hdr.pointNumber;
    if (numPoints <= 0 || numPoints > 65536) {
        std::fclose(f);
        return false;
    }

    std::fseek(f, 0, SEEK_END);
    long fileSz = std::ftell(f);
    long payloadSz = fileSz - static_cast<long>(sizeof(hdr));
    std::fseek(f, sizeof(hdr), SEEK_SET);

    std::vector<uint8_t> payload(payloadSz);
    std::fread(payload.data(), 1, payloadSz, f);
    std::fclose(f);

    size_t expectedUncomp = numPoints * sizeof(YaPBDiskPath);
    std::vector<uint8_t> uncompData(expectedUncomp + 2048);
    size_t actualUncomp = 0;

    WaypointCompressor comp;
    bool decompOk = comp.Decode(payload.data(), payload.size(), uncompData.data(), uncompData.size(), actualUncomp);
    if (!decompOk || actualUncomp < expectedUncomp) {
        // Fallback: raw uncompressed YaPB or POD-Bot
        if (payloadSz >= static_cast<long>(expectedUncomp)) {
            std::memcpy(uncompData.data(), payload.data(), expectedUncomp);
        } else {
            return false;
        }
    }

    const YaPBDiskPath* diskPaths = reinterpret_cast<const YaPBDiskPath*>(uncompData.data());
    m_nodes.resize(numPoints);
    for (int i = 0; i < numPoints; ++i) {
        m_nodes[i].id = static_cast<uint32_t>(i + 1);
        m_nodes[i].origin = diskPaths[i].origin;
        m_nodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
        m_nodes[i].radius = diskPaths[i].radius;
        m_nodes[i].campPitch = diskPaths[i].campPitch;
        m_nodes[i].campYaw = diskPaths[i].campYaw;

        for (int c = 0; c < 8; ++c) {
            int16_t rawIdx = diskPaths[i].index[c];
            m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints) ? (rawIdx + 1) : -1;
            m_nodes[i].connectionFlags[c] = diskPaths[i].connectionFlags[c];
        }
    }
    m_nextId = static_cast<uint32_t>(numPoints + 1);
    return true;
}

bool WaypointGraph::SaveYaPB(const std::string& filepath, GameMod mod) {
    if (m_nodes.empty()) return false;

    WaypointFileHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::memcpy(hdr.magic, "YaPB!\0\0\0", 8);
    hdr.fileVersion = 7;
    hdr.pointNumber = static_cast<int32_t>(m_nodes.size());
    std::strncpy(hdr.mapName, m_mapName.c_str(), 31);
    std::strncpy(hdr.author, m_author.c_str(), 31);

    // Map unique runtime node IDs to zero-based contiguous file indices
    std::unordered_map<uint32_t, int16_t> idToIndex;
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        idToIndex[m_nodes[i].id] = static_cast<int16_t>(i);
    }

    std::vector<YaPBDiskPath> diskPaths(m_nodes.size());
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        uint32_t f = m_nodes[i].flags;
        // In YaPB standard, translate zombie camp to standard camp
        if (f & WPT_FLAG_ZMHMCAMP) f |= WPT_FLAG_CAMP;
        f &= ~(WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH | WPT_FLAG_ZOMBIEONLY | WPT_FLAG_HUMANONLY | WPT_FLAG_ZOMBIEPUSH | WPT_FLAG_HELICOPTER);

        diskPaths[i].flags = static_cast<int32_t>(f);
        diskPaths[i].origin = m_nodes[i].origin;
        diskPaths[i].radius = m_nodes[i].radius;
        diskPaths[i].campPitch = m_nodes[i].campPitch;
        diskPaths[i].campYaw = m_nodes[i].campYaw;

        for (int c = 0; c < 8; ++c) {
            int16_t target = m_nodes[i].connections[c];
            int16_t diskIdx = -1;
            if (target > 0) {
                auto it = idToIndex.find(static_cast<uint32_t>(target));
                if (it != idToIndex.end() && it->second != static_cast<int16_t>(i)) {
                    diskIdx = it->second;
                }
            }
            diskPaths[i].index[c] = diskIdx;
            diskPaths[i].connectionFlags[c] = (diskIdx >= 0) ? m_nodes[i].connectionFlags[c] : 0;
        }
    }

    std::vector<uint8_t> compressedBytes;
    WaypointCompressor comp;
    const uint8_t* rawBytes = reinterpret_cast<const uint8_t*>(diskPaths.data());
    size_t rawSize = diskPaths.size() * sizeof(YaPBDiskPath);

    if (!comp.Encode(rawBytes, rawSize, compressedBytes)) {
        return false;
    }

    FILE* f = std::fopen(filepath.c_str(), "wb");
    if (!f) return false;

    std::fwrite(&hdr, 1, sizeof(hdr), f);
    std::fwrite(compressedBytes.data(), 1, compressedBytes.size(), f);
    std::fclose(f);

    m_loadedPath = filepath;
    m_activeBot = BotType::YaPB;
    m_activeMod = mod;
    return true;
}

// --- POD-Bot mm (.wpt) Codec ---

#pragma pack(push, 1)
struct PODBotDiskNodeV6 {
    int32_t pathNumber;
    int32_t flags;
    Vector3 origin;
    float radius;
    float campStartX;
    float campStartY;
    float campEndX;
    float campEndY;
    int16_t index[8];
    uint16_t connectionFlags[8];
    int32_t distances[8];
};

struct PODBotDiskNodeV5 {
    int32_t flags;
    Vector3 origin;
    float radius;
    float campStartX;
    float campStartY;
    float campEndX;
    float campEndY;
    int16_t index[8];
    uint16_t connectionFlags[8];
    int32_t distances[8];
};
#pragma pack(pop)

bool WaypointGraph::LoadPODBot(const std::string& filepath) {
    FILE* f = std::fopen(filepath.c_str(), "rb");
    if (!f) return false;

    WaypointFileHeader hdr;
    if (std::fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fclose(f);
        return false;
    }

    Clear();
    m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
    m_author = std::string(hdr.author, strnlen(hdr.author, 31));
    m_activeBot = BotType::PODBot;
    m_loadedPath = filepath;

    int numPoints = hdr.pointNumber;
    if (numPoints <= 0 || numPoints > 65536) {
        std::fclose(f);
        return false;
    }

    std::fseek(f, 0, SEEK_END);
    long fileSz = std::ftell(f);
    long dataSz = fileSz - static_cast<long>(sizeof(hdr));
    std::fseek(f, sizeof(hdr), SEEK_SET);

    size_t perNodeSz = (numPoints > 0) ? (dataSz / numPoints) : 0;

    m_nodes.resize(numPoints);

    if (perNodeSz >= sizeof(EBotLegacyPathOLD)) { // 200 bytes
        std::vector<EBotLegacyPathOLD> diskPaths(numPoints);
        std::fread(diskPaths.data(), sizeof(EBotLegacyPathOLD), numPoints, f);
        for (int i = 0; i < numPoints; ++i) {
            m_nodes[i].id = static_cast<uint32_t>(i + 1);
            m_nodes[i].origin = diskPaths[i].origin;
            m_nodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            m_nodes[i].radius = diskPaths[i].radius;
            m_nodes[i].campYaw = diskPaths[i].campStartX;
            m_nodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints) ? (rawIdx + 1) : -1;
                m_nodes[i].connectionFlags[c] = diskPaths[i].connectionFlags[c];
            }
        }
    } else if (perNodeSz == sizeof(PODBotDiskNodeV6)) { // 104 bytes
        std::vector<PODBotDiskNodeV6> diskPaths(numPoints);
        std::fread(diskPaths.data(), sizeof(PODBotDiskNodeV6), numPoints, f);
        for (int i = 0; i < numPoints; ++i) {
            m_nodes[i].id = static_cast<uint32_t>(i + 1);
            m_nodes[i].origin = diskPaths[i].origin;
            m_nodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            m_nodes[i].radius = diskPaths[i].radius;
            m_nodes[i].campYaw = diskPaths[i].campStartX;
            m_nodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints) ? (rawIdx + 1) : -1;
                m_nodes[i].connectionFlags[c] = diskPaths[i].connectionFlags[c];
            }
        }
    } else { // 100 bytes or fallback
        std::vector<PODBotDiskNodeV5> diskPaths(numPoints);
        std::fread(diskPaths.data(), sizeof(PODBotDiskNodeV5), numPoints, f);
        for (int i = 0; i < numPoints; ++i) {
            m_nodes[i].id = static_cast<uint32_t>(i + 1);
            m_nodes[i].origin = diskPaths[i].origin;
            m_nodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            m_nodes[i].radius = diskPaths[i].radius;
            m_nodes[i].campYaw = diskPaths[i].campStartX;
            m_nodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints) ? (rawIdx + 1) : -1;
                m_nodes[i].connectionFlags[c] = diskPaths[i].connectionFlags[c];
            }
        }
    }

    std::fclose(f);
    m_nextId = static_cast<uint32_t>(numPoints + 1);
    return true;
}

bool WaypointGraph::SavePODBot(const std::string& filepath, GameMod mod) {
    if (m_nodes.empty()) return false;

    WaypointFileHeader hdr;
    std::memset(&hdr, 0, sizeof(hdr));
    std::memcpy(hdr.magic, "PODWAY!\0", 8);
    hdr.fileVersion = 6;
    hdr.pointNumber = static_cast<int32_t>(m_nodes.size());
    std::strncpy(hdr.mapName, m_mapName.c_str(), 31);
    std::strncpy(hdr.author, m_author.c_str(), 31);

    // Map unique runtime node IDs to zero-based contiguous file indices
    std::unordered_map<uint32_t, int16_t> idToIndex;
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        idToIndex[m_nodes[i].id] = static_cast<int16_t>(i);
    }

    std::vector<PODBotDiskNodeV6> diskPaths(m_nodes.size());
    std::memset(diskPaths.data(), 0, diskPaths.size() * sizeof(PODBotDiskNodeV6));

    for (size_t i = 0; i < m_nodes.size(); ++i) {
        diskPaths[i].pathNumber = static_cast<int32_t>(i);
        uint32_t f = m_nodes[i].flags;
        if (f & WPT_FLAG_ZMHMCAMP) f |= WPT_FLAG_CAMP;
        f &= ~(WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH | WPT_FLAG_ZOMBIEONLY | WPT_FLAG_HUMANONLY | WPT_FLAG_ZOMBIEPUSH | WPT_FLAG_HELICOPTER);

        diskPaths[i].flags = static_cast<int32_t>(f);
        diskPaths[i].origin = m_nodes[i].origin;
        diskPaths[i].radius = m_nodes[i].radius;
        diskPaths[i].campStartX = m_nodes[i].campYaw;
        diskPaths[i].campStartY = m_nodes[i].campPitch;

        for (int c = 0; c < 8; ++c) {
            int16_t target = m_nodes[i].connections[c];
            int16_t diskTarget = -1;
            if (target > 0) {
                auto it = idToIndex.find(static_cast<uint32_t>(target));
                if (it != idToIndex.end() && it->second != static_cast<int16_t>(i)) {
                    diskTarget = it->second;
                }
            }
            diskPaths[i].index[c] = diskTarget;
            diskPaths[i].connectionFlags[c] = (diskTarget >= 0) ? m_nodes[i].connectionFlags[c] : 0;
            if (diskTarget >= 0) {
                Vector3 diff = m_nodes[diskTarget].origin - m_nodes[i].origin;
                diskPaths[i].distances[c] = static_cast<int32_t>(diff.Length());
            }
        }
    }

    FILE* f = std::fopen(filepath.c_str(), "wb");
    if (!f) return false;

    std::fwrite(&hdr, 1, sizeof(hdr), f);
    std::fwrite(diskPaths.data(), sizeof(PODBotDiskNodeV6), diskPaths.size(), f);
    std::fclose(f);

    m_loadedPath = filepath;
    m_activeBot = BotType::PODBot;
    m_activeMod = mod;
    return true;
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
