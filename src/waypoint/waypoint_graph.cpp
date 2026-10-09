#include "waypoint/waypoint_graph.h"
#include "waypoint/compressor.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>

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

// --- High-Level Dispatch Load / Save ---

bool WaypointGraph::Load(const std::string& filepath) {
    std::string lowerPath = filepath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

    if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".ewp") == 0) {
        return LoadEBot(filepath);
    } else if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".spt") == 0) {
        return LoadSyPB(filepath);
    } else if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".pwf") == 0) {
        // Test header to distinguish EBot / SyPB / YaPB
        FILE* f = std::fopen(filepath.c_str(), "rb");
        if (f) {
            char magic[8]{0};
            std::fread(magic, 1, 8, f);
            std::fclose(f);
            if (std::strncmp(magic, "EBOTWP", 6) == 0) return LoadEBot(filepath);
            if (std::strncmp(magic, "SyPB", 4) == 0) return LoadSyPB(filepath);
            if (std::strncmp(magic, "YaPB", 4) == 0) return LoadYaPB(filepath);
        }
        return LoadYaPB(filepath);
    } else if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".wpt") == 0) {
        return LoadPODBot(filepath);
    }

    // Default fallback: Try EBot loader first (handles LZSS & backwards compatibility), then POD-Bot
    if (LoadEBot(filepath)) return true;
    return LoadPODBot(filepath);
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
struct EBotDiskPath {
    Vector3 origin;
    uint32_t flags;
    uint8_t radius;
    uint8_t mesh;
    int16_t index[8];
    uint16_t connectionFlags[8];
    float gravity;
};

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
    if (numPoints <= 0 || numPoints > 8192) {
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

        size_t expectedUncomp = numPoints * sizeof(EBotDiskPath);
        std::vector<uint8_t> uncompData(expectedUncomp);
        size_t actualUncomp = 0;

        WaypointCompressor comp;
        if (!comp.Decode(compData.data(), compData.size(), uncompData.data(), expectedUncomp, actualUncomp)) {
            return false;
        }

        const EBotDiskPath* diskPaths = reinterpret_cast<const EBotDiskPath*>(uncompData.data());
        m_nodes.resize(numPoints);
        for (int i = 0; i < numPoints; ++i) {
            m_nodes[i].id = static_cast<uint32_t>(i + 1);
            m_nodes[i].origin = diskPaths[i].origin;
            m_nodes[i].flags = diskPaths[i].flags;
            m_nodes[i].radius = static_cast<float>(diskPaths[i].radius);
            m_nodes[i].mesh = diskPaths[i].mesh;
            m_nodes[i].gravity = diskPaths[i].gravity;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints) ? (rawIdx + 1) : -1;
                m_nodes[i].connectionFlags[c] = diskPaths[i].connectionFlags[c];
            }
        }
        m_nextId = static_cast<uint32_t>(numPoints + 1);
        return true;
    } else if (hdr.fileVersion == 126) { // Uncompressed raw EBotPath
        std::vector<EBotDiskPath> diskPaths(numPoints);
        std::fread(diskPaths.data(), sizeof(EBotDiskPath), numPoints, f);
        std::fclose(f);

        m_nodes.resize(numPoints);
        for (int i = 0; i < numPoints; ++i) {
            m_nodes[i].id = static_cast<uint32_t>(i + 1);
            m_nodes[i].origin = diskPaths[i].origin;
            m_nodes[i].flags = diskPaths[i].flags;
            m_nodes[i].radius = static_cast<float>(diskPaths[i].radius);
            m_nodes[i].mesh = diskPaths[i].mesh;
            m_nodes[i].gravity = diskPaths[i].gravity;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints) ? (rawIdx + 1) : -1;
                m_nodes[i].connectionFlags[c] = diskPaths[i].connectionFlags[c];
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
                m_nodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints) ? (rawIdx + 1) : -1;
                m_nodes[i].connectionFlags[c] = diskPaths[i].connectionFlags[c];
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

    std::vector<EBotDiskPath> diskPaths(m_nodes.size());
    for (size_t i = 0; i < m_nodes.size(); ++i) {
        diskPaths[i].origin = m_nodes[i].origin;
        uint32_t f = m_nodes[i].flags;
        if (mod == GameMod::Standard) {
            // Strip zombie-only bits if targeting standard CS
            f &= ~(WPT_FLAG_ZMHMCAMP | WPT_FLAG_HMCAMPMESH | WPT_FLAG_ZOMBIEONLY | WPT_FLAG_HUMANONLY | WPT_FLAG_ZOMBIEPUSH | WPT_FLAG_HELICOPTER);
        }
        diskPaths[i].flags = f;
        diskPaths[i].radius = static_cast<uint8_t>(std::clamp(m_nodes[i].radius, 0.0f, 255.0f));
        diskPaths[i].mesh = m_nodes[i].mesh;
        diskPaths[i].gravity = m_nodes[i].gravity;

        for (int c = 0; c < 8; ++c) {
            int16_t target = m_nodes[i].connections[c];
            diskPaths[i].index[c] = (target > 0 && target <= static_cast<int16_t>(m_nodes.size())) ? (target - 1) : -1;
            diskPaths[i].connectionFlags[c] = m_nodes[i].connectionFlags[c];
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
    if (numPoints <= 0 || numPoints > 8192) {
        std::fclose(f);
        return false;
    }

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
            int16_t diskTarget = (target > 0 && target <= static_cast<int16_t>(m_nodes.size())) ? (target - 1) : -1;
            diskPaths[i].index[c] = diskTarget;
            diskPaths[i].connectionFlags[c] = m_nodes[i].connectionFlags[c];
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
    if (numPoints <= 0 || numPoints > 8192) {
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
    std::vector<uint8_t> uncompData(expectedUncomp);
    size_t actualUncomp = 0;

    WaypointCompressor comp;
    if (!comp.Decode(payload.data(), payload.size(), uncompData.data(), expectedUncomp, actualUncomp) || actualUncomp != expectedUncomp) {
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
            diskPaths[i].index[c] = (target > 0 && target <= static_cast<int16_t>(m_nodes.size())) ? (target - 1) : -1;
            diskPaths[i].connectionFlags[c] = m_nodes[i].connectionFlags[c];
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
struct PODBotDiskNode {
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
    if (numPoints <= 0 || numPoints > 8192) {
        std::fclose(f);
        return false;
    }

    std::vector<PODBotDiskNode> diskPaths(numPoints);
    std::fread(diskPaths.data(), sizeof(PODBotDiskNode), numPoints, f);
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

    std::vector<PODBotDiskNode> diskPaths(m_nodes.size());
    std::memset(diskPaths.data(), 0, diskPaths.size() * sizeof(PODBotDiskNode));

    for (size_t i = 0; i < m_nodes.size(); ++i) {
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
            int16_t diskTarget = (target > 0 && target <= static_cast<int16_t>(m_nodes.size())) ? (target - 1) : -1;
            diskPaths[i].index[c] = diskTarget;
            diskPaths[i].connectionFlags[c] = m_nodes[i].connectionFlags[c];
            if (diskTarget >= 0) {
                Vector3 diff = m_nodes[diskTarget].origin - m_nodes[i].origin;
                diskPaths[i].distances[c] = static_cast<int32_t>(diff.Length());
            }
        }
    }

    FILE* f = std::fopen(filepath.c_str(), "wb");
    if (!f) return false;

    std::fwrite(&hdr, 1, sizeof(hdr), f);
    std::fwrite(diskPaths.data(), sizeof(PODBotDiskNode), diskPaths.size(), f);
    std::fclose(f);

    m_loadedPath = filepath;
    m_activeBot = BotType::PODBot;
    m_activeMod = mod;
    return true;
}
