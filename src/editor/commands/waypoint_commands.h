#ifndef WAYPOINT_COMMANDS_H
#define WAYPOINT_COMMANDS_H

#include "editor/commands/command.h"
#include "editor/scene/editor_scene.h"
#include "waypoint/waypoint_graph.h"
#include "waypoint/waypoint_types.h"
#include <vector>
#include <string>
#include <algorithm>
#include <memory>
#include <utility>

struct WaypointIncomingLink {
    uint32_t fromId{0};
    uint16_t flags{0};
};

// Command: Add Waypoint Node
class CmdAddWaypoint : public IEditCommand {
public:
    CmdAddWaypoint(EditorScene* scene, const WaypointNode& node, const std::vector<WaypointIncomingLink>& incomingLinks = {})
        : m_scene(scene), m_nodeData(node), m_incomingLinks(incomingLinks) {}

    void Execute() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        graph.InsertNode(m_nodeData);
        for (const auto& link : m_incomingLinks) {
            graph.ConnectNodes(link.fromId, m_nodeData.id, false, link.flags);
        }
        m_scene->SelectWaypoint(m_nodeData.id, false);
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    void Undo() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        graph.RemoveNode(m_nodeData.id);
        m_scene->ClearWaypointSelection();
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    const char* GetName() const override { return "Add Waypoint"; }
    uint32_t GetNodeID() const { return m_nodeData.id; }

private:
    EditorScene* m_scene;
    WaypointNode m_nodeData;
    std::vector<WaypointIncomingLink> m_incomingLinks;
};

// Command: Delete Waypoint Node
class CmdDeleteWaypoint : public IEditCommand {
public:
    CmdDeleteWaypoint(EditorScene* scene, uint32_t id)
        : m_scene(scene), m_id(id) {
        if (m_scene) {
            const auto* node = m_scene->GetWaypoints().GetNode(id);
            if (node) {
                m_nodeData = *node;
                m_valid = true;
                // Capture incoming connections pointing to this node
                for (const auto& other : m_scene->GetWaypoints().GetNodes()) {
                    if (other.id == id) continue;
                    for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                        if (other.connections[c] == static_cast<int16_t>(id)) {
                            m_incomingLinks.push_back({ other.id, other.connectionFlags[c] });
                        }
                    }
                }
            }
        }
    }

    void Execute() override {
        if (!m_scene || !m_valid) return;
        m_scene->GetWaypoints().RemoveNode(m_id);
        if (m_scene->GetSelectedWaypointID() == m_id) {
            m_scene->SelectWaypoint(0);
        }
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    void Undo() override {
        if (!m_scene || !m_valid) return;
        auto& graph = m_scene->GetWaypoints();
        graph.InsertNode(m_nodeData);
        for (const auto& link : m_incomingLinks) {
            graph.ConnectNodes(link.fromId, m_nodeData.id, false, link.flags);
        }
        m_scene->SelectWaypoint(m_nodeData.id, false);
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    const char* GetName() const override { return "Delete Waypoint"; }

private:
    EditorScene* m_scene;
    uint32_t m_id{0};
    WaypointNode m_nodeData;
    std::vector<WaypointIncomingLink> m_incomingLinks;
    bool m_valid{false};
};

// Command: Batch Delete Waypoints
class CmdBatchDeleteWaypoints : public IEditCommand {
public:
    CmdBatchDeleteWaypoints(EditorScene* scene, const std::vector<uint32_t>& ids)
        : m_scene(scene) {
        if (m_scene) {
            for (uint32_t id : ids) {
                const auto* node = m_scene->GetWaypoints().GetNode(id);
                if (node) {
                    m_deletedNodes.push_back(*node);
                }
            }
            // Capture external incoming connections
            for (const auto& other : m_scene->GetWaypoints().GetNodes()) {
                if (std::find(ids.begin(), ids.end(), other.id) != ids.end()) continue;
                for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                    int16_t tgt = other.connections[c];
                    if (tgt > 0 && std::find(ids.begin(), ids.end(), static_cast<uint32_t>(tgt)) != ids.end()) {
                        m_externalIncomingLinks.push_back({ other.id, static_cast<uint32_t>(tgt), other.connectionFlags[c] });
                    }
                }
            }
        }
    }

    void Execute() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        for (const auto& n : m_deletedNodes) {
            graph.RemoveNode(n.id);
        }
        m_scene->ClearWaypointSelection();
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    void Undo() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        for (const auto& n : m_deletedNodes) {
            graph.InsertNode(n);
        }
        for (const auto& link : m_externalIncomingLinks) {
            graph.ConnectNodes(link.fromId, link.toId, false, link.flags);
        }
        std::vector<uint32_t> restoredIds;
        for (const auto& n : m_deletedNodes) restoredIds.push_back(n.id);
        m_scene->BoxSelectWaypoints(restoredIds, false, false);
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    const char* GetName() const override { return "Batch Delete Waypoints"; }

private:
    struct ExternalLink {
        uint32_t fromId{0};
        uint32_t toId{0};
        uint16_t flags{0};
    };

    EditorScene* m_scene;
    std::vector<WaypointNode> m_deletedNodes;
    std::vector<ExternalLink> m_externalIncomingLinks;
};

