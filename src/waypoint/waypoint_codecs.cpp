#include "waypoint/waypoint_graph.h"
#include "waypoint/compressor.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <fstream>
#include <unordered_map>
#include <unordered_set>

// --- High-Level Dispatch Load / Save ---

bool WaypointGraph::Load(const std::string& filepath) {
    std::string lowerPath = filepath;
    std::transform(lowerPath.begin(), lowerPath.end(), lowerPath.begin(), ::tolower);

    // Read header first to inspect magic and version
    FILE* f = std::fopen(filepath.c_str(), "rb");
    if (!f) return false;

    WaypointFileHeader hdr;
    if (std::fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);

    // 1. Dispatch based on exact magic header signature
    if (std::strncmp(hdr.magic, "EBOT", 4) == 0) {
        if (LoadEBot(filepath)) return true;
    } else if (std::strncmp(hdr.magic, "SyPB", 4) == 0) {
        if (LoadSyPB(filepath)) return true;
    } else if (std::strncmp(hdr.magic, "YaPB", 4) == 0) {
        if (LoadYaPB(filepath)) return true;
    } else if (std::strncmp(hdr.magic, "POD", 3) == 0 || std::strncmp(hdr.magic, "PWF", 3) == 0) {
        if (LoadPODBot(filepath)) return true;
    }

    // 2. Dispatch based on file extension
    if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".ewp") == 0) {
        if (LoadEBot(filepath)) return true;
        if (LoadSyPB(filepath)) return true;
    } else if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".spt") == 0) {
        if (LoadSyPB(filepath)) return true;
        if (LoadEBot(filepath)) return true;
    } else if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".wpt") == 0) {
        if (LoadPODBot(filepath)) return true;
    } else if (lowerPath.size() >= 4 && lowerPath.compare(lowerPath.size() - 4, 4, ".pwf") == 0) {
        // .pwf files: if version <= 7 or magic matches POD, prioritize PODBot/EBot legacy parser
        if (hdr.fileVersion <= 7 || std::strncmp(hdr.magic, "POD", 3) == 0) {
            if (LoadPODBot(filepath)) return true;
            if (LoadYaPB(filepath)) return true;
            if (LoadSyPB(filepath)) return true;
        } else {
            if (LoadYaPB(filepath)) return true;
            if (LoadSyPB(filepath)) return true;
            if (LoadPODBot(filepath)) return true;
        }
    }

    // 3. Comprehensive safe fallback
    if (LoadPODBot(filepath)) return true;
    if (LoadEBot(filepath)) return true;
    if (LoadSyPB(filepath)) return true;
    if (LoadYaPB(filepath)) return true;

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

// 204-byte legacy node (EBot legacy and POD-Bot / SyPB v7 standard)
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
    uint32_t standVis;
    uint32_t crouchVis;
};
static_assert(sizeof(EBotLegacyPathOLD) == 204, "EBotLegacyPathOLD must be 204 bytes");

// 200-byte packed legacy node fallback
struct EBotLegacyPathOLD_200 {
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
static_assert(sizeof(EBotLegacyPathOLD_200) == 200, "EBotLegacyPathOLD_200 must be 200 bytes");
#pragma pack(pop)

bool WaypointGraph::LoadEBot(const std::string& filepath) {
    FILE* f = std::fopen(filepath.c_str(), "rb");
    if (!f) return false;

    WaypointFileHeader hdr;
    if (std::fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fclose(f);
        return false;
    }

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
        std::vector<uint8_t> uncompData(expectedUncomp56 + 16384);
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

        std::vector<WaypointNode> loadedNodes(numPoints);
        for (int i = 0; i < numPoints; ++i) {
            loadedNodes[i].id = static_cast<uint32_t>(i + 1);
            Vector3 org = is56 ? diskPaths56[i].origin : diskPaths54[i].origin;
            if (std::isnan(org.x) || std::isnan(org.y) || std::isnan(org.z) ||
                std::abs(org.x) > 65536.0f || std::abs(org.y) > 65536.0f || std::abs(org.z) > 65536.0f) {
                org = Vector3(0.0f, 0.0f, 0.0f);
            }
            loadedNodes[i].origin = org;
            loadedNodes[i].flags = is56 ? diskPaths56[i].flags : diskPaths54[i].flags;
            loadedNodes[i].radius = static_cast<float>(is56 ? diskPaths56[i].radius : diskPaths54[i].radius);
            loadedNodes[i].mesh = is56 ? diskPaths56[i].mesh : diskPaths54[i].mesh;
            loadedNodes[i].gravity = is56 ? diskPaths56[i].gravity : diskPaths54[i].gravity;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = is56 ? diskPaths56[i].index[c] : diskPaths54[i].index[c];
                if (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) {
                    loadedNodes[i].connections[c] = static_cast<int16_t>(rawIdx + 1);
                    loadedNodes[i].connectionFlags[c] = is56 ? diskPaths56[i].connectionFlags[c] : diskPaths54[i].connectionFlags[c];
                } else {
                    loadedNodes[i].connections[c] = -1;
                    loadedNodes[i].connectionFlags[c] = 0;
                }
            }
        }

