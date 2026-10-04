#include "bsp_file.h"
#include <fstream>
#include <cstring>
#include <algorithm>
#include <cmath>

BSPFile::BSPFile()
    : m_loaded(false),
      m_planes(nullptr), m_numPlanes(0),
      m_nodes(nullptr), m_numNodes(0),
      m_clipnodes(nullptr), m_numClipnodes(0),
      m_leaves(nullptr), m_numLeaves(0),
      m_models(nullptr), m_numModels(0),
      m_texinfo(nullptr), m_numTexInfo(0),
      m_faces(nullptr), m_numFaces(0),
      m_vertices(nullptr), m_numVertices(0),
      m_edges(nullptr), m_numEdges(0),
      m_surfedges(nullptr), m_numSurfEdges(0),
      m_marksurfaces(nullptr), m_numMarkSurfaces(0),
      m_visdata(nullptr), m_visdatalen(0),
      m_lightdata(nullptr), m_lightdatalen(0) {
}

BSPFile::~BSPFile() {
    Unload();
}

void BSPFile::Unload() {
    m_loaded = false;
    m_mapName.clear();
    m_rawData.clear();

    m_planes = nullptr;
    m_numPlanes = 0;
    m_nodes = nullptr;
    m_numNodes = 0;
    m_clipnodes = nullptr;
    m_numClipnodes = 0;
    m_leaves = nullptr;
    m_numLeaves = 0;
    m_models = nullptr;
    m_numModels = 0;
    m_texinfo = nullptr;
    m_numTexInfo = 0;
    m_faces = nullptr;
    m_numFaces = 0;
    m_vertices = nullptr;
    m_numVertices = 0;
    m_edges = nullptr;
    m_numEdges = 0;
    m_surfedges = nullptr;
    m_numSurfEdges = 0;
    m_marksurfaces = nullptr;
    m_numMarkSurfaces = 0;
    m_visdata = nullptr;
    m_visdatalen = 0;
    m_lightdata = nullptr;
    m_lightdatalen = 0;

    m_textures.clear();
    m_entities.clear();
}

bool BSPFile::Load(const std::string& filepath) {
    Unload();

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return false;
    }

    std::streamsize fileSize = file.tellg();
    if (fileSize < static_cast<std::streamsize>(sizeof(dheader_t))) {
        return false;
    }

    file.seekg(0, std::ios::beg);
    m_rawData.resize(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(m_rawData.data()), fileSize)) {
        m_rawData.clear();
        return false;
    }

    // Extract map name from filepath
    size_t lastSlash = filepath.find_last_of("/\\");
    std::string filename = (lastSlash == std::string::npos) ? filepath : filepath.substr(lastSlash + 1);
    size_t dot = filename.find_last_of('.');
    m_mapName = (dot == std::string::npos) ? filename : filename.substr(0, dot);

    return ParseLumps(m_rawData.data(), m_rawData.size());
}

bool BSPFile::LoadFromMemory(const uint8_t* data, size_t size) {
    Unload();
    if (!data || size < sizeof(dheader_t)) {
        return false;
    }

    m_rawData.assign(data, data + size);
    return ParseLumps(m_rawData.data(), m_rawData.size());
}

