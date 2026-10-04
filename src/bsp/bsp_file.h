#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "bsp_types.h"
#include "bsp_entity.h"

class BSPFile {
public:
    BSPFile();
    ~BSPFile();

    bool Load(const std::string& filepath);
    bool LoadFromMemory(const uint8_t* data, size_t size);
    void Unload();
    bool IsLoaded() const { return m_loaded; }

    // Map queries
    int GetLeafIDAtPoint(const Vector3& origin) const;
    int GetContents(const Vector3& origin) const;
    bool CheckVis(int leafA, int leafB) const;
    bool CheckPAS(int leafA, int leafB) const;

    // Collision tracing
    bool TraceWorld(const Vector3& start, const Vector3& end, int hullType, BSPTraceResult* tr = nullptr) const;
    bool TraceModel(int modelIndex, const Vector3& start, const Vector3& end, int hullType, BSPTraceResult* tr = nullptr) const;
    bool GetGround(const Vector3& start, Vector3* outGround, float maxDrop = 2000.0f) const;

    // Entities
    const std::vector<BSPEntity>& GetEntities() const { return m_entities; }
    std::vector<const BSPEntity*> FindEntities(const std::string& classname) const;

    // Model and geometry data
    const dmodel_t* GetModel(int index) const;
    int GetModelCount() const { return m_numModels; }
    int GetLeafCount() const { return m_numLeaves; }
    const std::string& GetMapName() const { return m_mapName; }

private:
    bool ParseLumps(const uint8_t* buffer, size_t size);
    bool TraceNodeRecursive(int nodeNum, float p1f, float p2f, const Vector3& p1, const Vector3& p2, BSPTraceResult* tr) const;
    bool TraceClipnodeRecursive(int clipnodeNum, float p1f, float p2f, const Vector3& p1, const Vector3& p2, BSPTraceResult* tr) const;

private:
    bool m_loaded;
    std::string m_mapName;
    std::vector<uint8_t> m_rawData;

    const dplane_t* m_planes;
    int m_numPlanes;

    const dnode_t* m_nodes;
    int m_numNodes;

    const dclipnode_t* m_clipnodes;
    int m_numClipnodes;

    const dleaf_t* m_leaves;
    int m_numLeaves;

    const dmodel_t* m_models;
    int m_numModels;

    const uint8_t* m_visdata;
    int m_visdatalen;

    std::vector<BSPEntity> m_entities;
};