// Command: Move Waypoint (Single)
class CmdMoveWaypoint : public IEditCommand {
public:
    CmdMoveWaypoint(EditorScene* scene, uint32_t id, const Vector3& oldPos, const Vector3& newPos)
        : m_scene(scene), m_id(id), m_oldPos(oldPos), m_newPos(newPos) {}

    void Execute() override {
        if (!m_scene) return;
        auto* node = m_scene->GetWaypoints().GetNode(m_id);
        if (node) {
            node->origin = m_newPos;
            if (m_scene->HasBSP()) m_scene->GetWaypoints().CalculateWayzone(m_id, m_scene->GetBSP());
            m_scene->SetModified(true);
            m_scene->RebuildWaypointRenderer();
        }
    }

    void Undo() override {
        if (!m_scene) return;
        auto* node = m_scene->GetWaypoints().GetNode(m_id);
        if (node) {
            node->origin = m_oldPos;
            if (m_scene->HasBSP()) m_scene->GetWaypoints().CalculateWayzone(m_id, m_scene->GetBSP());
            m_scene->SetModified(true);
            m_scene->RebuildWaypointRenderer();
        }
    }

    const char* GetName() const override { return "Move Waypoint"; }

private:
    EditorScene* m_scene;
    uint32_t m_id{0};
    Vector3 m_oldPos;
    Vector3 m_newPos;
};

// Command: Batch Move Waypoints
class CmdBatchMoveWaypoints : public IEditCommand {
public:
    struct MoveEntry {
        uint32_t id{0};
        Vector3 oldPos;
        Vector3 newPos;
    };

    CmdBatchMoveWaypoints(EditorScene* scene, const std::vector<MoveEntry>& entries)
        : m_scene(scene), m_entries(entries) {}

    void Execute() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        for (const auto& entry : m_entries) {
            auto* node = graph.GetNode(entry.id);
            if (node) {
                node->origin = entry.newPos;
                if (m_scene->HasBSP()) graph.CalculateWayzone(entry.id, m_scene->GetBSP());
            }
        }
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    void Undo() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        for (const auto& entry : m_entries) {
            auto* node = graph.GetNode(entry.id);
            if (node) {
                node->origin = entry.oldPos;
                if (m_scene->HasBSP()) graph.CalculateWayzone(entry.id, m_scene->GetBSP());
            }
        }
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    const char* GetName() const override { return "Batch Move Waypoints"; }

private:
    EditorScene* m_scene;
    std::vector<MoveEntry> m_entries;
};

