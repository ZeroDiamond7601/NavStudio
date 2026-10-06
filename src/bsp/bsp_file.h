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
    size_t GetFileSize() const { return m_rawData.size(); }

    // Map queries
    int GetLeafIDAtPoint(const Vector3& origin) const;
    int GetContents(const Vector3& origin) const;
    bool CheckVis(int leafA, int leafB) const;
    bool CheckPAS(int leafA, int leafB) const;
    bool DecompressPVS(int leafIndex, uint8_t* outBuffer, size_t bufferSize) const;
    bool DecompressPAS(int leafIndex, uint8_t* outBuffer, size_t bufferSize) const;
    int GetVisibleLeafCount(int leafIndex) const;
    bool IsPointVisible(const Vector3& ptA, const Vector3& ptB) const;
    bool IsPointAudible(const Vector3& ptA, const Vector3& ptB) const;

    // World & Leaves info
    bool GetWorldBounds(Vector3& mins, Vector3& maxs) const;
    bool GetLeafBounds(int leafIndex, Vector3& mins, Vector3& maxs) const;
    int GetLeafContents(int leafIndex) const;
    int GetLeafAmbient(int leafIndex, int channel) const;
    int GetPVSByteSize() const { return (m_numLeaves + 7) / 8; }
    int GetLeafFaceCount(int leafIndex) const;
    int GetLeafFaces(int leafIndex, int* outFaces, int maxFaces) const;

    // Collision tracing
    bool TraceWorld(const Vector3& start, const Vector3& end, int hullType, BSPTraceResult* tr = nullptr) const;
    bool TraceModel(int modelIndex, const Vector3& start, const Vector3& end, int hullType, BSPTraceResult* tr = nullptr) const;
    bool TraceTexture(const Vector3& start, const Vector3& end, char* outTexture, size_t maxLen) const;
    BSPMaterialType TraceMaterial(const Vector3& start, const Vector3& end, char* outTexture = nullptr, size_t maxLen = 0) const;
    bool GetGround(const Vector3& start, Vector3* outGround, float maxDrop = 2000.0f, int hullType = HULL_HUMAN) const;

    // Lightmap and illumination sampling
    bool GetPointLight(const Vector3& start, const Vector3& end, float& outBrightness, Vector3* outColor = nullptr) const;
    bool GetFaceLight(int faceIndex, const Vector3& point, float& outBrightness, Vector3* outColor = nullptr) const;

    // Entities & Target relationships
    const std::vector<BSPEntity>& GetEntities() const { return m_entities; }
    std::vector<const BSPEntity*> FindEntities(const std::string& classname) const;
    const BSPEntity* GetEntity(int index) const;
    int GetEntityCount() const { return static_cast<int>(m_entities.size()); }
    std::vector<int> FindEntitiesByTarget(const std::string& target) const;
    std::vector<int> FindEntitiesByTargetname(const std::string& targetname) const;

    // Worldspawn metadata
    bool GetSkyname(std::string& outSkyname) const;
    bool GetMapTitle(std::string& outTitle) const;
    bool GetWadList(std::string& outWadList) const;

    // Model and geometry data
    const dmodel_t* GetModel(int index) const;
    int GetModelCount() const { return m_numModels; }
    int GetLeafCount() const { return m_numLeaves; }
    int GetNodeCount() const { return m_numNodes; }
    int GetPlaneCount() const { return m_numPlanes; }
    int GetFaceCount() const { return m_numFaces; }
    const dplane_t* GetPlane(int index) const;
    const dface_t* GetFace(int index) const;
    int GetFaceVertexCount(int faceIndex) const;
    int GetFacePolygon(int faceIndex, Vector3* outVertices, int maxVertices) const;
    const std::string& GetMapName() const { return m_mapName; }

    // Textures & Materials
    int GetTextureCount() const { return static_cast<int>(m_textures.size()); }
    const char* GetTextureName(int index) const;
    bool GetTextureDimensions(int index, int& width, int& height) const;
    int FindTexture(const char* name) const;
    const char* GetFaceTextureName(int faceIndex) const;
    static BSPMaterialType ClassifyMaterial(const char* textureName);
    int GetTextureFlags(int textureIndex, bool* isTransparent = nullptr, bool* isFluid = nullptr, bool* isSky = nullptr, bool* isAnimated = nullptr) const;

    // TexInfo and MipTex access
    const texinfo_t* GetTexInfo(int index) const;
    int GetTexInfoCount() const { return m_numTexInfo; }
    const miptex_t* GetMiptex(int index) const;
    const uint8_t* GetMiptexData(int index, size_t* outRemaining = nullptr) const;

private:
    bool ParseLumps(const uint8_t* buffer, size_t size);
    bool TraceNodeRecursive(int nodeNum, float p1f, float p2f, const Vector3& p1, const Vector3& p2, BSPTraceResult* tr, int depth = 0) const;
    bool TraceClipnodeRecursive(int clipnodeNum, float p1f, float p2f, const Vector3& p1, const Vector3& p2, BSPTraceResult* tr, int depth = 0) const;
    int FindFaceOnNode(int nodeNum, const Vector3& point) const;
    bool IsPointInFace(int faceIndex, const Vector3& point) const;
    Vector3 GetFaceCentroid(int faceIndex) const;

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

    const texinfo_t* m_texinfo;
    int m_numTexInfo;

    const dface_t* m_faces;
    int m_numFaces;

    const dvertex_t* m_vertices;
    int m_numVertices;

    const dedge_t* m_edges;
    int m_numEdges;

    const int32_t* m_surfedges;
    int m_numSurfEdges;

    const uint16_t* m_marksurfaces;
    int m_numMarkSurfaces;

    const uint8_t* m_visdata;
    int m_visdatalen;

    const uint8_t* m_lightdata;
    int m_lightdatalen;

    const uint8_t* m_texlumpData;
    int32_t m_texlumpLen;

    std::vector<BSPTextureInfo> m_textures;
    std::vector<BSPEntity> m_entities;
};
