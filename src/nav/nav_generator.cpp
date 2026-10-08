#include "nav_generator.h"
#include "../bsp/bsp_file.h"
#include "nav_file.h"
#include "nav_area.h"

#include <iostream>
#include <vector>
#include <queue>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <filesystem>

namespace fs = std::filesystem;

namespace {

// Internal node structure used during flood fill sampling
struct NavGenNode {
    uint32_t id;
    Vector3 pos;
    Vector3 normal;
    uint8_t attributes;
    bool isCovered;
    NavGenNode* to[NUM_NAV_DIRECTIONS];
    NavArea* area;

    NavGenNode(uint32_t _id, const Vector3& _pos, const Vector3& _normal)
        : id(_id), pos(_pos), normal(_normal), attributes(0), isCovered(false), area(nullptr) {
        for (int i = 0; i < NUM_NAV_DIRECTIONS; ++i) {
            to[i] = nullptr;
        }
    }

    bool IsBiLinked(NavDirType dir) const {
        if (!to[dir]) return false;
        NavDirType opp = static_cast<NavDirType>((dir + 2) % 4);
        return to[dir]->to[opp] == this;
    }

    bool IsClosedCell() const {
        return IsBiLinked(NAV_DIR_SOUTH) && IsBiLinked(NAV_DIR_EAST) &&
               to[NAV_DIR_EAST]->IsBiLinked(NAV_DIR_SOUTH) &&
               to[NAV_DIR_SOUTH]->IsBiLinked(NAV_DIR_EAST) &&
               to[NAV_DIR_EAST]->to[NAV_DIR_SOUTH] == to[NAV_DIR_SOUTH]->to[NAV_DIR_EAST];
    }
};

struct GridKey {
    int32_t x;
    int32_t y;

    bool operator==(const GridKey& other) const {
        return x == other.x && y == other.y;
    }
};

struct GridKeyHash {
    size_t operator()(const GridKey& k) const {
        size_t h1 = std::hash<int32_t>{}(k.x);
        size_t h2 = std::hash<int32_t>{}(k.y);
        return h1 ^ (h2 + 0x9e3779b9U + (h1 << 6) + (h1 >> 2));
    }
};

class NodeMap {
public:
    explicit NodeMap(float stepSize) : m_stepSize(stepSize), m_nextId(1) {}

    ~NodeMap() {
        for (NavGenNode* n : m_allNodes) {
            delete n;
        }
        m_allNodes.clear();
    }

    NavGenNode* FindNode(const Vector3& pos, float zTolerance = 18.0f) const {
        int32_t gx = static_cast<int32_t>(std::round(pos.x / m_stepSize));
        int32_t gy = static_cast<int32_t>(std::round(pos.y / m_stepSize));
        GridKey key{gx, gy};

        auto it = m_grid.find(key);
        if (it != m_grid.end()) {
            for (NavGenNode* n : it->second) {
                if (std::fabs(n->pos.z - pos.z) <= zTolerance) {
                    return n;
                }
            }
        }
        return nullptr;
    }

    NavGenNode* CreateNode(const Vector3& pos, const Vector3& normal) {
        int32_t gx = static_cast<int32_t>(std::round(pos.x / m_stepSize));
        int32_t gy = static_cast<int32_t>(std::round(pos.y / m_stepSize));
        GridKey key{gx, gy};

        NavGenNode* node = new NavGenNode(m_nextId++, pos, normal);
        m_allNodes.push_back(node);
        m_grid[key].push_back(node);
        return node;
    }

    const std::vector<NavGenNode*>& GetAllNodes() const { return m_allNodes; }

private:
    float m_stepSize;
    uint32_t m_nextId;
    std::unordered_map<GridKey, std::vector<NavGenNode*>, GridKeyHash> m_grid;
    std::vector<NavGenNode*> m_allNodes;
};

// Check if a rectangular block of nodes is valid for area creation
bool TestArea(NavGenNode* node, int width, int height) {
    if (!node || node->isCovered) return false;

    Vector3 normal = node->normal;
    float planeD = -normal.Dot(node->pos);
    const float offPlaneTolerance = 8.0f;

    NavGenNode* vertNode = node;
    for (int y = 0; y < height; ++y) {
        NavGenNode* horizNode = vertNode;
        for (int x = 0; x < width; ++x) {
            if (horizNode->attributes != node->attributes) return false;
            if (horizNode->isCovered) return false;
            if (!horizNode->IsClosedCell()) return false;

            horizNode = horizNode->to[NAV_DIR_EAST];
            if (!horizNode) return false;

            if (width > 1 || height > 1) {
                float dist = std::fabs(normal.Dot(horizNode->pos) + planeD);
                if (dist > offPlaneTolerance) return false;
            }
        }

        vertNode = vertNode->to[NAV_DIR_SOUTH];
        if (!vertNode) return false;

        if (width > 1 || height > 1) {
            float dist = std::fabs(normal.Dot(vertNode->pos) + planeD);
            if (dist > offPlaneTolerance) return false;
        }
    }

    // Check planarity of southern edge
    if (width > 1 || height > 1) {
        NavGenNode* horizNode = vertNode;
        for (int x = 0; x < width; ++x) {
            horizNode = horizNode->to[NAV_DIR_EAST];
            if (!horizNode) return false;

            float dist = std::fabs(normal.Dot(horizNode->pos) + planeD);
            if (dist > offPlaneTolerance) return false;
        }
    }

    return true;
}

// Build a rectangular NavArea covering [width x height] cells
NavArea* BuildArea(NavGenNode* node, int width, int height, NavMesh& outNav) {
    NavGenNode* nwNode = node;
    NavGenNode* neNode = nullptr;
    NavGenNode* swNode = nullptr;
    NavGenNode* seNode = nullptr;

    NavGenNode* vertNode = node;
    for (int y = 0; y < height; ++y) {
        NavGenNode* horizNode = vertNode;
        for (int x = 0; x < width; ++x) {
            horizNode->isCovered = true;
            horizNode = horizNode->to[NAV_DIR_EAST];
        }

        if (y == 0) {
            neNode = horizNode;
        }
        vertNode = vertNode->to[NAV_DIR_SOUTH];
    }

    swNode = vertNode;
    NavGenNode* horizNode = vertNode;
    for (int x = 0; x < width; ++x) {
        horizNode = horizNode->to[NAV_DIR_EAST];
    }
    seNode = horizNode;

    if (!nwNode || !neNode || !swNode || !seNode) return nullptr;

    NavExtent extent;
    extent.lo.x = nwNode->pos.x;
    extent.lo.y = nwNode->pos.y;
    extent.hi.x = seNode->pos.x;
    extent.hi.y = seNode->pos.y;

    extent.lo.z = nwNode->pos.z;
    extent.hi.z = seNode->pos.z;

    NavArea* area = outNav.CreateArea(extent, neNode->pos.z, swNode->pos.z);
    if (!area) return nullptr;

    area->SetAttributes(node->attributes);

    // Associate nodes with the new area
    vertNode = nwNode;
    for (int y = 0; y <= height; ++y) {
        NavGenNode* h = vertNode;
        for (int x = 0; x <= width; ++x) {
            if (h && !h->area) {
                h->area = area;
            }
            if (h) h = h->to[NAV_DIR_EAST];
        }
        if (vertNode) vertNode = vertNode->to[NAV_DIR_SOUTH];
    }

    return area;
}

} // anonymous namespace