// Command: Connect Waypoints
class CmdConnectWaypoints : public IEditCommand {
public:
    CmdConnectWaypoints(EditorScene* scene, uint32_t fromId, uint32_t toId, bool bidirectional, uint16_t connFlags)
        : m_scene(scene), m_fromId(fromId), m_toId(toId), m_bidirectional(bidirectional), m_connFlags(connFlags) {
        if (m_scene) {
            const auto* from = m_scene->GetWaypoints().GetNode(fromId);
            const auto* to = m_scene->GetWaypoints().GetNode(toId);
            if (from) {
                m_oldFromHadLink = from->HasConnectionTo(static_cast<int16_t>(toId));
                for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                    if (from->connections[c] == static_cast<int16_t>(toId)) {
                        m_oldFromFlags = from->connectionFlags[c];
                        break;
                    }
                }
            }
            if (to) {
                m_oldToHadLink = to->HasConnectionTo(static_cast<int16_t>(fromId));
                for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                    if (to->connections[c] == static_cast<int16_t>(fromId)) {
                        m_oldToFlags = to->connectionFlags[c];
                        break;
                    }
                }
            }
        }
    }

    void Execute() override {
        if (!m_scene) return;
        m_scene->GetWaypoints().ConnectNodes(m_fromId, m_toId, m_bidirectional, m_connFlags);
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    void Undo() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        auto* from = graph.GetNode(m_fromId);
        auto* to = graph.GetNode(m_toId);
        if (from) {
            if (m_oldFromHadLink) from->AddConnection(static_cast<int16_t>(m_toId), m_oldFromFlags);
            else from->RemoveConnection(static_cast<int16_t>(m_toId));
        }
        if (to && m_bidirectional) {
            if (m_oldToHadLink) to->AddConnection(static_cast<int16_t>(m_fromId), m_oldToFlags);
            else to->RemoveConnection(static_cast<int16_t>(m_fromId));
        }
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    const char* GetName() const override { return "Connect Waypoints"; }

private:
    EditorScene* m_scene;
    uint32_t m_fromId{0};
    uint32_t m_toId{0};
    bool m_bidirectional{true};
    uint16_t m_connFlags{0};
    bool m_oldFromHadLink{false};
    uint16_t m_oldFromFlags{0};
    bool m_oldToHadLink{false};
    uint16_t m_oldToFlags{0};
};

// Command: Disconnect Waypoints
class CmdDisconnectWaypoints : public IEditCommand {
public:
    CmdDisconnectWaypoints(EditorScene* scene, uint32_t fromId, uint32_t toId, bool bidirectional)
        : m_scene(scene), m_fromId(fromId), m_toId(toId), m_bidirectional(bidirectional) {
        if (m_scene) {
            const auto* from = m_scene->GetWaypoints().GetNode(fromId);
            const auto* to = m_scene->GetWaypoints().GetNode(toId);
            if (from) {
                m_fromHadLink = from->HasConnectionTo(static_cast<int16_t>(toId));
                for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                    if (from->connections[c] == static_cast<int16_t>(toId)) {
                        m_fromFlags = from->connectionFlags[c];
                        break;
                    }
                }
            }
            if (to) {
                m_toHadLink = to->HasConnectionTo(static_cast<int16_t>(fromId));
                for (int c = 0; c < WPT_MAX_CONNECTIONS; ++c) {
                    if (to->connections[c] == static_cast<int16_t>(fromId)) {
                        m_toFlags = to->connectionFlags[c];
                        break;
                    }
                }
            }
        }
    }

    void Execute() override {
        if (!m_scene) return;
        m_scene->GetWaypoints().DisconnectNodes(m_fromId, m_toId, m_bidirectional);
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    void Undo() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        if (m_fromHadLink) {
            graph.ConnectNodes(m_fromId, m_toId, false, m_fromFlags);
        }
        if (m_toHadLink && m_bidirectional) {
            graph.ConnectNodes(m_toId, m_fromId, false, m_toFlags);
        }
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    const char* GetName() const override { return "Disconnect Waypoints"; }

private:
    EditorScene* m_scene;
    uint32_t m_fromId{0};
    uint32_t m_toId{0};
    bool m_bidirectional{true};
    bool m_fromHadLink{false};
    uint16_t m_fromFlags{0};
    bool m_toHadLink{false};
    uint16_t m_toFlags{0};
};

// Command: Set Waypoint Properties
class CmdSetWaypointProps : public IEditCommand {
public:
    CmdSetWaypointProps(EditorScene* scene, uint32_t id,
                        uint32_t oldFlags, uint32_t newFlags,
                        float oldRadius, float newRadius,
                        uint8_t oldMesh, uint8_t newMesh,
                        float oldPitch, float newPitch,
                        float oldYaw, float newYaw)
        : m_scene(scene), m_id(id),
          m_oldFlags(oldFlags), m_newFlags(newFlags),
          m_oldRadius(oldRadius), m_newRadius(newRadius),
          m_oldMesh(oldMesh), m_newMesh(newMesh),
          m_oldPitch(oldPitch), m_newPitch(newPitch),
          m_oldYaw(oldYaw), m_newYaw(newYaw) {}