bool BSPFile::ParseLumps(const uint8_t* buffer, size_t size) {
    if (!buffer || size < sizeof(dheader_t)) {
        return false;
    }

    const dheader_t* header = reinterpret_cast<const dheader_t*>(buffer);
    if (header->version != BSP_VERSION_GOLDSRC) {
        Unload();
        return false;
    }

    #define PARSE_LUMP_SAFE(type, ptr, count, index) \
        do { \
            int32_t ofs = header->lumps[index].fileofs; \
            int32_t len = header->lumps[index].filelen; \
            if (ofs < 0 || len < 0 || static_cast<size_t>(ofs + len) > size) { \
                Unload(); \
                return false; \
            } \
            ptr = reinterpret_cast<const type*>(buffer + ofs); \
            count = len / sizeof(type); \
        } while (0)

    PARSE_LUMP_SAFE(dplane_t, m_planes, m_numPlanes, LUMP_PLANES);
    PARSE_LUMP_SAFE(dnode_t, m_nodes, m_numNodes, LUMP_NODES);
    PARSE_LUMP_SAFE(dclipnode_t, m_clipnodes, m_numClipnodes, LUMP_CLIPNODES);
    PARSE_LUMP_SAFE(dleaf_t, m_leaves, m_numLeaves, LUMP_LEAVES);
    PARSE_LUMP_SAFE(dmodel_t, m_models, m_numModels, LUMP_MODELS);
    PARSE_LUMP_SAFE(texinfo_t, m_texinfo, m_numTexInfo, LUMP_TEXINFO);
    PARSE_LUMP_SAFE(dface_t, m_faces, m_numFaces, LUMP_FACES);
    PARSE_LUMP_SAFE(dvertex_t, m_vertices, m_numVertices, LUMP_VERTICES);
    PARSE_LUMP_SAFE(dedge_t, m_edges, m_numEdges, LUMP_EDGES);
    PARSE_LUMP_SAFE(int32_t, m_surfedges, m_numSurfEdges, LUMP_SURFEDGES);
    PARSE_LUMP_SAFE(uint16_t, m_marksurfaces, m_numMarkSurfaces, LUMP_MARKSURFACES);

    #undef PARSE_LUMP_SAFE

    // Textures lump
    m_textures.clear();
    int32_t texOfs = header->lumps[LUMP_TEXTURES].fileofs;
    int32_t texLen = header->lumps[LUMP_TEXTURES].filelen;
    if (texOfs >= 0 && texLen >= static_cast<int32_t>(sizeof(int32_t)) && static_cast<size_t>(texOfs + texLen) <= size) {
        const dmiptexlump_t* miptexLump = reinterpret_cast<const dmiptexlump_t*>(buffer + texOfs);
        int32_t numMiptex = miptexLump->nummiptex;
        if (numMiptex > 0 && numMiptex < 65536) {
            m_textures.reserve(static_cast<size_t>(numMiptex));
            for (int32_t i = 0; i < numMiptex; ++i) {
                size_t ofsPos = sizeof(int32_t) + i * sizeof(int32_t);
                if (ofsPos + sizeof(int32_t) > static_cast<size_t>(texLen)) break;

                int32_t dataOfs = miptexLump->dataofs[i];
                BSPTextureInfo info = {};
                if (dataOfs >= 0 && static_cast<size_t>(dataOfs + sizeof(miptex_t)) <= static_cast<size_t>(texLen)) {
                    const miptex_t* mt = reinterpret_cast<const miptex_t*>(buffer + texOfs + dataOfs);
                    std::strncpy(info.name, mt->name, sizeof(info.name) - 1);
                    info.name[sizeof(info.name) - 1] = '\0';
                    info.width = static_cast<int>(mt->width);
                    info.height = static_cast<int>(mt->height);
                }
                m_textures.push_back(info);
            }
        }
    }

    // Visdata lump
    int32_t visOfs = header->lumps[LUMP_VISIBILITY].fileofs;
    int32_t visLen = header->lumps[LUMP_VISIBILITY].filelen;
    if (visOfs >= 0 && visLen >= 0 && static_cast<size_t>(visOfs + visLen) <= size) {
        m_visdata = buffer + visOfs;
        m_visdatalen = visLen;
    } else {
        m_visdata = nullptr;
        m_visdatalen = 0;
    }

    // Lighting lump
    int32_t lightOfs = header->lumps[LUMP_LIGHTING].fileofs;
    int32_t lightLen = header->lumps[LUMP_LIGHTING].filelen;
    if (lightOfs >= 0 && lightLen >= 0 && static_cast<size_t>(lightOfs + lightLen) <= size) {
        m_lightdata = buffer + lightOfs;
        m_lightdatalen = lightLen;
    } else {
        m_lightdata = nullptr;
        m_lightdatalen = 0;
    }

    // Entities lump
    int32_t entOfs = header->lumps[LUMP_ENTITIES].fileofs;
    int32_t entLen = header->lumps[LUMP_ENTITIES].filelen;
    if (entOfs >= 0 && entLen > 0 && static_cast<size_t>(entOfs + entLen) <= size) {
        BSPEntityParser::Parse(reinterpret_cast<const char*>(buffer + entOfs), entLen, m_entities);
    }

    m_loaded = true;
    return true;
}

int BSPFile::GetLeafIDAtPoint(const Vector3& origin) const {
    if (!m_loaded || m_numNodes <= 0 || !m_nodes) {
        return -1;
    }

    int index = 0;
    while (index >= 0) {
        if (index >= m_numNodes) return -1;
        const dnode_t* node = &m_nodes[index];
        if (node->planenum < 0 || node->planenum >= m_numPlanes) return -1;

        const dplane_t* plane = &m_planes[node->planenum];
        float dot = plane->normal.Dot(origin) - plane->dist;
        index = (dot >= 0.0f) ? node->children[0] : node->children[1];
    }

    return -index - 1;
}

int BSPFile::GetContents(const Vector3& origin) const {
    if (!m_loaded || m_numModels <= 0 || !m_models) {
        return CONTENTS_EMPTY;
    }

    int index = m_models[0].headnode[0];
    while (index >= 0) {
        if (index >= m_numClipnodes) break;
        const dclipnode_t* node = &m_clipnodes[index];
        if (node->planenum < 0 || node->planenum >= m_numPlanes) break;

        const dplane_t* plane = &m_planes[node->planenum];
        float d = plane->normal.Dot(origin) - plane->dist;
        index = (d >= 0.0f) ? node->children[0] : node->children[1];
    }

    return index;
}

bool BSPFile::CheckVis(int leafA, int leafB) const {
    if (!m_loaded || !m_visdata || m_visdatalen == 0) return true;
    if (leafA <= 0 || leafA >= m_numLeaves || leafB <= 0 || leafB >= m_numLeaves) return true;
    if (leafA == leafB) return true;

    int offset = m_leaves[leafA].visofs;
    if (offset < 0 || offset >= m_visdatalen) return true;

    int currentLeaf = 1;
    const uint8_t* v = m_visdata + offset;
    const uint8_t* vEnd = m_visdata + m_visdatalen;

    while (currentLeaf < m_numLeaves && v < vEnd) {
        if (v[0] == 0) {
            v++;
            if (v >= vEnd) break;
            currentLeaf += 8 * (*v++);
        } else {
            uint8_t byteVal = *v++;
            for (int bit = 0; bit < 8; ++bit) {
                if (currentLeaf == leafB) {
                    return (byteVal & (1 << bit)) != 0;
                }
                currentLeaf++;
            }
        }
    }
    return false;
}

bool BSPFile::DecompressPVS(int leafIndex, uint8_t* outBuffer, size_t bufferSize) const {
    if (!m_loaded || !m_visdata || m_visdatalen == 0 || !outBuffer || bufferSize == 0) return false;
    if (leafIndex <= 0 || leafIndex >= m_numLeaves) return false;

    size_t rowBytes = static_cast<size_t>((m_numLeaves + 7) / 8);
    size_t copyBytes = std::min(rowBytes, bufferSize);
    std::memset(outBuffer, 0, bufferSize);

    int offset = m_leaves[leafIndex].visofs;
    if (offset < 0 || offset >= m_visdatalen) {
        std::memset(outBuffer, 0xFF, copyBytes);
        return true;
    }

    size_t outPos = 0;
    const uint8_t* in = m_visdata + offset;
    const uint8_t* inEnd = m_visdata + m_visdatalen;

    while (outPos < copyBytes && in < inEnd) {
        if (*in != 0) {
            outBuffer[outPos++] = *in++;
        } else {
            in++;
            if (in >= inEnd) break;
            uint8_t count = *in++;
            size_t toZero = std::min(static_cast<size_t>(count), copyBytes - outPos);
            std::memset(outBuffer + outPos, 0, toZero);
            outPos += count;
        }
    }

    return true;
}

