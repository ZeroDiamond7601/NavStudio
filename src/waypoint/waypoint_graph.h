#ifndef WAYPOINT_GRAPH_H
#define WAYPOINT_GRAPH_H

#include "waypoint/waypoint_types.h"
#include <vector>
#include <string>
#include <memory>

class WaypointGraph {
public:
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

    WaypointAnalysisStats AnalyzeGraph(const class BSPFile* bsp = nullptr, GameMod mod = GameMod::Standard);

    // E-Bot Waypoint Utilities & Optimization
    size_t DeleteOrphanNodes();
    size_t FixWaypoints(const class BSPFile* bsp = nullptr);
    void CalculateWayzone(uint32_t nodeId, const class BSPFile* bsp);
    size_t CalculateAllWayzones(const class BSPFile* bsp);
    bool ValidateNodes(std::vector<std::string>* outWarnings = nullptr);

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