    void Execute() override {
        if (!m_scene) return;
        auto* node = m_scene->GetWaypoints().GetNode(m_id);
        if (node) {
            node->flags = m_newFlags;
            node->radius = m_newRadius;
            node->mesh = m_newMesh;
            node->campPitch = m_newPitch;
            node->campYaw = m_newYaw;
            m_scene->SetModified(true);
            m_scene->RebuildWaypointRenderer();
        }
    }

    void Undo() override {
        if (!m_scene) return;
        auto* node = m_scene->GetWaypoints().GetNode(m_id);
        if (node) {
            node->flags = m_oldFlags;
            node->radius = m_oldRadius;
            node->mesh = m_oldMesh;
            node->campPitch = m_oldPitch;
            node->campYaw = m_oldYaw;
            m_scene->SetModified(true);
            m_scene->RebuildWaypointRenderer();
        }
    }

    const char* GetName() const override { return "Set Waypoint Properties"; }

private:
    EditorScene* m_scene;
    uint32_t m_id{0};
    uint32_t m_oldFlags, m_newFlags;
    float m_oldRadius, m_newRadius;
    uint8_t m_oldMesh, m_newMesh;
    float m_oldPitch, m_newPitch;
    float m_oldYaw, m_newYaw;
};

// Command: Duplicate Waypoints
class CmdDuplicateWaypoints : public IEditCommand {
public:
    CmdDuplicateWaypoints(EditorScene* scene, const std::vector<WaypointNode>& clonedNodes)
        : m_scene(scene), m_clonedNodes(clonedNodes) {}

    void Execute() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        std::vector<uint32_t> newIds;
        for (const auto& n : m_clonedNodes) {
            graph.InsertNode(n);
            newIds.push_back(n.id);
        }
        m_scene->BoxSelectWaypoints(newIds, false, false);
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    void Undo() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        for (const auto& n : m_clonedNodes) {
            graph.RemoveNode(n.id);
        }
        m_scene->ClearWaypointSelection();
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    const char* GetName() const override { return "Duplicate Waypoint(s)"; }

private:
    EditorScene* m_scene;
    std::vector<WaypointNode> m_clonedNodes;
};

// Command: Bridge Waypoints
class CmdBridgeWaypoints : public IEditCommand {
public:
    CmdBridgeWaypoints(EditorScene* scene, uint32_t idA, uint32_t idB, const std::vector<WaypointNode>& bridgeNodes)
        : m_scene(scene), m_idA(idA), m_idB(idB), m_bridgeNodes(bridgeNodes) {}

    void Execute() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        for (const auto& bn : m_bridgeNodes) {
            graph.InsertNode(bn);
        }
        if (!m_bridgeNodes.empty()) {
            graph.ConnectNodes(m_idA, m_bridgeNodes.front().id, true, WPT_CONN_NONE);
            graph.ConnectNodes(m_bridgeNodes.back().id, m_idB, true, WPT_CONN_NONE);
        }
        std::vector<uint32_t> allBridgedIds;
        for (const auto& b : m_bridgeNodes) allBridgedIds.push_back(b.id);
        m_scene->BoxSelectWaypoints(allBridgedIds, false, false);
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    void Undo() override {
        if (!m_scene) return;
        auto& graph = m_scene->GetWaypoints();
        if (!m_bridgeNodes.empty()) {
            graph.DisconnectNodes(m_idA, m_bridgeNodes.front().id, true);
            graph.DisconnectNodes(m_bridgeNodes.back().id, m_idB, true);
        }
        for (const auto& bn : m_bridgeNodes) {
            graph.RemoveNode(bn.id);
        }
        m_scene->ClearWaypointSelection();
        m_scene->SetModified(true);
        m_scene->RebuildWaypointRenderer();
    }

    const char* GetName() const override { return "Bridge Waypoints"; }

private:
    EditorScene* m_scene;
    uint32_t m_idA{0};
    uint32_t m_idB{0};
    std::vector<WaypointNode> m_bridgeNodes;
};

#endif // WAYPOINT_COMMANDS_H