        Clear();
        m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
        m_author = std::string(hdr.author, strnlen(hdr.author, 31));
        m_activeBot = BotType::EBot;
        m_loadedPath = filepath;
        m_nodes = std::move(loadedNodes);
        m_nextId = static_cast<uint32_t>(numPoints + 1);
        return true;
    } else if (hdr.fileVersion == 126) { // Uncompressed raw EBotPath
        std::fseek(f, 0, SEEK_END);
        long dataSz = std::ftell(f) - static_cast<long>(sizeof(hdr));
        std::fseek(f, sizeof(hdr), SEEK_SET);

        bool is56 = (dataSz >= static_cast<long>(numPoints * sizeof(EBotDiskPath)));
        std::vector<WaypointNode> loadedNodes(numPoints);

        if (is56) {
            std::vector<EBotDiskPath> diskPaths(numPoints);
            std::fread(diskPaths.data(), sizeof(EBotDiskPath), numPoints, f);
            std::fclose(f);
            for (int i = 0; i < numPoints; ++i) {
                loadedNodes[i].id = static_cast<uint32_t>(i + 1);
                loadedNodes[i].origin = diskPaths[i].origin;
                loadedNodes[i].flags = diskPaths[i].flags;
                loadedNodes[i].radius = static_cast<float>(diskPaths[i].radius);
                loadedNodes[i].mesh = diskPaths[i].mesh;
                loadedNodes[i].gravity = diskPaths[i].gravity;
                for (int c = 0; c < 8; ++c) {
                    int16_t rawIdx = diskPaths[i].index[c];
                    loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                    loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
                }
            }
        } else {
            std::vector<EBotDiskPath54> diskPaths(numPoints);
            std::fread(diskPaths.data(), sizeof(EBotDiskPath54), numPoints, f);
            std::fclose(f);
            for (int i = 0; i < numPoints; ++i) {
                loadedNodes[i].id = static_cast<uint32_t>(i + 1);
                loadedNodes[i].origin = diskPaths[i].origin;
                loadedNodes[i].flags = diskPaths[i].flags;
                loadedNodes[i].radius = static_cast<float>(diskPaths[i].radius);
                loadedNodes[i].mesh = diskPaths[i].mesh;
                loadedNodes[i].gravity = diskPaths[i].gravity;
                for (int c = 0; c < 8; ++c) {
                    int16_t rawIdx = diskPaths[i].index[c];
                    loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                    loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
                }
            }
        }

        Clear();
        m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
        m_author = std::string(hdr.author, strnlen(hdr.author, 31));
        m_activeBot = BotType::EBot;
        m_loadedPath = filepath;
        m_nodes = std::move(loadedNodes);
        m_nextId = static_cast<uint32_t>(numPoints + 1);
        return true;
    } else { // Legacy EBot format (version < 126) - only accept if magic starts with EBOT
        if (std::strncmp(hdr.magic, "EBOT", 4) != 0) {
            std::fclose(f);
            return false;
        }

        std::fseek(f, 0, SEEK_END);
        long dataSz = std::ftell(f) - static_cast<long>(sizeof(hdr));
        std::fseek(f, sizeof(hdr), SEEK_SET);

        std::vector<WaypointNode> loadedNodes(numPoints);
        if (dataSz >= static_cast<long>(numPoints * sizeof(EBotLegacyPathOLD))) {
            std::vector<EBotLegacyPathOLD> diskPaths(numPoints);
            std::fread(diskPaths.data(), sizeof(EBotLegacyPathOLD), numPoints, f);
            std::fclose(f);

            for (int i = 0; i < numPoints; ++i) {
                loadedNodes[i].id = static_cast<uint32_t>(i + 1);
                loadedNodes[i].origin = diskPaths[i].origin;
                loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
                loadedNodes[i].radius = diskPaths[i].radius;
                loadedNodes[i].campYaw = diskPaths[i].campStartX;
                loadedNodes[i].campPitch = diskPaths[i].campStartY;
                for (int c = 0; c < 8; ++c) {
                    int16_t rawIdx = diskPaths[i].index[c];
                    loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                    loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
                }
            }
        } else if (dataSz >= static_cast<long>(numPoints * sizeof(EBotLegacyPathOLD_200))) {
            std::vector<EBotLegacyPathOLD_200> diskPaths(numPoints);
            std::fread(diskPaths.data(), sizeof(EBotLegacyPathOLD_200), numPoints, f);
            std::fclose(f);

            for (int i = 0; i < numPoints; ++i) {
                loadedNodes[i].id = static_cast<uint32_t>(i + 1);
                loadedNodes[i].origin = diskPaths[i].origin;
                loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
                loadedNodes[i].radius = diskPaths[i].radius;
                loadedNodes[i].campYaw = diskPaths[i].campStartX;
                loadedNodes[i].campPitch = diskPaths[i].campStartY;
                for (int c = 0; c < 8; ++c) {
                    int16_t rawIdx = diskPaths[i].index[c];
                    loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                    loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
                }
            }
        } else {
            std::fclose(f);
            return false;
        }

        Clear();
        m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
        m_author = std::string(hdr.author, strnlen(hdr.author, 31));
        m_activeBot = BotType::EBot;
        m_loadedPath = filepath;
        m_nodes = std::move(loadedNodes);
        m_nextId = static_cast<uint32_t>(numPoints + 1);
        return true;
    }
}
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

    int numPoints = hdr.pointNumber;
    if (numPoints <= 0 || numPoints > 65536) {
        std::fclose(f);
        return false;
    }

    std::fseek(f, 0, SEEK_END);
    long fileSz = std::ftell(f);
    long dataSz = fileSz - static_cast<long>(sizeof(hdr));
    std::fseek(f, sizeof(hdr), SEEK_SET);

    if (dataSz <= 0) {
        std::fclose(f);
        return false;
    }

    size_t perNodeSz = (numPoints > 0) ? (dataSz / numPoints) : 0;
    std::vector<WaypointNode> loadedNodes(numPoints);

    if (perNodeSz >= 204 || dataSz >= static_cast<long>(numPoints * sizeof(EBotLegacyPathOLD))) {
        std::vector<EBotLegacyPathOLD> diskPaths(numPoints);
        if (std::fread(diskPaths.data(), sizeof(EBotLegacyPathOLD), numPoints, f) != static_cast<size_t>(numPoints)) {
            std::fclose(f);
            return false;
        }
        std::fclose(f);

        for (int i = 0; i < numPoints; ++i) {
            loadedNodes[i].id = static_cast<uint32_t>(i + 1);
            loadedNodes[i].origin = diskPaths[i].origin;
            loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            loadedNodes[i].radius = diskPaths[i].radius;
            loadedNodes[i].campYaw = diskPaths[i].campStartX;
            loadedNodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
            }
        }
    } else if (perNodeSz >= 200 && dataSz >= static_cast<long>(numPoints * sizeof(EBotLegacyPathOLD_200))) {
        std::vector<EBotLegacyPathOLD_200> diskPaths(numPoints);
        if (std::fread(diskPaths.data(), sizeof(EBotLegacyPathOLD_200), numPoints, f) != static_cast<size_t>(numPoints)) {
            std::fclose(f);
            return false;
        }
        std::fclose(f);

        for (int i = 0; i < numPoints; ++i) {
            loadedNodes[i].id = static_cast<uint32_t>(i + 1);
            loadedNodes[i].origin = diskPaths[i].origin;
            loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            loadedNodes[i].radius = diskPaths[i].radius;
            loadedNodes[i].campYaw = diskPaths[i].campStartX;
            loadedNodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
            }
        }
    } else if (perNodeSz >= 104 || dataSz >= static_cast<long>(numPoints * sizeof(PODBotDiskNodeV6))) {
        std::vector<PODBotDiskNodeV6> diskPaths(numPoints);
        if (std::fread(diskPaths.data(), sizeof(PODBotDiskNodeV6), numPoints, f) != static_cast<size_t>(numPoints)) {
            std::fclose(f);
            return false;
        }
        std::fclose(f);

        for (int i = 0; i < numPoints; ++i) {
            loadedNodes[i].id = static_cast<uint32_t>(i + 1);
            loadedNodes[i].origin = diskPaths[i].origin;
            loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            loadedNodes[i].radius = diskPaths[i].radius;
            loadedNodes[i].campYaw = diskPaths[i].campStartX;
            loadedNodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
            }
        }
    } else if (perNodeSz >= 100 && dataSz >= static_cast<long>(numPoints * sizeof(PODBotDiskNodeV5))) {
        std::vector<PODBotDiskNodeV5> diskPaths(numPoints);
        if (std::fread(diskPaths.data(), sizeof(PODBotDiskNodeV5), numPoints, f) != static_cast<size_t>(numPoints)) {
            std::fclose(f);
            return false;
        }
        std::fclose(f);

        for (int i = 0; i < numPoints; ++i) {
            loadedNodes[i].id = static_cast<uint32_t>(i + 1);
            loadedNodes[i].origin = diskPaths[i].origin;
            loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            loadedNodes[i].radius = diskPaths[i].radius;
            loadedNodes[i].campYaw = diskPaths[i].campStartX;
            loadedNodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
            }
        }
    } else {
        std::fclose(f);
        return false;
    }

    Clear();
    m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
    m_author = std::string(hdr.author, strnlen(hdr.author, 31));
    m_activeBot = BotType::SyPB;
    m_loadedPath = filepath;
    m_nodes = std::move(loadedNodes);
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

    bool isYaPBMagic = (std::strncmp(hdr.magic, "YaPB", 4) == 0);

    int numPoints = hdr.pointNumber;
    if (numPoints <= 0 || numPoints > 65536) {
        std::fclose(f);
        return false;
    }

    std::fseek(f, 0, SEEK_END);
    long fileSz = std::ftell(f);
    long payloadSz = fileSz - static_cast<long>(sizeof(hdr));
    std::fseek(f, sizeof(hdr), SEEK_SET);

    if (payloadSz <= 0) {
        std::fclose(f);
        return false;
    }

    std::vector<uint8_t> payload(payloadSz);
    std::fread(payload.data(), 1, payloadSz, f);
    std::fclose(f);

    size_t expectedUncomp = numPoints * sizeof(YaPBDiskPath);
    std::vector<uint8_t> uncompData(expectedUncomp + 4096);
    size_t actualUncomp = 0;

    WaypointCompressor comp;
    bool decompOk = comp.Decode(payload.data(), payload.size(), uncompData.data(), uncompData.size(), actualUncomp);
    if (!decompOk || actualUncomp < expectedUncomp) {
        // Fallback: ONLY do raw uncompressed YaPB if magic explicitly matches YaPB!
        // Never falsely claim POD-Bot or SyPB uncompressed files!
        if (isYaPBMagic && payloadSz >= static_cast<long>(expectedUncomp)) {
            std::memcpy(uncompData.data(), payload.data(), expectedUncomp);
        } else {
            return false;
        }
    }

    const YaPBDiskPath* diskPaths = reinterpret_cast<const YaPBDiskPath*>(uncompData.data());
    std::vector<WaypointNode> loadedNodes(numPoints);
    for (int i = 0; i < numPoints; ++i) {
        loadedNodes[i].id = static_cast<uint32_t>(i + 1);
        loadedNodes[i].origin = diskPaths[i].origin;
        loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
        loadedNodes[i].radius = diskPaths[i].radius;
        loadedNodes[i].campPitch = diskPaths[i].campPitch;
        loadedNodes[i].campYaw = diskPaths[i].campYaw;

        for (int c = 0; c < 8; ++c) {
            int16_t rawIdx = diskPaths[i].index[c];
            loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
            loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
        }
    }

    Clear();
    m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
    m_author = std::string(hdr.author, strnlen(hdr.author, 31));
    m_activeBot = BotType::YaPB;
    m_loadedPath = filepath;
    m_nodes = std::move(loadedNodes);
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
static_assert(sizeof(PODBotDiskNodeV6) == 104, "PODBotDiskNodeV6 must be 104 bytes");

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
static_assert(sizeof(PODBotDiskNodeV5) == 100, "PODBotDiskNodeV5 must be 100 bytes");
#pragma pack(pop)

