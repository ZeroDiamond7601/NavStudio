#ifndef WAYPOINT_GRAPH_H
#define WAYPOINT_GRAPH_H

#include "waypoint/waypoint_types.h"
#include <vector>
#include <string>
#include <memory>
#include <functional>
#include <cstddef>
#include <cstdint>

using WaypointProgressCallback = std::function<void(float progress, const std::string& status)>;

// Waypoint Graph Optimizer Options & Results
struct WaypointOptimizeOptions {
    bool mergeOverlapping{true};
    float mergeDistance{25.0f};
    bool pruneCrossingLinks{true};
    bool pruneRedundantDiagonals{true};
    bool pruneCollinear{true};
    float collinearMaxAngle{15.0f};
    bool fixOneWayLinks{true};
    bool pruneBlockedLinks{true};
    bool pruneOrphans{true};
    bool recalculateWayzones{true};
};

struct WaypointOptimizeStats {
    size_t overlappingMerged{0};
    size_t crossingLinksPruned{0};
    size_t diagonalChordsPruned{0};
    size_t collinearPruned{0};
    size_t blockedLinksPruned{0};
    size_t oneWayLinksFixed{0};
    size_t orphansRemoved{0};
    size_t wayzonesCalculated{0};
    size_t totalModified{0};
    double durationSeconds{0.0};
};

struct WaypointParkourOptions {
    float maxJumpDist{220.0f};
    float minJumpDist{30.0f};
    float maxJumpHeight{55.0f};
    float maxDropHeight{250.0f};
    bool detectCrateClimbs{true};
    bool detectChasmLeaps{true};
    bool detectDropShortcuts{true};
    bool detectDoubleJumps{true};
};

struct WaypointParkourStats {
    size_t jumpUpsCreated{0};
    size_t gapJumpsCreated{0};
    size_t dropJumpsCreated{0};
    size_t doubleJumpsCreated{0};
    size_t totalParkourLinks{0};
    double durationSeconds{0.0};
};

struct WaypointPathStep {
    uint32_t fromId{0};
    uint32_t toId{0};
    Vector3 fromPos;
    Vector3 toPos;
    uint16_t connFlags{0};
    float distance{0.0f};
    float deltaZ{0.0f};
    bool isJump{false};
    bool isCrouch{false};
    bool isLadder{false};
    std::string warning;
};

struct WaypointPathAudit {
    bool success{false};
    float totalDistance{0.0f};
    float estimatedDurationSec{0.0f};
    std::vector<WaypointPathStep> steps;
    std::vector<std::string> warnings;
};

class WaypointGraph {
public:
    using WaypointOptimizeOptions = ::WaypointOptimizeOptions;
    using WaypointOptimizeStats = ::WaypointOptimizeStats;
    using WaypointParkourOptions = ::WaypointParkourOptions;
    using WaypointParkourStats = ::WaypointParkourStats;

    WaypointGraph();
    ~WaypointGraph() = default;

    void Clear();
    bool IsEmpty() const { return m_nodes.empty(); }
    size_t GetNodeCount() const { return m_nodes.size(); }

    const std::vector<WaypointNode>& GetNodes() const { return m_nodes; }
    std::vector<WaypointNode>& GetNodes() { return m_nodes; }

    const WaypointNode* GetNodeByID(uint32_t id) const;
    WaypointNode* GetNodeByID(uint32_t id);
    const WaypointNode* GetNode(uint32_t id) const { return GetNodeByID(id); }
    WaypointNode* GetNode(uint32_t id) { return GetNodeByID(id); }

    // Node Operations
    WaypointNode* AddNode(const Vector3& origin, uint32_t flags = WPT_FLAG_NONE, float radius = 16.0f);
    bool RemoveNode(uint32_t id);
    bool DeleteNode(uint32_t id) { return RemoveNode(id); }
    int FindNearestNode(const Vector3& pos, float maxDist = 500.0f) const;