bool BSPFile::DecompressPAS(int leafIndex, uint8_t* outBuffer, size_t bufferSize) const {
    if (!m_loaded || !m_visdata || m_visdatalen == 0 || !outBuffer || bufferSize == 0) return false;
    if (leafIndex <= 0 || leafIndex >= m_numLeaves) return false;

    size_t rowBytes = static_cast<size_t>((m_numLeaves + 7) / 8);
    size_t copyBytes = std::min(rowBytes, bufferSize);

    std::vector<uint8_t> pvsA(rowBytes, 0);
    if (!DecompressPVS(leafIndex, pvsA.data(), rowBytes)) {
        return false;
    }

    std::memcpy(outBuffer, pvsA.data(), copyBytes);

    std::vector<uint8_t> pvsK(rowBytes, 0);
    for (int k = 1; k < m_numLeaves; ++k) {
        size_t byteIdx = static_cast<size_t>((k - 1) / 8);
        int bitIdx = (k - 1) % 8;
        if (byteIdx < pvsA.size() && (pvsA[byteIdx] & (1 << bitIdx))) {
            if (DecompressPVS(k, pvsK.data(), rowBytes)) {
                for (size_t b = 0; b < copyBytes; ++b) {
                    outBuffer[b] |= pvsK[b];
                }
            }
        }
    }

    return true;
}

bool BSPFile::CheckPAS(int leafA, int leafB) const {
    if (!m_loaded || !m_visdata || m_visdatalen == 0) return true;
    if (leafA <= 0 || leafA >= m_numLeaves || leafB <= 0 || leafB >= m_numLeaves) return true;
    if (leafA == leafB) return true;

    if (CheckVis(leafA, leafB)) return true;

    size_t rowBytes = static_cast<size_t>((m_numLeaves + 7) / 8);
    std::vector<uint8_t> pvsA(rowBytes, 0);
    if (!DecompressPVS(leafA, pvsA.data(), rowBytes)) return true;

    for (int k = 1; k < m_numLeaves; ++k) {
        size_t byteIdx = static_cast<size_t>((k - 1) / 8);
        int bitIdx = (k - 1) % 8;
        if (byteIdx < pvsA.size() && (pvsA[byteIdx] & (1 << bitIdx))) {
            if (CheckVis(k, leafB)) {
                return true;
            }
        }
    }

    return false;
}

int BSPFile::GetVisibleLeafCount(int leafIndex) const {
    if (!m_loaded || leafIndex <= 0 || leafIndex >= m_numLeaves) return 0;
    size_t rowBytes = static_cast<size_t>((m_numLeaves + 7) / 8);
    std::vector<uint8_t> pvs(rowBytes, 0);
    if (!DecompressPVS(leafIndex, pvs.data(), rowBytes)) return 0;

    int count = 0;
    for (int k = 1; k < m_numLeaves; ++k) {
        size_t byteIdx = static_cast<size_t>((k - 1) / 8);
        int bitIdx = (k - 1) % 8;
        if (byteIdx < pvs.size() && (pvs[byteIdx] & (1 << bitIdx))) {
            count++;
        }
    }
    return count;
}

bool BSPFile::IsPointVisible(const Vector3& ptA, const Vector3& ptB) const {
    int leafA = GetLeafIDAtPoint(ptA);
    int leafB = GetLeafIDAtPoint(ptB);
    return CheckVis(leafA, leafB);
}

bool BSPFile::IsPointAudible(const Vector3& ptA, const Vector3& ptB) const {
    int leafA = GetLeafIDAtPoint(ptA);
    int leafB = GetLeafIDAtPoint(ptB);
    return CheckPAS(leafA, leafB);
}

bool BSPFile::GetWorldBounds(Vector3& mins, Vector3& maxs) const {
    if (!m_loaded || m_numModels <= 0 || !m_models) return false;
    mins = m_models[0].mins;
    maxs = m_models[0].maxs;
    return true;
}

bool BSPFile::GetLeafBounds(int leafIndex, Vector3& mins, Vector3& maxs) const {
    if (!m_loaded || leafIndex < 0 || leafIndex >= m_numLeaves || !m_leaves) return false;
    mins = Vector3(static_cast<float>(m_leaves[leafIndex].mins[0]),
                   static_cast<float>(m_leaves[leafIndex].mins[1]),
                   static_cast<float>(m_leaves[leafIndex].mins[2]));
    maxs = Vector3(static_cast<float>(m_leaves[leafIndex].maxs[0]),
                   static_cast<float>(m_leaves[leafIndex].maxs[1]),
                   static_cast<float>(m_leaves[leafIndex].maxs[2]));
    return true;
}

int BSPFile::GetLeafContents(int leafIndex) const {
    if (!m_loaded || leafIndex < 0 || leafIndex >= m_numLeaves || !m_leaves) return CONTENTS_EMPTY;
    return m_leaves[leafIndex].contents;
}

int BSPFile::GetLeafAmbient(int leafIndex, int channel) const {
    if (!m_loaded || leafIndex < 0 || leafIndex >= m_numLeaves || !m_leaves || channel < 0 || channel > 3) return 0;
    return m_leaves[leafIndex].ambient_level[channel];
}