NavGenerateResult NavGenerator::Generate(
    const BSPFile& bsp,
    NavMesh& outNav,
    const NavGenerateOptions& options,
    NavGenerateProgressCallback progress
) {
    NavGenerateResult result;
    auto startTime = std::chrono::high_resolution_clock::now();

    if (!bsp.IsLoaded()) {
        result.success = false;
        result.errorMessage = "BSP map is not loaded.";
        return result;
    }

    outNav.Unload();
    outNav.SetLoaded(true);
    outNav.SetVersion(NAV_VERSION_5);
    outNav.SetBspSize(static_cast<uint32_t>(bsp.GetFileSize()));

    if (progress) progress(0.05f, "Discovering player spawn seeds...");

    // 1. Gather Seeds
    std::vector<Vector3> seedCandidates;
    const std::vector<std::string> seedClassnames = {
        "info_player_start",
        "info_player_deathmatch",
        "info_vip_start",
        "hostage_entity",
        "armoury_entity",
        "func_bomb_target",
        "info_bomb_target"
    };

    for (const auto& cls : seedClassnames) {
        auto ents = bsp.FindEntities(cls);
        for (const BSPEntity* ent : ents) {
            Vector3 origin;
            if (ent->GetOrigin(origin)) {
                seedCandidates.push_back(origin);
            }
        }
    }

    // Ladders as seeds
    auto ladders = bsp.FindEntities("func_ladder");
    for (const BSPEntity* ent : ladders) {
        std::string modelStr = ent->GetString("model");
        if (!modelStr.empty() && modelStr[0] == '*') {
            int modelIdx = std::atoi(modelStr.c_str() + 1);
            const dmodel_t* mod = bsp.GetModel(modelIdx);
            if (mod) {
                Vector3 mid((mod->mins.x + mod->maxs.x) * 0.5f,
                            (mod->mins.y + mod->maxs.y) * 0.5f,
                            mod->mins.z + 10.0f);
                seedCandidates.push_back(mid);
            }
        }
    }

    // Fallback: If no entity seeds found, sample flat upward BSP faces
    if (seedCandidates.empty()) {
        for (int i = 0; i < bsp.GetFaceCount(); ++i) {
            const dface_t* face = bsp.GetFace(i);
            if (!face || face->planenum < 0 || face->planenum >= bsp.GetPlaneCount()) continue;
            const dplane_t* plane = bsp.GetPlane(face->planenum);
            if (plane && plane->normal.z >= options.maxSlopeNormalZ) {
                Vector3 poly[32];
                int vcount = bsp.GetFacePolygon(i, poly, 32);
                if (vcount >= 3) {
                    Vector3 centroid(0, 0, 0);
                    for (int v = 0; v < vcount; ++v) {
                        centroid = centroid + poly[v];
                    }
                    centroid = centroid * (1.0f / static_cast<float>(vcount));
                    seedCandidates.push_back(centroid + Vector3(0, 0, 16.0f));
                }
            }
        }
    }

    if (progress) progress(0.15f, "Snapping seeds to walkability grid...");

    NodeMap nodeMap(options.stepSize);
    std::queue<NavGenNode*> openQueue;

    for (const Vector3& cand : seedCandidates) {
        float sx = std::round(cand.x / options.stepSize) * options.stepSize;
        float sy = std::round(cand.y / options.stepSize) * options.stepSize;

        Vector3 groundPos;
        BSPTraceResult groundTr;
        Vector3 gStart(sx, sy, cand.z + 36.0f);
        Vector3 gEnd(sx, sy, cand.z - 500.0f);
        if (bsp.TraceWorld(gStart, gEnd, HULL_POINT, &groundTr) && groundTr.fraction < 1.0f && !groundTr.startsolid && !groundTr.allsolid) {
            groundPos = groundTr.endpos;
            if (!nodeMap.FindNode(groundPos, 18.0f)) {
                NavGenNode* seedNode = nodeMap.CreateNode(groundPos, groundTr.planeNormal);
                openQueue.push(seedNode);
            }
        } else if (bsp.GetGround(Vector3(sx, sy, cand.z + 18.0f), &groundPos, 500.0f, HULL_POINT)) {
            if (!nodeMap.FindNode(groundPos, 18.0f)) {
                NavGenNode* seedNode = nodeMap.CreateNode(groundPos, Vector3(0, 0, 1));
                openQueue.push(seedNode);
            }
        }
    }

    if (openQueue.empty()) {
        result.success = false;
        result.errorMessage = "Failed to establish walkable seed nodes on map geometry.";
        return result;
    }

    if (progress) progress(0.25f, "Sampling walkable geometry across map...");

    // 2. BFS Flood Fill
    size_t exploredCount = 0;
    while (!openQueue.empty()) {
        NavGenNode* curr = openQueue.front();
        openQueue.pop();
        exploredCount++;

        if (exploredCount % 1000 == 0 && progress) {
            progress(0.25f + 0.35f * (std::min(1.0f, static_cast<float>(exploredCount) / 10000.0f)),
                     "Sampled " + std::to_string(nodeMap.GetAllNodes().size()) + " navigation nodes...");
        }

        for (int dir = 0; dir < NUM_NAV_DIRECTIONS; ++dir) {
            if (curr->to[dir]) continue;

            float targetX = curr->pos.x;
            float targetY = curr->pos.y;

            switch (dir) {
                case NAV_DIR_NORTH: targetY -= options.stepSize; break;
                case NAV_DIR_EAST:  targetX += options.stepSize; break;
                case NAV_DIR_SOUTH: targetY += options.stepSize; break;
                case NAV_DIR_WEST:  targetX -= options.stepSize; break;
            }

            // Downward floor trace using HULL_POINT for exact floor elevation
            Vector3 gStart(targetX, targetY, curr->pos.z + options.maxJumpHeight + 4.0f);
            Vector3 gEnd(targetX, targetY, curr->pos.z - options.maxDrop);

            BSPTraceResult groundTr;
            if (!bsp.TraceWorld(gStart, gEnd, HULL_POINT, &groundTr) || groundTr.fraction >= 1.0f || groundTr.startsolid || groundTr.allsolid) {
                continue;
            }

            Vector3 groundPos = groundTr.endpos;
            float dz = groundPos.z - curr->pos.z;
            if (dz > options.maxJumpHeight || dz < -options.maxDrop) {
                continue;
            }

            // Slope check
            if (groundTr.planeNormal.z < options.maxSlopeNormalZ) {
                continue;
            }

            // Check if surface is water/fluid
            bool isFluid = false;
            bsp.GetTextureFlags(groundTr.hitFace >= 0 ? groundTr.hitFace : 0, nullptr, &isFluid, nullptr, nullptr);
            if (isFluid || (groundTr.hitTexture[0] == '!')) {
                // Skip liquid hazards
                continue;
            }

            // Vertical headroom clearance from floor upward
            BSPTraceResult trHead;
            bsp.TraceWorld(groundPos + Vector3(0, 0, 2.0f), groundPos + Vector3(0, 0, options.humanHeight + 10.0f), HULL_POINT, &trHead);
            float clearance = (trHead.fraction < 1.0f && !trHead.startsolid && !trHead.allsolid)
                                ? (trHead.endpos.z - groundPos.z)
                                : (options.humanHeight + 10.0f);

            // Reject if space cannot fit even a crouching player
            if (clearance < options.crouchHeight - 2.0f) {
                continue;
            }

            bool isCrouch = (clearance < options.humanHeight - 6.0f);

            // Traversal check from curr to groundPos
            float stepZ = std::max(curr->pos.z, groundPos.z);
            bool traversalClear = false;

            // Try standing traversal first if both current and target nodes have standing headroom
            if (!isCrouch && (curr->attributes & NAV_ATTR_CROUCH) == 0) {
                // Standing hull: center at stepZ + 38.0f (+2 units above floor to avoid floor brush clipping)
                BSPTraceResult trStand;
                Vector3 sStart(curr->pos.x, curr->pos.y, stepZ + 38.0f);
                Vector3 sEnd(groundPos.x, groundPos.y, stepZ + 38.0f);
                bsp.TraceWorld(sStart, sEnd, HULL_HUMAN, &trStand);
                if (trStand.fraction >= 1.0f && !trStand.startsolid && !trStand.allsolid) {
                    traversalClear = true;
                }
            }

            // If standing traversal failed or if low clearance is present, test crouch traversal
            if (!traversalClear && options.generateCrouch && clearance >= options.crouchHeight - 2.0f) {
                // Crouching hull: center at stepZ + 20.0f (+2 units above floor)
                BSPTraceResult trCrouch;
                Vector3 cStart(curr->pos.x, curr->pos.y, stepZ + 20.0f);
                Vector3 cEnd(groundPos.x, groundPos.y, stepZ + 20.0f);
                bsp.TraceWorld(cStart, cEnd, HULL_HEAD, &trCrouch);
                if (trCrouch.fraction >= 1.0f && !trCrouch.startsolid && !trCrouch.allsolid) {
                    traversalClear = true;
                    isCrouch = true;
                }
            }

            // Fallback: If hull swept trace clipped a minor step or doorway bevel, check feet and waist clearance rays
            if (!traversalClear) {
                BSPTraceResult trFeet, trWaist;
                bsp.TraceWorld(curr->pos + Vector3(0, 0, 18.0f), groundPos + Vector3(0, 0, 18.0f), HULL_POINT, &trFeet);
                bsp.TraceWorld(curr->pos + Vector3(0, 0, 38.0f), groundPos + Vector3(0, 0, 38.0f), HULL_POINT, &trWaist);
                if (trFeet.fraction >= 1.0f && trWaist.fraction >= 1.0f && !trFeet.startsolid && !trWaist.startsolid) {
                    traversalClear = true;
                }
            }

            if (!traversalClear) {
                continue;
            }

            uint8_t attributes = 0;
            if (options.generateCrouch && isCrouch) {
                attributes |= NAV_ATTR_CROUCH;
            }

            NavGenNode* neighbor = nodeMap.FindNode(groundPos, 18.0f);
            if (neighbor) {
                curr->to[dir] = neighbor;
                if (std::fabs(dz) <= options.maxStepHeight) {
                    NavDirType opp = static_cast<NavDirType>((dir + 2) % 4);
                    neighbor->to[opp] = curr;
                }
            } else {
                NavGenNode* newNode = nodeMap.CreateNode(groundPos, groundTr.planeNormal);
                newNode->attributes = attributes;
                curr->to[dir] = newNode;

                if (std::fabs(dz) <= options.maxStepHeight) {
                    NavDirType opp = static_cast<NavDirType>((dir + 2) % 4);
                    newNode->to[opp] = curr;
                }
                openQueue.push(newNode);
            }
        }
    }

    if (progress) progress(0.65f, "Forming navigation areas from node graph...");

    // 3. Area Formation (Greedy rectangular coverage)
    int tryWidth = 30;
    int tryHeight = 30;

    while (tryWidth > 0 && tryHeight > 0) {
        for (NavGenNode* node : nodeMap.GetAllNodes()) {
            if (node->isCovered) continue;

            if (TestArea(node, tryWidth, tryHeight)) {
                BuildArea(node, tryWidth, tryHeight, outNav);
            }
        }

        if (tryWidth >= tryHeight) {
            tryWidth--;
        } else {
            tryHeight--;
        }
    }

    // Residual 1x1 coverage for remaining connected cells
    for (NavGenNode* node : nodeMap.GetAllNodes()) {
        if (!node->isCovered && node->IsClosedCell()) {
            BuildArea(node, 1, 1, outNav);
        }
    }

    if (progress) progress(0.80f, "Connecting navigation areas...");

    // 4. Connect Areas
    size_t connectionCount = 0;
    for (NavArea* area : outNav.GetAreas()) {
        if (!area) continue;

        // Trace nodes inside or along area extent
        float minX = area->GetExtent().lo.x;
        float maxX = area->GetExtent().hi.x;
        float minY = area->GetExtent().lo.y;
        float maxY = area->GetExtent().hi.y;

        for (NavGenNode* n : nodeMap.GetAllNodes()) {
            if (!n || n->area != area) continue;

            for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
                NavGenNode* adj = n->to[d];
                if (adj && adj->area && adj->area != area) {
                    if (!area->IsConnected(adj->area, d)) {
                        area->ConnectTo(adj->area, static_cast<NavDirType>(d));
                        connectionCount++;
                    }
                }
            }
        }

        // Jump down drops along edges
        if (options.generateJumpConnections) {
            for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
                if (area->GetAdjacentCount(static_cast<NavDirType>(d)) == 0) {
                    Vector3 testStart = area->GetCenter();
                    switch (d) {
                        case NAV_DIR_NORTH: testStart.y = minY - options.stepSize * 0.5f; break;
                        case NAV_DIR_EAST:  testStart.x = maxX + options.stepSize * 0.5f; break;
                        case NAV_DIR_SOUTH: testStart.y = maxY + options.stepSize * 0.5f; break;
                        case NAV_DIR_WEST:  testStart.x = minX - options.stepSize * 0.5f; break;
                    }

                    Vector3 dropGround;
                    if (bsp.GetGround(testStart + Vector3(0, 0, 10.0f), &dropGround, options.maxDrop)) {
                        float dropDelta = area->GetCenter().z - dropGround.z;
                        if (dropDelta > options.maxStepHeight && dropDelta <= options.maxDrop) {
                            NavArea* dropArea = outNav.GetNearestArea(dropGround, options.stepSize * 1.5f);
                            if (dropArea && dropArea != area && !area->IsConnected(dropArea, d)) {
                                area->ConnectTo(dropArea, static_cast<NavDirType>(d));
                                connectionCount++;
                            }
                        }
                    }
                }
            }
        }
    }

    // 5. Merge Adjacent Rectangles
    if (options.mergeAreas) {
        if (progress) progress(0.88f, "Merging coplanar navigation areas...");

        bool mergedAny = false;
        do {
            mergedAny = false;
            const auto& areaList = outNav.GetAreas();
            for (size_t i = 0; i < areaList.size(); ++i) {
                NavArea* areaA = areaList[i];
                if (!areaA) continue;

                for (size_t j = i + 1; j < areaList.size(); ++j) {
                    NavArea* areaB = areaList[j];
                    if (!areaB) continue;

                    if (areaA->GetAttributes() != areaB->GetAttributes()) continue;

                    const auto& extA = areaA->GetExtent();
                    const auto& extB = areaB->GetExtent();

                    const float kEps = 1.0f;
                    // North/South alignment merge
                    if (std::fabs(extA.lo.x - extB.lo.x) < kEps && std::fabs(extA.hi.x - extB.hi.x) < kEps) {
                        if (std::fabs(extA.hi.y - extB.lo.y) < kEps &&
                            std::fabs(areaA->GetSWZ() - extB.lo.z) < 4.0f &&
                            std::fabs(extA.hi.z - areaB->GetNEZ()) < 4.0f) {
                            // Merge B into A vertically (B is South of A)
                            NavExtent newExt = extA;
                            newExt.hi.y = extB.hi.y;
                            newExt.hi.z = extB.hi.z;
                            areaA->SetExtent(newExt);
                            areaA->SetCornerHeights(areaA->GetNEZ(), areaB->GetSWZ());

                            // Transfer outgoing connections
                            for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
                                for (const auto& conn : areaB->GetAdjacentList(static_cast<NavDirType>(d))) {
                                    if (conn.area && conn.area != areaA && !areaA->IsConnected(conn.area, d)) {
                                        areaA->ConnectTo(conn.area, static_cast<NavDirType>(d));
                                    }
                                }
                            }
                            // Redirect incoming connections
                            for (NavArea* other : outNav.GetAreas()) {
                                if (!other || other == areaA || other == areaB) continue;
                                for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
                                    if (other->IsConnected(areaB, d)) {
                                        other->Disconnect(areaB);
                                        if (!other->IsConnected(areaA, d)) {
                                            other->ConnectTo(areaA, static_cast<NavDirType>(d));
                                        }
                                    }
                                }
                            }
                            outNav.RemoveArea(areaB->GetID());
                            mergedAny = true;
                            break;
                        }
                    }

                    // East/West alignment merge
                    if (std::fabs(extA.lo.y - extB.lo.y) < kEps && std::fabs(extA.hi.y - extB.hi.y) < kEps) {
                        if (std::fabs(extA.hi.x - extB.lo.x) < kEps &&
                            std::fabs(areaA->GetNEZ() - extB.lo.z) < 4.0f &&
                            std::fabs(extA.hi.z - areaB->GetSWZ()) < 4.0f) {
                            // Merge B into A horizontally (B is East of A)
                            NavExtent newExt = extA;
                            newExt.hi.x = extB.hi.x;
                            newExt.hi.z = extB.hi.z;
                            areaA->SetExtent(newExt);
                            areaA->SetCornerHeights(areaB->GetNEZ(), areaA->GetSWZ());

                            // Transfer outgoing connections
                            for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
                                for (const auto& conn : areaB->GetAdjacentList(static_cast<NavDirType>(d))) {
                                    if (conn.area && conn.area != areaA && !areaA->IsConnected(conn.area, d)) {
                                        areaA->ConnectTo(conn.area, static_cast<NavDirType>(d));
                                    }
                                }
                            }
                            // Redirect incoming connections
                            for (NavArea* other : outNav.GetAreas()) {
                                if (!other || other == areaA || other == areaB) continue;
                                for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
                                    if (other->IsConnected(areaB, d)) {
                                        other->Disconnect(areaB);
                                        if (!other->IsConnected(areaA, d)) {
                                            other->ConnectTo(areaA, static_cast<NavDirType>(d));
                                        }
                                    }
                                }
                            }
                            outNav.RemoveArea(areaB->GetID());
                            mergedAny = true;
                            break;
                        }
                    }
                }
                if (mergedAny) break;
            }
        } while (mergedAny);
    }

    if (progress) progress(0.95f, "Linking ladders and finalizing spatial grid...");

    // 6. Build Ladders
    if (options.generateLadders) {
        outNav.BuildLadders(&bsp);
    }

    // 7. Finalize Grid
    outNav.RebuildGrid();

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;

    result.success = (outNav.GetAreaCount() > 0);
    result.areasGenerated = outNav.GetAreaCount();
    result.connectionsCreated = connectionCount;
    result.laddersLinked = outNav.GetLadders().size();
    result.durationSeconds = elapsed.count();

    if (progress) progress(1.0f, "NavMesh generation complete.");
    return result;
}