bool WaypointGraph::LoadPODBot(const std::string& filepath) {
    FILE* f = std::fopen(filepath.c_str(), "rb");
    if (!f) return false;

    WaypointFileHeader hdr;
    if (std::fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        std::fclose(f);
        return false;
    }

    int numPoints = hdr.pointNumber;
    if (numPoints <= 0 || numPoints > 65536) {
        std::fclose(f);
        return false;
    }

    std::fseek(f, 0, SEEK_END);
    long fileSz = std::ftell(f);
    long dataSz = fileSz - static_cast<long>(sizeof(hdr));
    std::fseek(f, sizeof(hdr), SEEK_SET);

    if (dataSz <= 0) {
        std::fclose(f);
        return false;
    }

    size_t perNodeSz = (numPoints > 0) ? (dataSz / numPoints) : 0;
    std::vector<WaypointNode> loadedNodes(numPoints);

    if (hdr.fileVersion == 7 || perNodeSz >= 204 || dataSz >= static_cast<long>(numPoints * sizeof(EBotLegacyPathOLD))) {
        // Version 7: 204 bytes per node
        std::vector<EBotLegacyPathOLD> diskPaths(numPoints);
        if (std::fread(diskPaths.data(), sizeof(EBotLegacyPathOLD), numPoints, f) != static_cast<size_t>(numPoints)) {
            std::fclose(f);
            return false;
        }
        std::fclose(f);

        for (int i = 0; i < numPoints; ++i) {
            loadedNodes[i].id = static_cast<uint32_t>(i + 1);
            loadedNodes[i].origin = diskPaths[i].origin;
            loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            loadedNodes[i].radius = diskPaths[i].radius;
            loadedNodes[i].campYaw = diskPaths[i].campStartX;
            loadedNodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
            }
        }
    } else if (perNodeSz >= 200 && dataSz >= static_cast<long>(numPoints * sizeof(EBotLegacyPathOLD_200))) {
        // Legacy 200-byte format
        std::vector<EBotLegacyPathOLD_200> diskPaths(numPoints);
        if (std::fread(diskPaths.data(), sizeof(EBotLegacyPathOLD_200), numPoints, f) != static_cast<size_t>(numPoints)) {
            std::fclose(f);
            return false;
        }
        std::fclose(f);

        for (int i = 0; i < numPoints; ++i) {
            loadedNodes[i].id = static_cast<uint32_t>(i + 1);
            loadedNodes[i].origin = diskPaths[i].origin;
            loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            loadedNodes[i].radius = diskPaths[i].radius;
            loadedNodes[i].campYaw = diskPaths[i].campStartX;
            loadedNodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
            }
        }
    } else if (hdr.fileVersion == 6 || perNodeSz >= sizeof(PODBotDiskNodeV6) || dataSz >= static_cast<long>(numPoints * sizeof(PODBotDiskNodeV6))) {
        // Version 6: 104 bytes per node
        std::vector<PODBotDiskNodeV6> diskPaths(numPoints);
        if (std::fread(diskPaths.data(), sizeof(PODBotDiskNodeV6), numPoints, f) != static_cast<size_t>(numPoints)) {
            std::fclose(f);
            return false;
        }
        std::fclose(f);

        for (int i = 0; i < numPoints; ++i) {
            loadedNodes[i].id = static_cast<uint32_t>(i + 1);
            loadedNodes[i].origin = diskPaths[i].origin;
            loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            loadedNodes[i].radius = diskPaths[i].radius;
            loadedNodes[i].campYaw = diskPaths[i].campStartX;
            loadedNodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
            }
        }
    } else {
        // Version 5: 100 bytes per node
        std::vector<PODBotDiskNodeV5> diskPaths(numPoints);
        if (std::fread(diskPaths.data(), sizeof(PODBotDiskNodeV5), numPoints, f) != static_cast<size_t>(numPoints)) {
            std::fclose(f);
            return false;
        }
        std::fclose(f);

        for (int i = 0; i < numPoints; ++i) {
            loadedNodes[i].id = static_cast<uint32_t>(i + 1);
            loadedNodes[i].origin = diskPaths[i].origin;
            loadedNodes[i].flags = static_cast<uint32_t>(diskPaths[i].flags);
            loadedNodes[i].radius = diskPaths[i].radius;
            loadedNodes[i].campYaw = diskPaths[i].campStartX;
            loadedNodes[i].campPitch = diskPaths[i].campStartY;
            for (int c = 0; c < 8; ++c) {
                int16_t rawIdx = diskPaths[i].index[c];
                loadedNodes[i].connections[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? static_cast<int16_t>(rawIdx + 1) : -1;
                loadedNodes[i].connectionFlags[c] = (rawIdx >= 0 && rawIdx < numPoints && rawIdx != i) ? diskPaths[i].connectionFlags[c] : 0;
            }
        }
    }

    Clear();
    m_mapName = std::string(hdr.mapName, strnlen(hdr.mapName, 31));
    m_author = std::string(hdr.author, strnlen(hdr.author, 31));
    m_activeBot = BotType::PODBot;
    m_loadedPath = filepath;
    m_nodes = std::move(loadedNodes);
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