int BSPFile::GetLeafFaceCount(int leafIndex) const {
    if (!m_loaded || leafIndex < 0 || leafIndex >= m_numLeaves || !m_leaves) return 0;
    return static_cast<int>(m_leaves[leafIndex].nummarksurfaces);
}

int BSPFile::GetLeafFaces(int leafIndex, int* outFaces, int maxFaces) const {
    if (!m_loaded || leafIndex < 0 || leafIndex >= m_numLeaves || !m_leaves || !m_marksurfaces || !outFaces || maxFaces <= 0) return 0;
    const dleaf_t* leaf = &m_leaves[leafIndex];
    int count = std::min(static_cast<int>(leaf->nummarksurfaces), maxFaces);

    for (int i = 0; i < count; ++i) {
        int markIdx = leaf->firstmarksurface + i;
        if (markIdx >= m_numMarkSurfaces) return i;
        outFaces[i] = static_cast<int>(m_marksurfaces[markIdx]);
    }
    return count;
}

const dplane_t* BSPFile::GetPlane(int index) const {
    if (!m_loaded || index < 0 || index >= m_numPlanes) return nullptr;
    return &m_planes[index];
}

const dface_t* BSPFile::GetFace(int index) const {
    if (!m_loaded || index < 0 || index >= m_numFaces) return nullptr;
    return &m_faces[index];
}

int BSPFile::GetFaceVertexCount(int faceIndex) const {
    if (!m_loaded || faceIndex < 0 || faceIndex >= m_numFaces || !m_faces) return 0;
    return static_cast<int>(m_faces[faceIndex].numedges);
}

int BSPFile::GetFacePolygon(int faceIndex, Vector3* outVertices, int maxVertices) const {
    if (!m_loaded || faceIndex < 0 || faceIndex >= m_numFaces || !m_faces || !outVertices || maxVertices <= 0) return 0;
    const dface_t* face = &m_faces[faceIndex];
    if (face->numedges < 3 || !m_surfedges || !m_edges || !m_vertices) return 0;

    int count = std::min(static_cast<int>(face->numedges), maxVertices);
    for (int i = 0; i < count; ++i) {
        int32_t seIdx = face->firstedge + i;
        if (seIdx < 0 || seIdx >= m_numSurfEdges) return i;
        int32_t se = m_surfedges[seIdx];
        uint32_t edgeIdx = static_cast<uint32_t>(std::abs(se));
        if (edgeIdx >= static_cast<uint32_t>(m_numEdges)) return i;

        const dedge_t* edge = &m_edges[edgeIdx];
        uint16_t vIdx = (se > 0) ? edge->v[0] : edge->v[1];
        if (vIdx >= m_numVertices) return i;

        outVertices[i] = m_vertices[vIdx].point;
    }
    return count;
}

const char* BSPFile::GetTextureName(int index) const {
    if (!m_loaded || index < 0 || index >= static_cast<int>(m_textures.size())) return "";
    return m_textures[index].name;
}

bool BSPFile::GetTextureDimensions(int index, int& width, int& height) const {
    if (!m_loaded || index < 0 || index >= static_cast<int>(m_textures.size())) {
        width = 0;
        height = 0;
        return false;
    }
    width = m_textures[index].width;
    height = m_textures[index].height;
    return true;
}

int BSPFile::FindTexture(const char* name) const {
    if (!m_loaded || !name || name[0] == '\0') return -1;
    for (size_t i = 0; i < m_textures.size(); ++i) {
#if defined(_WIN32)
        if (_stricmp(m_textures[i].name, name) == 0) return static_cast<int>(i);
#else
        if (strcasecmp(m_textures[i].name, name) == 0) return static_cast<int>(i);
#endif
    }
    return -1;
}

const char* BSPFile::GetFaceTextureName(int faceIndex) const {
    if (!m_loaded || faceIndex < 0 || faceIndex >= m_numFaces || !m_faces || !m_texinfo) return "";
    const dface_t* face = &m_faces[faceIndex];
    if (face->texinfo < 0 || face->texinfo >= m_numTexInfo) return "";
    int miptex = m_texinfo[face->texinfo].miptex;
    if (miptex < 0 || miptex >= static_cast<int>(m_textures.size())) return "";
    return m_textures[miptex].name;
}

BSPMaterialType BSPFile::ClassifyMaterial(const char* textureName) {
    if (!textureName || textureName[0] == '\0') return MAT_UNKNOWN;

    // Check direct prefix
    if (textureName[0] == '!') return MAT_SLOSH;
    if (textureName[0] == '{') return MAT_GRATE;

    std::string s = textureName;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    // Skip leading animation / special prefixes e.g. "+0", "+a", "-", "~"
    size_t start = 0;
    while (start < s.size() && (s[start] == '+' || s[start] == '-' || s[start] == '~' || s[start] == '{' || s[start] == '!' || (s[start] >= '0' && s[start] <= '9'))) {
        start++;
    }
    std::string sub = (start < s.size()) ? s.substr(start) : s;

    if (sub.find("glass") != std::string::npos || sub.find("window") != std::string::npos) return MAT_GLASS;
    if (sub.find("vent") != std::string::npos || sub.find("duct") != std::string::npos) return MAT_VENT;
    if (sub.find("grate") != std::string::npos || sub.find("fence") != std::string::npos || sub.find("chain") != std::string::npos) return MAT_GRATE;
    if (sub.find("wood") != std::string::npos || sub.find("plank") != std::string::npos || sub.find("crate") != std::string::npos || sub.find("board") != std::string::npos || sub.find("door") != std::string::npos || sub.find("timber") != std::string::npos) return MAT_WOOD;
    if (sub.find("metal") != std::string::npos || sub.find("pipe") != std::string::npos || sub.find("iron") != std::string::npos || sub.find("steel") != std::string::npos || sub.find("tin") != std::string::npos || sub.find("rail") != std::string::npos || sub.find("beam") != std::string::npos) return MAT_METAL;
    if (sub.find("dirt") != std::string::npos || sub.find("sand") != std::string::npos || sub.find("mud") != std::string::npos || sub.find("gravel") != std::string::npos || sub.find("ground") != std::string::npos || sub.find("earth") != std::string::npos) return MAT_DIRT;
    if (sub.find("tile") != std::string::npos || sub.find("marble") != std::string::npos) return MAT_TILE;
    if (sub.find("water") != std::string::npos || sub.find("liquid") != std::string::npos || sub.find("slime") != std::string::npos || sub.find("river") != std::string::npos) return MAT_SLOSH;
    if (sub.find("comp") != std::string::npos || sub.find("keyboard") != std::string::npos || sub.find("screen") != std::string::npos || sub.find("monitor") != std::string::npos || sub.find("console") != std::string::npos) return MAT_COMPUTER;
    if (sub.find("grass") != std::string::npos || sub.find("leaf") != std::string::npos || sub.find("leaves") != std::string::npos || sub.find("plant") != std::string::npos || sub.find("tree") != std::string::npos) return MAT_FOLIAGE;
    if (sub.find("flesh") != std::string::npos || sub.find("meat") != std::string::npos || sub.find("blood") != std::string::npos) return MAT_FLESH;

    return MAT_CONCRETE;
}