NavGenerateResult NavGenerator::GenerateToFile(
    const std::string& bspPath,
    const std::string& navPath,
    const NavGenerateOptions& options,
    NavGenerateProgressCallback progress
) {
    NavGenerateResult result;
    BSPFile bsp;

    if (progress) progress(0.02f, "Loading BSP: " + bspPath);
    if (!bsp.Load(bspPath)) {
        result.success = false;
        result.errorMessage = "Failed to load BSP file: " + bspPath;
        return result;
    }

    NavMesh nav;
    result = Generate(bsp, nav, options, progress);

    if (result.success) {
        if (progress) progress(0.98f, "Saving NAV to: " + navPath);
        if (!nav.Save(navPath)) {
            result.success = false;
            result.errorMessage = "Failed to write NAV file: " + navPath;
            return result;
        }
    }

    return result;
}

size_t NavGenerator::FloodFillFromSeed(
    const BSPFile& bsp,
    NavMesh& nav,
    const Vector3& seedPos,
    const NavGenerateOptions& options,
    size_t maxNodes,
    std::vector<uint32_t>* outCreatedAreaIds
) {
    if (!bsp.IsLoaded()) return 0;

    // Snapshot pre-existing area extents to avoid overlapping them during fill
    struct ExistingArea { NavExtent ext; float centerZ; };
    std::vector<ExistingArea> existingAreas;
    existingAreas.reserve(nav.GetAreaCount());
    for (const NavArea* a : nav.GetAreas()) {
        if (a) existingAreas.push_back({ a->GetExtent(), a->GetCenter().z });
    }

    float step = options.stepSize;
    float sx = std::round(seedPos.x / step) * step;
    float sy = std::round(seedPos.y / step) * step;

    Vector3 groundPos;
    BSPTraceResult groundTr;
    Vector3 gStart(sx, sy, seedPos.z + 24.0f);
    Vector3 gEnd(sx, sy, seedPos.z - 200.0f);
    if (!bsp.TraceWorld(gStart, gEnd, HULL_POINT, &groundTr) || groundTr.fraction >= 1.0f || groundTr.startsolid || groundTr.allsolid) {
        if (!bsp.GetGround(Vector3(sx, sy, seedPos.z + 18.0f), &groundPos, 500.0f)) {
            return 0;
        }
        groundTr.planeNormal = Vector3(0.0f, 0.0f, 1.0f);
    } else {
        groundPos = groundTr.endpos;
    }

    NodeMap nodeMap(step);
    std::queue<NavGenNode*> openQueue;

    NavGenNode* seedNode = nodeMap.CreateNode(groundPos, groundTr.planeNormal);
    openQueue.push(seedNode);

    size_t exploredCount = 0;
    while (!openQueue.empty() && exploredCount < maxNodes) {
        NavGenNode* curr = openQueue.front();
        openQueue.pop();
        exploredCount++;

        for (int dir = 0; dir < NUM_NAV_DIRECTIONS; ++dir) {
            if (curr->to[dir]) continue;

            float targetX = curr->pos.x;
            float targetY = curr->pos.y;

            switch (dir) {
                case NAV_DIR_NORTH: targetY -= step; break;
                case NAV_DIR_EAST:  targetX += step; break;
                case NAV_DIR_SOUTH: targetY += step; break;
                case NAV_DIR_WEST:  targetX -= step; break;
            }

            Vector3 cStart(targetX, targetY, curr->pos.z + options.maxStepHeight + 2.0f);
            Vector3 cEnd(targetX, targetY, curr->pos.z - options.maxDrop);

            BSPTraceResult gTr;
            if (!bsp.TraceWorld(cStart, cEnd, HULL_POINT, &gTr) || gTr.fraction >= 1.0f || gTr.startsolid || gTr.allsolid) {
                continue;
            }

            if (gTr.planeNormal.z < options.maxSlopeNormalZ) {
                continue;
            }

            Vector3 candGround = gTr.endpos;
            float dz = candGround.z - curr->pos.z;
            if (dz > options.maxStepHeight || dz < -options.maxDrop) {
                continue;
            }

            BSPTraceResult trHead;
            bsp.TraceWorld(candGround + Vector3(0, 0, 2.0f), candGround + Vector3(0, 0, 200.0f), HULL_POINT, &trHead);
            float clearance = trHead.endpos.z - candGround.z;
            if (clearance < options.crouchHeight - 2.0f) {
                continue;
            }

            bool isCrouch = (clearance < options.humanHeight - 8.0f);
            float stepZ = std::max(curr->pos.z, candGround.z);
            bool traversalClear = false;

            if (!isCrouch && (curr->attributes & NAV_ATTR_CROUCH) == 0) {
                BSPTraceResult trStand;
                Vector3 sStart(curr->pos.x, curr->pos.y, stepZ + 38.0f);
                Vector3 sEnd(candGround.x, candGround.y, stepZ + 38.0f);
                bsp.TraceWorld(sStart, sEnd, HULL_HUMAN, &trStand);
                if (trStand.fraction >= 1.0f && !trStand.startsolid && !trStand.allsolid) {
                    traversalClear = true;
                }
            }

            if (!traversalClear && options.generateCrouch && clearance >= options.crouchHeight - 2.0f) {
                BSPTraceResult trCrouch;
                Vector3 cSt(curr->pos.x, curr->pos.y, stepZ + 20.0f);
                Vector3 cEn(candGround.x, candGround.y, stepZ + 20.0f);
                bsp.TraceWorld(cSt, cEn, HULL_HEAD, &trCrouch);
                if (trCrouch.fraction >= 1.0f && !trCrouch.startsolid && !trCrouch.allsolid) {
                    traversalClear = true;
                    isCrouch = true;
                }
            }

            // Fallback: Check feet and waist clearance rays
            if (!traversalClear) {
                BSPTraceResult trFeet, trWaist;
                bsp.TraceWorld(curr->pos + Vector3(0, 0, 18.0f), candGround + Vector3(0, 0, 18.0f), HULL_POINT, &trFeet);
                bsp.TraceWorld(curr->pos + Vector3(0, 0, 38.0f), candGround + Vector3(0, 0, 38.0f), HULL_POINT, &trWaist);
                if (trFeet.fraction >= 1.0f && trWaist.fraction >= 1.0f && !trFeet.startsolid && !trWaist.startsolid) {
                    traversalClear = true;
                }
            }

            if (!traversalClear) continue;

            uint8_t attributes = 0;
            if (options.generateCrouch && isCrouch) {
                attributes |= NAV_ATTR_CROUCH;
            }

            NavGenNode* neighbor = nodeMap.FindNode(candGround, 18.0f);
            if (neighbor) {
                curr->to[dir] = neighbor;
                if (std::fabs(dz) <= options.maxStepHeight) {
                    NavDirType opp = static_cast<NavDirType>((dir + 2) % 4);
                    neighbor->to[opp] = curr;
                }
            } else {
                NavGenNode* newNode = nodeMap.CreateNode(candGround, gTr.planeNormal);
                newNode->attributes = attributes;
                curr->to[dir] = newNode;
                if (std::fabs(dz) <= options.maxStepHeight) {
                    NavDirType opp = static_cast<NavDirType>((dir + 2) % 4);
                    newNode->to[opp] = curr;
                }
                openQueue.push(newNode);
            }
        }
    }

    // Mark BFS nodes that overlap pre-existing nav areas as covered
    if (!existingAreas.empty()) {
        for (NavGenNode* node : nodeMap.GetAllNodes()) {
            if (node->isCovered) continue;
            for (const auto& ea : existingAreas) {
                if (node->pos.x >= ea.ext.lo.x - 1.0f && node->pos.x <= ea.ext.hi.x + 1.0f &&
                    node->pos.y >= ea.ext.lo.y - 1.0f && node->pos.y <= ea.ext.hi.y + 1.0f &&
                    std::fabs(node->pos.z - ea.centerZ) <= 24.0f) {
                    node->isCovered = true;
                    break;
                }
            }
        }
    }

    // Area Formation
    std::vector<NavArea*> newAreas;
    int tryWidth = 30;
    int tryHeight = 30;
    while (tryWidth > 0 && tryHeight > 0) {
        for (NavGenNode* node : nodeMap.GetAllNodes()) {
            if (node->isCovered) continue;
            if (TestArea(node, tryWidth, tryHeight)) {
                NavArea* a = BuildArea(node, tryWidth, tryHeight, nav);
                if (a) newAreas.push_back(a);
            }
        }
        if (tryWidth >= tryHeight) tryWidth--;
        else tryHeight--;
    }

    for (NavGenNode* node : nodeMap.GetAllNodes()) {
        if (!node->isCovered && node->IsClosedCell()) {
            NavArea* a = BuildArea(node, 1, 1, nav);
            if (a) newAreas.push_back(a);
        }
    }

    if (newAreas.empty()) return 0;

    // Connect new areas to each other
    for (NavArea* area : newAreas) {
        if (!area) continue;
        for (NavGenNode* n : nodeMap.GetAllNodes()) {
            if (!n || n->area != area) continue;
            for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
                NavGenNode* adj = n->to[d];
                if (adj && adj->area && adj->area != area) {
                    if (!area->IsConnected(adj->area, d)) {
                        area->ConnectTo(adj->area, static_cast<NavDirType>(d));
                    }
                }
            }
        }
    }

    // Connect border nodes to pre-existing areas in navmesh
    for (NavArea* area : newAreas) {
        if (!area) continue;
        const NavExtent& ext = area->GetExtent();
        for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
            Vector3 checkPos = area->GetCenter();
            switch (d) {
                case NAV_DIR_NORTH: checkPos.y = ext.lo.y - step * 0.5f; break;
                case NAV_DIR_EAST:  checkPos.x = ext.hi.x + step * 0.5f; break;
                case NAV_DIR_SOUTH: checkPos.y = ext.hi.y + step * 0.5f; break;
                case NAV_DIR_WEST:  checkPos.x = ext.lo.x - step * 0.5f; break;
            }
            NavArea* existing = nav.GetNearestArea(checkPos, step * 1.5f);
            if (existing && existing != area) {
                if (std::find(newAreas.begin(), newAreas.end(), existing) == newAreas.end()) {
                    if (std::fabs(existing->GetCenter().z - area->GetCenter().z) <= options.maxStepHeight) {
                        if (!area->IsConnected(existing, d)) {
                            area->ConnectTo(existing, static_cast<NavDirType>(d));
                        }
                        NavDirType opp = static_cast<NavDirType>((d + 2) % 4);
                        if (!existing->IsConnected(area, opp)) {
                            existing->ConnectTo(area, opp);
                        }
                    }
                }
            }
        }
    }

    // Height smoothing along seam: align new border area corners with existing areas
    for (NavArea* area : newAreas) {
        if (!area) continue;
        bool heightModified = false;
        NavExtent ext = area->GetExtent();
        float neZ = area->GetNEZ();
        float swZ = area->GetSWZ();
        float nwZ = ext.lo.z;
        float seZ = ext.hi.z;

        for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
            for (const auto& conn : area->GetAdjacentList(static_cast<NavDirType>(d))) {
                NavArea* existing = conn.area;
                if (!existing || std::find(newAreas.begin(), newAreas.end(), existing) != newAreas.end()) continue;

                float maxStep = options.maxStepHeight;
                if (d == NAV_DIR_NORTH) {
                    float exZ_NW = existing->GetZ(ext.lo.x, ext.lo.y);
                    float exZ_NE = existing->GetZ(ext.hi.x, ext.lo.y);
                    if (std::fabs(exZ_NW - nwZ) <= maxStep) { nwZ = exZ_NW; heightModified = true; }
                    if (std::fabs(exZ_NE - neZ) <= maxStep) { neZ = exZ_NE; heightModified = true; }
                } else if (d == NAV_DIR_SOUTH) {
                    float exZ_SW = existing->GetZ(ext.lo.x, ext.hi.y);
                    float exZ_SE = existing->GetZ(ext.hi.x, ext.hi.y);
                    if (std::fabs(exZ_SW - swZ) <= maxStep) { swZ = exZ_SW; heightModified = true; }
                    if (std::fabs(exZ_SE - seZ) <= maxStep) { seZ = exZ_SE; heightModified = true; }
                } else if (d == NAV_DIR_WEST) {
                    float exZ_NW = existing->GetZ(ext.lo.x, ext.lo.y);
                    float exZ_SW = existing->GetZ(ext.lo.x, ext.hi.y);
                    if (std::fabs(exZ_NW - nwZ) <= maxStep) { nwZ = exZ_NW; heightModified = true; }
                    if (std::fabs(exZ_SW - swZ) <= maxStep) { swZ = exZ_SW; heightModified = true; }
                } else if (d == NAV_DIR_EAST) {
                    float exZ_NE = existing->GetZ(ext.hi.x, ext.lo.y);
                    float exZ_SE = existing->GetZ(ext.hi.x, ext.hi.y);
                    if (std::fabs(exZ_NE - neZ) <= maxStep) { neZ = exZ_NE; heightModified = true; }
                    if (std::fabs(exZ_SE - seZ) <= maxStep) { seZ = exZ_SE; heightModified = true; }
                }
            }
        }

        if (heightModified) {
            ext.lo.z = nwZ;
            ext.hi.z = seZ;
            nav.GetGrid().RemoveArea(area);
            area->SetExtent(ext);
            area->SetCornerHeights(neZ, swZ);
            nav.GetGrid().AddArea(area);
        }
    }

    if (outCreatedAreaIds) {
        outCreatedAreaIds->clear();
        for (NavArea* a : newAreas) {
            if (a) outCreatedAreaIds->push_back(a->GetID());
        }
    }

    return newAreas.size();
}

