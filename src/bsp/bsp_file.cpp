#include "bsp_file.h"
#include <fstream>
#include <cstring>
#include <algorithm>

BSPFile::BSPFile()
    : m_loaded(false),
      m_planes(nullptr), m_numPlanes(0),
      m_nodes(nullptr), m_numNodes(0),
      m_clipnodes(nullptr), m_numClipnodes(0),
      m_leaves(nullptr), m_numLeaves(0),
      m_models(nullptr), m_numModels(0),
      m_visdata(nullptr), m_visdatalen(0) {
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
    m_visdata = nullptr;
    m_visdatalen = 0;

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

    return index; // If < 0, it is the contents code
}

bool BSPFile::CheckVis(int leafA, int leafB) const {
    if (!m_loaded || !m_visdata || m_visdatalen == 0) return true;
    if (leafA <= 0 || leafA >= m_numLeaves || leafB <= 0 || leafB >= m_numLeaves) return true;

    int offset = m_leaves[leafA].visofs;
    if (offset < 0 || offset >= m_visdatalen) return true;

    int currentLeaf = 1;
    const uint8_t* v = m_visdata + offset;
    while (currentLeaf < m_numLeaves) {
        if (v[0] == 0) {
            currentLeaf += 8 * v[1];
            v += 2;
        } else {
            for (int bit = 1; bit <= 8; bit++) {
                if (currentLeaf == leafB) {
                    return (v[0] & (1 << (bit - 1))) != 0;
                }
                currentLeaf++;
            }
            v++;
        }
    }
    return false;
}

bool BSPFile::CheckPAS(int leafA, int leafB) const {
    if (!m_loaded || !m_visdata || m_visdatalen == 0) return true;
    if (leafA <= 0 || leafA >= m_numLeaves || leafB <= 0 || leafB >= m_numLeaves) return true;

    int offset = m_leaves[leafA].visofs;
    if (offset < 0) return true;

    int pasOffset = offset + (m_numLeaves + 7) / 8;
    if (pasOffset >= m_visdatalen) return true;

    int currentLeaf = 1;
    const uint8_t* v = m_visdata + pasOffset;
    while (currentLeaf < m_numLeaves) {
        if (v[0] == 0) {
            currentLeaf += 8 * v[1];
            v += 2;
        } else {
            for (int bit = 1; bit <= 8; bit++) {
                if (currentLeaf == leafB) {
                    return (v[0] & (1 << (bit - 1))) != 0;
                }
                currentLeaf++;
            }
            v++;
        }
    }
    return false;
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
        if (tr && tr->planeNormal == Vector3(0, 0, 0)) {
            tr->planeNormal = (side == 0) ? plane->normal : (plane->normal * -1.0f);
            tr->planeDist = plane->dist;
        }
        return true;
    }

    return TraceNodeRecursive(node->children[1 - side], midf, p2f, mid, p2, tr);
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
        }
        return true;
    }

    return TraceClipnodeRecursive(node->children[1 - side], midf, p2f, mid, p2, tr);
}

bool BSPFile::TraceWorld(const Vector3& start, const Vector3& end, int hullType, BSPTraceResult* tr) const {
    if (!m_loaded || m_numModels <= 0 || !m_models) return false;
    return TraceModel(0, start, end, hullType, tr);
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
        tr->planeNormal = Vector3(0.0f, 0.0f, 0.0f);
        tr->planeDist = 0.0f;
    }

    int headnode = m_models[modelIndex].headnode[hullType];
    if (hullType == HULL_POINT) {
        return TraceNodeRecursive(headnode, 0.0f, 1.0f, start, end, tr);
    }
    return TraceClipnodeRecursive(headnode, 0.0f, 1.0f, start, end, tr);
}

bool BSPFile::GetGround(const Vector3& start, Vector3* outGround, float maxDrop) const {
    if (!m_loaded || !outGround) return false;

    Vector3 end = start;
    end.z -= maxDrop;

    BSPTraceResult tr;
    // Trace with Hull 1 (standard player hull)
    bool hit = TraceWorld(start, end, HULL_HUMAN, &tr);
    if (!hit) {
        // Fallback to point trace if human hull is tight
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

const dmodel_t* BSPFile::GetModel(int index) const {
    if (!m_loaded || index < 0 || index >= m_numModels) return nullptr;
    return &m_models[index];
}