BSPMaterialType BSPFile::TraceMaterial(const Vector3& start, const Vector3& end, char* outTexture, size_t maxLen) const {
    if (outTexture && maxLen > 0) outTexture[0] = '\0';
    if (!m_loaded) return MAT_UNKNOWN;

    char texName[64] = {0};
    if (TraceTexture(start, end, texName, sizeof(texName))) {
        if (outTexture && maxLen > 0) {
            std::strncpy(outTexture, texName, maxLen - 1);
            outTexture[maxLen - 1] = '\0';
        }
        return ClassifyMaterial(texName);
    }
    return MAT_UNKNOWN;
}

int BSPFile::GetTextureFlags(int textureIndex, bool* isTransparent, bool* isFluid, bool* isSky, bool* isAnimated) const {
    if (isTransparent) *isTransparent = false;
    if (isFluid) *isFluid = false;
    if (isSky) *isSky = false;
    if (isAnimated) *isAnimated = false;

    if (!m_loaded || textureIndex < 0 || textureIndex >= static_cast<int>(m_textures.size())) {
        return TEX_FLAG_NONE;
    }

    const char* name = m_textures[textureIndex].name;
    if (isTransparent) *isTransparent = (name[0] == '{');
    if (isFluid) *isFluid = (name[0] == '!');
    if (isSky) {
#if defined(_WIN32)
        *isSky = (_strnicmp(name, "sky", 3) == 0);
#else
        *isSky = (strncasecmp(name, "sky", 3) == 0);
#endif
    }
    if (isAnimated) *isAnimated = (name[0] == '+');

    int flags = TEX_FLAG_NONE;
    if (m_texinfo) {
        for (int i = 0; i < m_numTexInfo; ++i) {
            if (m_texinfo[i].miptex == textureIndex) {
                flags = m_texinfo[i].flags;
                break;
            }
        }
    }

    return flags;
}

bool BSPFile::GetFaceLight(int faceIndex, const Vector3& point, float& outBrightness, Vector3* outColor) const {
    outBrightness = 255.0f;
    if (outColor) *outColor = Vector3(255.0f, 255.0f, 255.0f);

    if (!m_loaded || faceIndex < 0 || faceIndex >= m_numFaces || !m_faces) return false;
    const dface_t* face = &m_faces[faceIndex];

    if (face->lightofs < 0 || !m_lightdata || m_lightdatalen == 0) {
        return true;
    }

    if (face->texinfo < 0 || face->texinfo >= m_numTexInfo || !m_texinfo) return false;
    const texinfo_t* ti = &m_texinfo[face->texinfo];

    if (face->numedges < 3 || !m_surfedges || !m_edges || !m_vertices) return false;

    float mins = 1e30f, maxs = -1e30f;
    float mint = 1e30f, maxt = -1e30f;

    for (int16_t i = 0; i < face->numedges; ++i) {
        int32_t seIdx = face->firstedge + i;
        if (seIdx < 0 || seIdx >= m_numSurfEdges) break;
        int32_t se = m_surfedges[seIdx];
        uint32_t edgeIdx = static_cast<uint32_t>(std::abs(se));
        if (edgeIdx >= static_cast<uint32_t>(m_numEdges)) break;

        const dedge_t* edge = &m_edges[edgeIdx];
        uint16_t vIdx = (se > 0) ? edge->v[0] : edge->v[1];
        if (vIdx >= m_numVertices) break;

        const Vector3& v = m_vertices[vIdx].point;
        float s = v.x * ti->vecs[0][0] + v.y * ti->vecs[0][1] + v.z * ti->vecs[0][2] + ti->vecs[0][3];
        float t = v.x * ti->vecs[1][0] + v.y * ti->vecs[1][1] + v.z * ti->vecs[1][2] + ti->vecs[1][3];

        if (s < mins) mins = s;
        if (s > maxs) maxs = s;
        if (t < mint) mint = t;
        if (t > maxt) maxt = t;
    }

    float texMins = std::floor(mins / 16.0f) * 16.0f;
    float texMint = std::floor(mint / 16.0f) * 16.0f;
    float texMaxs = std::ceil(maxs / 16.0f) * 16.0f;
    float texMaxt = std::ceil(maxt / 16.0f) * 16.0f;

    int width = static_cast<int>((texMaxs - texMins) / 16.0f) + 1;
    int height = static_cast<int>((texMaxt - texMint) / 16.0f) + 1;
    if (width <= 0 || height <= 0) return false;

    float pointS = point.x * ti->vecs[0][0] + point.y * ti->vecs[0][1] + point.z * ti->vecs[0][2] + ti->vecs[0][3];
    float pointT = point.x * ti->vecs[1][0] + point.y * ti->vecs[1][1] + point.z * ti->vecs[1][2] + ti->vecs[1][3];

    float u = (pointS - texMins) / 16.0f;
    float v = (pointT - texMint) / 16.0f;

    u = std::max(0.0f, std::min(static_cast<float>(width - 1), u));
    v = std::max(0.0f, std::min(static_cast<float>(height - 1), v));

    int x0 = static_cast<int>(std::floor(u));
    int y0 = static_cast<int>(std::floor(v));
    int x1 = std::min(x0 + 1, width - 1);
    int y1 = std::min(y0 + 1, height - 1);

    float fx = u - static_cast<float>(x0);
    float fy = v - static_cast<float>(y0);

    const uint8_t* baseLight = m_lightdata + face->lightofs;
    int totalBytes = width * height * 3;
    if (face->lightofs + totalBytes > m_lightdatalen) return false;

    auto getLuxel = [&](int x, int y, float& r, float& g, float& b) {
        int idx = (y * width + x) * 3;
        r = static_cast<float>(baseLight[idx + 0]);
        g = static_cast<float>(baseLight[idx + 1]);
        b = static_cast<float>(baseLight[idx + 2]);
    };

    float r00, g00, b00;
    float r10, g10, b10;
    float r01, g01, b01;
    float r11, g11, b11;

    getLuxel(x0, y0, r00, g00, b00);
    getLuxel(x1, y0, r10, g10, b10);
    getLuxel(x0, y1, r01, g01, b01);
    getLuxel(x1, y1, r11, g11, b11);

    float w00 = (1.0f - fx) * (1.0f - fy);
    float w10 = fx * (1.0f - fy);
    float w01 = (1.0f - fx) * fy;
    float w11 = fx * fy;

    float r = w00 * r00 + w10 * r10 + w01 * r01 + w11 * r11;
    float g = w00 * g00 + w10 * g10 + w01 * g01 + w11 * g11;
    float b = w00 * b00 + w10 * b10 + w01 * b01 + w11 * b11;

    outBrightness = 0.299f * r + 0.587f * g + 0.114f * b;
    if (outColor) {
        outColor->x = r;
        outColor->y = g;
        outColor->z = b;
    }

    return true;
}