    // Link Operations
    bool ConnectNodes(uint32_t fromId, uint32_t toId, bool bidirectional = true, uint16_t connFlags = WPT_CONN_NONE);
    bool DisconnectNodes(uint32_t fromId, uint32_t toId, bool bidirectional = true);
    bool AddConnection(uint32_t fromId, uint32_t toId, uint16_t connFlags = WPT_CONN_NONE, bool bidirectional = true) {
        return ConnectNodes(fromId, toId, bidirectional, connFlags);
    }
    bool RemoveConnection(uint32_t fromId, uint32_t toId, bool bidirectional = true) {
        return DisconnectNodes(fromId, toId, bidirectional);
    }

    // Mod and Bot Profile
    BotType GetActiveBot() const { return m_activeBot; }
    void SetActiveBot(BotType bot) { m_activeBot = bot; }

    GameMod GetActiveMod() const { return m_activeMod; }
    void SetActiveMod(GameMod mod) { m_activeMod = mod; }

    const std::string& GetMapName() const { return m_mapName; }
    void SetMapName(const std::string& name) { m_mapName = name; }

    const std::string& GetAuthor() const { return m_author; }
    void SetAuthor(const std::string& author) { m_author = author; }

    const std::string& GetLoadedPath() const { return m_loadedPath; }

    // IO Operations
    bool Load(const std::string& filepath);
    bool Save(const std::string& filepath, BotType bot, GameMod mod);

    // Codecs per format
    bool LoadEBot(const std::string& filepath);
    bool SaveEBot(const std::string& filepath, GameMod mod);

    bool LoadSyPB(const std::string& filepath);
    bool SaveSyPB(const std::string& filepath, GameMod mod);

    bool LoadYaPB(const std::string& filepath);
    bool SaveYaPB(const std::string& filepath, GameMod mod);

    bool LoadPODBot(const std::string& filepath);
    bool SavePODBot(const std::string& filepath, GameMod mod);

    // Auto-Link Tool (creates links between nodes with clear line of sight and within reach)
    size_t AutoLinkNodes(float maxDist = 200.0f, const class BSPFile* bsp = nullptr);

    // Automated Waypoint Analyzer (integrating CS-EBOT NavMesh-like analysis & YaPB graph optimization)
    struct WaypointAnalysisStats {
        size_t totalScanned{0};
        size_t crouchAssigned{0};
        size_t jumpAssigned{0};
        size_t fallRiskAssigned{0};
        size_t campAnglesCalculated{0};
        size_t zombieCampsAssigned{0};
        size_t blockedLinksPruned{0};
        size_t totalModified{0};
    };

    WaypointAnalysisStats AnalyzeGraph(const class BSPFile* bsp = nullptr, GameMod mod = GameMod::Standard, WaypointProgressCallback progressCb = nullptr);

    // E-Bot Waypoint Utilities & Optimization
    size_t DeleteOrphanNodes();
    size_t FixWaypoints(const class BSPFile* bsp = nullptr);
    void CalculateWayzone(uint32_t nodeId, const class BSPFile* bsp);
    size_t CalculateAllWayzones(const class BSPFile* bsp, WaypointProgressCallback progressCb = nullptr);
    bool ValidateNodes(std::vector<std::string>* outWarnings = nullptr);

    WaypointOptimizeStats OptimizeGraph(const class BSPFile* bsp = nullptr, const WaypointOptimizeOptions& options = WaypointOptimizeOptions(), WaypointProgressCallback progressCb = nullptr);
    WaypointParkourStats GenerateParkour(const class BSPFile* bsp = nullptr, const WaypointParkourOptions& options = WaypointParkourOptions(), WaypointProgressCallback progressCb = nullptr);

    // Pathfinding & Traversal Audit
    bool FindPath(uint32_t startId, uint32_t goalId, std::vector<uint32_t>& outPath, float* outTotalCost = nullptr) const;
    WaypointPathAudit AuditPath(const std::vector<uint32_t>& path, const class BSPFile* bsp = nullptr) const;

private:
    std::vector<WaypointNode> m_nodes;
    uint32_t m_nextId{1};

    BotType m_activeBot{BotType::EBot};
    GameMod m_activeMod{GameMod::Standard};

    std::string m_mapName{"unknown"};
    std::string m_author{"NavStudio"};
    std::string m_loadedPath;
};

#endif // WAYPOINT_GRAPH_H