NavGenerator::BatchResult NavGenerator::GenerateBatch(
    const std::vector<std::string>& bspFiles,
    const std::string& outputDirectory,
    const NavGenerateOptions& options,
    bool overwriteExisting,
    BatchProgressCallback batchProgress
) {
    BatchResult batchResult;
    batchResult.totalMaps = bspFiles.size();
    batchResult.items.resize(bspFiles.size());

    if (bspFiles.empty()) {
        return batchResult;
    }

    auto batchStart = std::chrono::high_resolution_clock::now();

    // Prepare batch items
    for (size_t i = 0; i < bspFiles.size(); ++i) {
        batchResult.items[i].bspPath = bspFiles[i];

        fs::path p(bspFiles[i]);
        fs::path outNavPath;
        if (!outputDirectory.empty()) {
            outNavPath = fs::path(outputDirectory) / (p.stem().string() + ".nav");
        } else {
            outNavPath = p.parent_path() / (p.stem().string() + ".nav");
        }
        batchResult.items[i].navPath = outNavPath.string();
    }

    size_t numThreads = options.maxThreads > 0 ? static_cast<size_t>(options.maxThreads)
                                               : std::max(1u, std::thread::hardware_concurrency());
    numThreads = std::min(numThreads, bspFiles.size());

    std::atomic<size_t> currentIndex{0};
    std::atomic<size_t> completedCount{0};
    std::mutex progressMutex;

    auto worker = [&]() {
        while (true) {
            size_t idx = currentIndex.fetch_add(1);
            if (idx >= bspFiles.size()) break;

            auto& item = batchResult.items[idx];

            if (!overwriteExisting && fs::exists(item.navPath)) {
                item.result.success = true;
                item.result.errorMessage = "Skipped (already exists)";
            } else {
                item.result = GenerateToFile(item.bspPath, item.navPath, options, nullptr);
            }

            size_t done = completedCount.fetch_add(1) + 1;
            if (batchProgress) {
                std::lock_guard<std::mutex> lock(progressMutex);
                batchProgress(done, batchResult.totalMaps, item);
            }
        }
    };

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (size_t t = 0; t < numThreads; ++t) {
        threads.emplace_back(worker);
    }
    for (auto& t : threads) {
        if (t.joinable()) t.join();
    }

    auto batchEnd = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> totalElapsed = batchEnd - batchStart;
    batchResult.totalDurationSeconds = totalElapsed.count();

    for (const auto& item : batchResult.items) {
        if (item.result.success) {
            batchResult.succeeded++;
        } else {
            batchResult.failed++;
        }
    }

    return batchResult;
}

NavGenerator::BatchResult NavGenerator::GenerateDirectory(
    const std::string& directoryPath,
    const std::string& outputDirectory,
    const NavGenerateOptions& options,
    bool recursive,
    bool overwriteExisting,
    BatchProgressCallback batchProgress
) {
    std::vector<std::string> bspFiles;

    try {
        if (fs::exists(directoryPath) && fs::is_directory(directoryPath)) {
            if (recursive) {
                for (const auto& entry : fs::recursive_directory_iterator(directoryPath)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".bsp") {
                        bspFiles.push_back(entry.path().string());
                    }
                }
            } else {
                for (const auto& entry : fs::directory_iterator(directoryPath)) {
                    if (entry.is_regular_file() && entry.path().extension() == ".bsp") {
                        bspFiles.push_back(entry.path().string());
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        BatchResult res;
        res.items.push_back({directoryPath, "", {false, std::string("Filesystem error: ") + ex.what(), 0, 0, 0, 0.0}});
        res.failed = 1;
        return res;
    }

    std::sort(bspFiles.begin(), bspFiles.end());
    return GenerateBatch(bspFiles, outputDirectory, options, overwriteExisting, batchProgress);
}