bool BSPFile::GetPointLight(const Vector3& start, const Vector3& end, float& outBrightness, Vector3* outColor) const {
    outBrightness = 255.0f;
    if (outColor) *outColor = Vector3(255.0f, 255.0f, 255.0f);
    if (!m_loaded) return false;

    BSPTraceResult tr;
    if (!TraceWorld(start, end, HULL_POINT, &tr)) {
        return false;
    }

    if (tr.hitFace >= 0) {
        return GetFaceLight(tr.hitFace, tr.endpos, outBrightness, outColor);
    }

    return false;
}

bool BSPFile::GetSkyname(std::string& outSkyname) const {
    if (!m_loaded || m_entities.empty()) return false;
    if (m_entities[0].classname == "worldspawn") {
        outSkyname = m_entities[0].GetString("skyname");
        return !outSkyname.empty();
    }
    return false;
}

bool BSPFile::GetMapTitle(std::string& outTitle) const {
    if (!m_loaded || m_entities.empty()) return false;
    if (m_entities[0].classname == "worldspawn") {
        outTitle = m_entities[0].GetString("message");
        return !outTitle.empty();
    }
    return false;
}

bool BSPFile::GetWadList(std::string& outWadList) const {
    if (!m_loaded || m_entities.empty()) return false;
    if (m_entities[0].classname == "worldspawn") {
        outWadList = m_entities[0].GetString("wad");
        return !outWadList.empty();
    }
    return false;
}

std::vector<int> BSPFile::FindEntitiesByTarget(const std::string& target) const {
    std::vector<int> result;
    if (target.empty() || !m_loaded) return result;
    for (size_t i = 0; i < m_entities.size(); ++i) {
        if (m_entities[i].GetString("target") == target) {
            result.push_back(static_cast<int>(i));
        }
    }
    return result;
}

std::vector<int> BSPFile::FindEntitiesByTargetname(const std::string& targetname) const {
    std::vector<int> result;
    if (targetname.empty() || !m_loaded) return result;
    for (size_t i = 0; i < m_entities.size(); ++i) {
        if (m_entities[i].GetString("targetname") == targetname) {
            result.push_back(static_cast<int>(i));
        }
    }
    return result;
}

bool BSPFile::IsPointInFace(int faceIndex, const Vector3& point) const {
    if (faceIndex < 0 || faceIndex >= m_numFaces || !m_faces || !m_planes || !m_vertices || !m_edges || !m_surfedges) {
        return false;
    }

    const dface_t* face = &m_faces[faceIndex];
    if (face->planenum < 0 || face->planenum >= m_numPlanes) return false;
    if (face->numedges < 3) return false;

    Vector3 pn = m_planes[face->planenum].normal;
    if (face->side != 0) {
        pn = pn * -1.0f;
    }

    for (int16_t i = 0; i < face->numedges; ++i) {
        int32_t seIdx = face->firstedge + i;
        if (seIdx < 0 || seIdx >= m_numSurfEdges) return false;

        int32_t se = m_surfedges[seIdx];
        uint32_t edgeIdx = static_cast<uint32_t>(std::abs(se));
        if (edgeIdx >= static_cast<uint32_t>(m_numEdges)) return false;

        const dedge_t* edge = &m_edges[edgeIdx];
        uint16_t v0Idx = (se > 0) ? edge->v[0] : edge->v[1];
        uint16_t v1Idx = (se > 0) ? edge->v[1] : edge->v[0];

        if (v0Idx >= m_numVertices || v1Idx >= m_numVertices) return false;

        Vector3 p0 = m_vertices[v0Idx].point;
        Vector3 p1 = m_vertices[v1Idx].point;

        Vector3 edgeDir = p1 - p0;
        Vector3 inward = edgeDir.Cross(pn);

        if (inward.Dot(point - p0) < -0.1f) {
            return false;
        }
    }

    return true;
}

Vector3 BSPFile::GetFaceCentroid(int faceIndex) const {
    if (faceIndex < 0 || faceIndex >= m_numFaces || !m_faces || !m_vertices || !m_edges || !m_surfedges) {
        return Vector3();
    }

    const dface_t* face = &m_faces[faceIndex];
    if (face->numedges == 0) return Vector3();

    Vector3 sum;
    int count = 0;
    for (int16_t i = 0; i < face->numedges; ++i) {
        int32_t seIdx = face->firstedge + i;
        if (seIdx < 0 || seIdx >= m_numSurfEdges) break;

        int32_t se = m_surfedges[seIdx];
        uint32_t edgeIdx = static_cast<uint32_t>(std::abs(se));
        if (edgeIdx >= static_cast<uint32_t>(m_numEdges)) break;

        const dedge_t* edge = &m_edges[edgeIdx];
        uint16_t v0Idx = (se > 0) ? edge->v[0] : edge->v[1];
        if (v0Idx < m_numVertices) {
            sum = sum + m_vertices[v0Idx].point;
            count++;
        }
    }

    if (count > 0) {
        return sum * (1.0f / static_cast<float>(count));
    }
    return Vector3();
}

int BSPFile::FindFaceOnNode(int nodeNum, const Vector3& point) const {
    if (nodeNum < 0 || nodeNum >= m_numNodes || !m_nodes || !m_faces) return -1;
    const dnode_t* node = &m_nodes[nodeNum];
    if (node->numfaces == 0) return -1;
    if (node->numfaces == 1) return node->firstface;

    int bestFace = node->firstface;
    float bestDistSq = 1e30f;

    for (uint16_t i = 0; i < node->numfaces; ++i) {
        int faceIdx = node->firstface + i;
        if (faceIdx >= m_numFaces) break;

        if (IsPointInFace(faceIdx, point)) {
            return faceIdx;
        }

        Vector3 centroid = GetFaceCentroid(faceIdx);
        float dsq = point.DistToSqr(centroid);
        if (dsq < bestDistSq) {
            bestDistSq = dsq;
            bestFace = faceIdx;
        }
    }

    return bestFace;
}

bool BSPFile::TraceNodeRecursive(int nodeNum, float p1f, float p2f, const Vector3& p1, const Vector3& p2, BSPTraceResult* tr) const {
    if (nodeNum < 0) {
        int leafIdx = -nodeNum - 1;
        if (leafIdx >= 0 && leafIdx < m_numLeaves && m_leaves[leafIdx].contents == CONTENTS_SOLID) {
            if (tr) {
                if (p1f < tr->fraction) {
                    tr->fraction = p1f;
                    tr->endpos = p1;
                    tr->hitContents = CONTENTS_SOLID;
                }
            }
            return true;
        }
        return false;
    }

    if (nodeNum >= m_numNodes) return false;
    const dnode_t* node = &m_nodes[nodeNum];
    if (node->planenum < 0 || node->planenum >= m_numPlanes) return false;
    const dplane_t* plane = &m_planes[node->planenum];

    float t1 = plane->normal.Dot(p1) - plane->dist;
    float t2 = plane->normal.Dot(p2) - plane->dist;

    if (t1 >= 0.0f && t2 >= 0.0f) {
        return TraceNodeRecursive(node->children[0], p1f, p2f, p1, p2, tr);
    }
    if (t1 < 0.0f && t2 < 0.0f) {
        return TraceNodeRecursive(node->children[1], p1f, p2f, p1, p2, tr);
    }

    float frac = t1 / (t1 - t2);
    frac = std::max(0.0f, std::min(1.0f, frac));
    Vector3 mid = p1 + (p2 - p1) * frac;
    float midf = p1f + (p2f - p1f) * frac;

    int side = (t1 < 0.0f) ? 1 : 0;
    if (TraceNodeRecursive(node->children[side], p1f, midf, p1, mid, tr)) {
        return true;
    }

    if (TraceNodeRecursive(node->children[1 - side], midf, p2f, mid, p2, tr)) {
        if (tr && tr->planeNormal == Vector3(0, 0, 0)) {
            tr->planeNormal = (side == 0) ? plane->normal : (plane->normal * -1.0f);
            tr->planeDist = plane->dist;
            tr->hitPlane = node->planenum;

            int faceIdx = FindFaceOnNode(nodeNum, mid);
            if (faceIdx >= 0) {
                tr->hitFace = faceIdx;
                const char* tex = GetFaceTextureName(faceIdx);
                if (tex && tex[0] != '\0') {
                    std::strncpy(tr->hitTexture, tex, sizeof(tr->hitTexture) - 1);
                    tr->hitTexture[sizeof(tr->hitTexture) - 1] = '\0';
                }
            }
        }
        return true;
    }

    return false;
}

bool BSPFile::TraceClipnodeRecursive(int clipnodeNum, float p1f, float p2f, const Vector3& p1, const Vector3& p2, BSPTraceResult* tr) const {
    if (clipnodeNum < 0) {
        if (clipnodeNum == CONTENTS_SOLID || clipnodeNum == CONTENTS_CLIP) {
            if (tr) {
                if (p1f < tr->fraction) {
                    tr->fraction = p1f;
                    tr->endpos = p1;
                    tr->hitContents = clipnodeNum;
                }
            }
            return true;
        }
        return false;
    }

    if (clipnodeNum >= m_numClipnodes) return false;
    const dclipnode_t* node = &m_clipnodes[clipnodeNum];
    if (node->planenum < 0 || node->planenum >= m_numPlanes) return false;
    const dplane_t* plane = &m_planes[node->planenum];

    float t1 = plane->normal.Dot(p1) - plane->dist;
    float t2 = plane->normal.Dot(p2) - plane->dist;

    if (t1 >= 0.0f && t2 >= 0.0f) {
        return TraceClipnodeRecursive(node->children[0], p1f, p2f, p1, p2, tr);
    }
    if (t1 < 0.0f && t2 < 0.0f) {
        return TraceClipnodeRecursive(node->children[1], p1f, p2f, p1, p2, tr);
    }

    float frac = t1 / (t1 - t2);
    frac = std::max(0.0f, std::min(1.0f, frac));
    Vector3 mid = p1 + (p2 - p1) * frac;
    float midf = p1f + (p2f - p1f) * frac;

    int side = (t1 < 0.0f) ? 1 : 0;
    if (TraceClipnodeRecursive(node->children[side], p1f, midf, p1, mid, tr)) {
        if (tr && tr->planeNormal == Vector3(0, 0, 0)) {
            tr->planeNormal = (side == 0) ? plane->normal : (plane->normal * -1.0f);
            tr->planeDist = plane->dist;
            tr->hitPlane = node->planenum;
        }
        return true;
    }

    if (TraceClipnodeRecursive(node->children[1 - side], midf, p2f, mid, p2, tr)) {
        if (tr && tr->planeNormal == Vector3(0, 0, 0)) {
            tr->planeNormal = (side == 0) ? plane->normal : (plane->normal * -1.0f);
            tr->planeDist = plane->dist;
            tr->hitPlane = node->planenum;
        }
        return true;
    }

    return false;
}

bool BSPFile::TraceWorld(const Vector3& start, const Vector3& end, int hullType, BSPTraceResult* tr) const {
    if (!m_loaded || m_numModels <= 0 || !m_models) return false;
    bool hit = TraceModel(0, start, end, hullType, tr);

    if (hit && tr && hullType != HULL_POINT && tr->hitTexture[0] == '\0') {
        Vector3 dir = (end - start).Normalized();
        Vector3 testStart = tr->endpos - dir * 2.0f;
        Vector3 testEnd = tr->endpos + dir * 4.0f;
        BSPTraceResult pointTr;
        if (TraceModel(0, testStart, testEnd, HULL_POINT, &pointTr)) {
            tr->hitFace = pointTr.hitFace;
            tr->hitPlane = pointTr.hitPlane;
            std::strncpy(tr->hitTexture, pointTr.hitTexture, sizeof(tr->hitTexture) - 1);
            tr->hitTexture[sizeof(tr->hitTexture) - 1] = '\0';
        }
    }

    return hit;
}

bool BSPFile::TraceModel(int modelIndex, const Vector3& start, const Vector3& end, int hullType, BSPTraceResult* tr) const {
    if (!m_loaded || modelIndex < 0 || modelIndex >= m_numModels) return false;
    if (hullType < 0 || hullType > 3) return false;

    if (tr) {
        tr->fraction = 1.0f;
        tr->endpos = end;
        tr->allsolid = false;
        tr->startsolid = false;
        tr->hitContents = CONTENTS_EMPTY;
        tr->hitPlane = -1;
        tr->hitFace = -1;
        tr->hitTexture[0] = '\0';
        tr->planeNormal = Vector3(0.0f, 0.0f, 0.0f);
        tr->planeDist = 0.0f;
    }

    int headnode = m_models[modelIndex].headnode[hullType];
    if (hullType == HULL_POINT) {
        return TraceNodeRecursive(headnode, 0.0f, 1.0f, start, end, tr);
    }
    return TraceClipnodeRecursive(headnode, 0.0f, 1.0f, start, end, tr);
}

bool BSPFile::TraceTexture(const Vector3& start, const Vector3& end, char* outTexture, size_t maxLen) const {
    if (!outTexture || maxLen == 0) return false;
    outTexture[0] = '\0';
    if (!m_loaded) return false;

    BSPTraceResult tr;
    if (!TraceModel(0, start, end, HULL_POINT, &tr)) {
        return false;
    }

    if (tr.hitTexture[0] != '\0') {
        std::strncpy(outTexture, tr.hitTexture, maxLen - 1);
        outTexture[maxLen - 1] = '\0';
        return true;
    }

    return false;
}

bool BSPFile::GetGround(const Vector3& start, Vector3* outGround, float maxDrop) const {
    if (!m_loaded || !outGround) return false;

    Vector3 end = start;
    end.z -= maxDrop;

    BSPTraceResult tr;
    bool hit = TraceWorld(start, end, HULL_HUMAN, &tr);
    if (!hit) {
        hit = TraceWorld(start, end, HULL_POINT, &tr);
    }

    if (hit) {
        *outGround = tr.endpos;
        return true;
    }

    return false;
}

std::vector<const BSPEntity*> BSPFile::FindEntities(const std::string& classname) const {
    std::vector<const BSPEntity*> result;
    for (const auto& ent : m_entities) {
        if (ent.classname == classname) {
            result.push_back(&ent);
        }
    }
    return result;
}

const BSPEntity* BSPFile::GetEntity(int index) const {
    if (!m_loaded || index < 0 || index >= static_cast<int>(m_entities.size())) return nullptr;
    return &m_entities[index];
}

const dmodel_t* BSPFile::GetModel(int index) const {
    if (!m_loaded || index < 0 || index >= m_numModels) return nullptr;
    return &m_models[index];
}
