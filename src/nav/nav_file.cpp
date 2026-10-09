#include "nav_file.h"
#include "../bsp/bsp_file.h"
#include <fstream>
#include <cstring>
#include <algorithm>

NavMesh::NavMesh()
    : m_loaded(false), m_version(0), m_bspSize(0) {
}

NavMesh::~NavMesh() {
    Unload();
}

void NavMesh::ClearLadders() {
    for (NavLadder* ladder : m_ladders) {
        delete ladder;
    }
    m_ladders.clear();
}

void NavMesh::Unload() {
    m_loaded = false;
    m_version = 0;
    m_bspSize = 0;

    for (NavArea* area : m_areas) {
        delete area;
    }
    m_areas.clear();
    m_placeNames.clear();
    m_grid.Reset();
    ClearLadders();
}

NavArea* NavMesh::GetArea(size_t index) const {
    if (index >= m_areas.size()) return nullptr;
    return m_areas[index];
}

std::string NavMesh::GetPlaceName(uint16_t placeId) const {
    if (placeId == 0 || placeId > m_placeNames.size()) {
        return "";
    }
    return m_placeNames[placeId - 1];
}

bool NavMesh::Load(const std::string& filepath) {
    Unload();

    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    std::streamsize fileSize = file.tellg();
    if (fileSize < 16) return false;

    file.seekg(0, std::ios::beg);
    std::vector<uint8_t> buffer(static_cast<size_t>(fileSize));
    if (!file.read(reinterpret_cast<char*>(buffer.data()), fileSize)) {
        return false;
    }

    return LoadFromMemory(buffer.data(), buffer.size());
}

bool NavMesh::LoadFromMemory(const uint8_t* buffer, size_t size) {
    Unload();
    if (!buffer || size < 16) return false;

    size_t cursor = 0;

    #define READ_RAW(dest, bytes) \
        do { \
            if (cursor + (bytes) > size) { Unload(); return false; } \
            std::memcpy(&(dest), buffer + cursor, (bytes)); \
            cursor += (bytes); \
        } while (0)

    #define READ_TYPE(var, type) READ_RAW(var, sizeof(type))

    uint32_t magic = 0;
    READ_TYPE(magic, uint32_t);
    if (magic != NAV_MAGIC_NUMBER) {
        Unload();
        return false;
    }

    READ_TYPE(m_version, uint32_t);
    if (m_version < NAV_VERSION_4 || m_version > NAV_VERSION_CURRENT) {
        // Supported CS 1.6 / CZ nav mesh versions
        Unload();
        return false;
    }

    // Read BSP size (v4+)
    READ_TYPE(m_bspSize, uint32_t);

    // Read Place Directory (v5+)
    if (m_version >= NAV_VERSION_5) {
        uint16_t placeCount = 0;
        READ_TYPE(placeCount, uint16_t);
        m_placeNames.reserve(placeCount);

        for (uint16_t i = 0; i < placeCount; i++) {
            uint16_t strLen = 0;
            READ_TYPE(strLen, uint16_t);
            if (cursor + strLen > size) { Unload(); return false; }

            std::string name(reinterpret_cast<const char*>(buffer + cursor), strLen);
            cursor += strLen;

            // Trim null terminators if present
            while (!name.empty() && name.back() == '\0') {
                name.pop_back();
            }
            m_placeNames.push_back(name);
        }
    }

    // Read Area count
    uint32_t areaCount = 0;
    READ_TYPE(areaCount, uint32_t);
    if (areaCount == 0 || areaCount > (size / 40)) {
        Unload();
        return false;
    }

    m_areas.reserve(areaCount);

    float minX = 999999.0f, minY = 999999.0f;
    float maxX = -999999.0f, maxY = -999999.0f;

    for (uint32_t i = 0; i < areaCount; i++) {
        NavArea* area = new NavArea();
        m_areas.push_back(area);

        uint32_t id = 0;
        READ_TYPE(id, uint32_t);
        area->SetID(id);

        uint8_t flags = 0;
        READ_TYPE(flags, uint8_t);
        area->SetAttributes(flags);

        NavExtent extent;
        READ_RAW(extent.lo, 3 * sizeof(float));
        READ_RAW(extent.hi, 3 * sizeof(float));
        area->SetExtent(extent);

        float neZ = 0.0f, swZ = 0.0f;
        READ_TYPE(neZ, float);
        READ_TYPE(swZ, float);
        area->SetCornerHeights(neZ, swZ);

        minX = std::min(minX, extent.lo.x);
        minY = std::min(minY, extent.lo.y);
        maxX = std::max(maxX, extent.hi.x);
        maxY = std::max(maxY, extent.hi.y);

        // 4 directions
        for (int d = 0; d < NUM_NAV_DIRECTIONS; d++) {
            uint32_t connCount = 0;
            READ_TYPE(connCount, uint32_t);
            auto& list = area->GetAdjacentList(static_cast<NavDirType>(d));
            list.reserve(connCount);

            for (uint32_t c = 0; c < connCount; c++) {
                uint32_t targetId = 0;
                READ_TYPE(targetId, uint32_t);
                list.push_back(NavConnect(targetId, nullptr));
            }
        }

        // Hiding spots
        uint8_t hidingCount = 0;
        READ_TYPE(hidingCount, uint8_t);
        area->GetHidingSpots().reserve(hidingCount);

        for (uint8_t h = 0; h < hidingCount; h++) {
            uint32_t hid = 0;
            Vector3 hpos;
            uint8_t hflags = 0;
            READ_TYPE(hid, uint32_t);
            READ_RAW(hpos, 3 * sizeof(float));
            READ_TYPE(hflags, uint8_t);
            area->GetHidingSpots().push_back(NavHidingSpot(hid, hpos, hflags));
        }

        // Approach areas
        uint8_t approachCount = 0;
        READ_TYPE(approachCount, uint8_t);
        area->GetApproachInfo().reserve(approachCount);

        for (uint8_t a = 0; a < approachCount; a++) {
            NavApproachInfo info;
            READ_TYPE(info.hereId, uint32_t);
            READ_TYPE(info.prevId, uint32_t);
            uint8_t pHow = 0;
            READ_TYPE(pHow, uint8_t);
            info.prevHow = static_cast<NavTraverseType>(pHow);
            READ_TYPE(info.nextId, uint32_t);
            uint8_t nHow = 0;
            READ_TYPE(nHow, uint8_t);
            info.nextHow = static_cast<NavTraverseType>(nHow);
            area->GetApproachInfo().push_back(info);
        }

        // Encounter paths
        uint32_t encounterCount = 0;
        READ_TYPE(encounterCount, uint32_t);
        area->GetSpotEncounters().reserve(encounterCount);

        for (uint32_t e = 0; e < encounterCount; e++) {
            NavSpotEncounter enc;
            READ_TYPE(enc.fromId, uint32_t);
            uint8_t fDir = 0;
            READ_TYPE(fDir, uint8_t);
            enc.fromDir = static_cast<NavDirType>(fDir);

            READ_TYPE(enc.toId, uint32_t);
            uint8_t tDir = 0;
            READ_TYPE(tDir, uint8_t);
            enc.toDir = static_cast<NavDirType>(tDir);

            uint8_t spotCount = 0;
            READ_TYPE(spotCount, uint8_t);
            enc.spotList.reserve(spotCount);

            for (uint8_t s = 0; s < spotCount; s++) {
                NavSpotOrder order;
                READ_TYPE(order.id, uint32_t);
                uint8_t st = 0;
                READ_TYPE(st, uint8_t);
                order.t = static_cast<float>(st) / 255.0f;
                enc.spotList.push_back(order);
            }

            area->GetSpotEncounters().push_back(enc);
        }

        // Place ID (v5+)
        if (m_version >= NAV_VERSION_5) {
            uint16_t placeId = 0;
            READ_TYPE(placeId, uint16_t);
            area->SetPlace(placeId);
            area->SetPlaceName(GetPlaceName(placeId));
        }
    }

    #undef READ_RAW
    #undef READ_TYPE

    // Initialize spatial grid
    m_grid.Initialize(minX, maxX, minY, maxY, 300.0f);
    for (NavArea* area : m_areas) {
        m_grid.AddArea(area);
    }

    PostLoad();
    m_loaded = true;
    return true;
}

bool NavMesh::PostLoad() {
    for (NavArea* area : m_areas) {
        // Resolve adjacent connections
        for (int d = 0; d < NUM_NAV_DIRECTIONS; d++) {
            auto& list = area->GetAdjacentList(static_cast<NavDirType>(d));
            for (auto& conn : list) {
                conn.area = m_grid.GetAreaByID(conn.id);
            }
        }

        // Resolve approach areas
        for (auto& app : area->GetApproachInfo()) {
            app.here = m_grid.GetAreaByID(app.hereId);
            app.prev = m_grid.GetAreaByID(app.prevId);
            app.next = m_grid.GetAreaByID(app.nextId);
        }

        // Resolve spot encounters
        for (auto& enc : area->GetSpotEncounters()) {
            enc.from = m_grid.GetAreaByID(enc.fromId);
            enc.to = m_grid.GetAreaByID(enc.toId);
        }
    }

    return true;
}

bool NavMesh::Save(const std::string& filepath) const {
    if (!m_loaded) return false;

    std::ofstream file(filepath, std::ios::binary);
    if (!file.is_open()) return false;

    uint32_t magic = NAV_MAGIC_NUMBER;
    file.write(reinterpret_cast<const char*>(&magic), sizeof(uint32_t));

    uint32_t version = NAV_VERSION_5;
    file.write(reinterpret_cast<const char*>(&version), sizeof(uint32_t));

    file.write(reinterpret_cast<const char*>(&m_bspSize), sizeof(uint32_t));

    // Place directory
    uint16_t placeCount = static_cast<uint16_t>(m_placeNames.size());
    file.write(reinterpret_cast<const char*>(&placeCount), sizeof(uint16_t));

    for (const auto& name : m_placeNames) {
        uint16_t len = static_cast<uint16_t>(name.size() + 1);
        file.write(reinterpret_cast<const char*>(&len), sizeof(uint16_t));
        file.write(name.c_str(), len);
    }

    // Area count
    uint32_t areaCount = static_cast<uint32_t>(m_areas.size());
    file.write(reinterpret_cast<const char*>(&areaCount), sizeof(uint32_t));

    for (const NavArea* area : m_areas) {
        uint32_t id = area->GetID();
        file.write(reinterpret_cast<const char*>(&id), sizeof(uint32_t));

        uint8_t flags = area->GetAttributes();
        file.write(reinterpret_cast<const char*>(&flags), sizeof(uint8_t));

        const NavExtent& ext = area->GetExtent();
        file.write(reinterpret_cast<const char*>(&ext.lo), 3 * sizeof(float));
        file.write(reinterpret_cast<const char*>(&ext.hi), 3 * sizeof(float));

        float neZ = area->GetNEZ();
        float swZ = area->GetSWZ();
        file.write(reinterpret_cast<const char*>(&neZ), sizeof(float));
        file.write(reinterpret_cast<const char*>(&swZ), sizeof(float));

        for (int d = 0; d < NUM_NAV_DIRECTIONS; d++) {
            const auto& list = area->GetAdjacentList(static_cast<NavDirType>(d));
            uint32_t count = static_cast<uint32_t>(list.size());
            file.write(reinterpret_cast<const char*>(&count), sizeof(uint32_t));

            for (const auto& conn : list) {
                uint32_t cid = conn.area ? conn.area->GetID() : conn.id;
                file.write(reinterpret_cast<const char*>(&cid), sizeof(uint32_t));
            }
        }

        const auto& hiding = area->GetHidingSpots();
        uint8_t hCount = static_cast<uint8_t>(std::min(size_t(255), hiding.size()));
        file.write(reinterpret_cast<const char*>(&hCount), sizeof(uint8_t));

        for (uint8_t h = 0; h < hCount; h++) {
            file.write(reinterpret_cast<const char*>(&hiding[h].id), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&hiding[h].pos), 3 * sizeof(float));
            file.write(reinterpret_cast<const char*>(&hiding[h].flags), sizeof(uint8_t));
        }

        const auto& approach = area->GetApproachInfo();
        uint8_t aCount = static_cast<uint8_t>(std::min(size_t(255), approach.size()));
        file.write(reinterpret_cast<const char*>(&aCount), sizeof(uint8_t));

        for (uint8_t a = 0; a < aCount; a++) {
            uint32_t hId = approach[a].here ? approach[a].here->GetID() : approach[a].hereId;
            uint32_t pId = approach[a].prev ? approach[a].prev->GetID() : approach[a].prevId;
            uint8_t pHow = static_cast<uint8_t>(approach[a].prevHow);
            uint32_t nId = approach[a].next ? approach[a].next->GetID() : approach[a].nextId;
            uint8_t nHow = static_cast<uint8_t>(approach[a].nextHow);

            file.write(reinterpret_cast<const char*>(&hId), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&pId), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&pHow), sizeof(uint8_t));
            file.write(reinterpret_cast<const char*>(&nId), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&nHow), sizeof(uint8_t));
        }

        const auto& encounters = area->GetSpotEncounters();
        uint32_t eCount = static_cast<uint32_t>(encounters.size());
        file.write(reinterpret_cast<const char*>(&eCount), sizeof(uint32_t));

        for (const auto& enc : encounters) {
            uint32_t fId = enc.from ? enc.from->GetID() : enc.fromId;
            uint8_t fDir = static_cast<uint8_t>(enc.fromDir);
            uint32_t tId = enc.to ? enc.to->GetID() : enc.toId;
            uint8_t tDir = static_cast<uint8_t>(enc.toDir);

            file.write(reinterpret_cast<const char*>(&fId), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&fDir), sizeof(uint8_t));
            file.write(reinterpret_cast<const char*>(&tId), sizeof(uint32_t));
            file.write(reinterpret_cast<const char*>(&tDir), sizeof(uint8_t));

            uint8_t sCount = static_cast<uint8_t>(std::min(size_t(255), enc.spotList.size()));
            file.write(reinterpret_cast<const char*>(&sCount), sizeof(uint8_t));

            for (uint8_t s = 0; s < sCount; s++) {
                uint32_t sId = enc.spotList[s].id;
                uint8_t st = static_cast<uint8_t>(enc.spotList[s].t * 255.0f);
                file.write(reinterpret_cast<const char*>(&sId), sizeof(uint32_t));
                file.write(reinterpret_cast<const char*>(&st), sizeof(uint8_t));
            }
        }

        uint16_t pId = area->GetPlace();
        file.write(reinterpret_cast<const char*>(&pId), sizeof(uint16_t));
    }

    return true;
}

void NavMesh::BuildLadders(const BSPFile* bsp) {
    ClearLadders();
    if (!bsp || !bsp->IsLoaded() || !m_loaded) return;

    // Find all func_ladder entities in map
    auto ladderEntities = bsp->FindEntities("func_ladder");
    uint32_t nextLadderId = 1;

    for (const BSPEntity* ent : ladderEntities) {
        std::string modelStr = ent->GetString("model");
        if (modelStr.empty() || modelStr[0] != '*') continue;

        int modelIdx = std::atoi(modelStr.c_str() + 1);
        const dmodel_t* mod = bsp->GetModel(modelIdx);
        if (!mod) continue;

        NavLadder* ladder = new NavLadder();
        ladder->id = nextLadderId++;
        ladder->top = Vector3((mod->mins.x + mod->maxs.x) * 0.5f,
                              (mod->mins.y + mod->maxs.y) * 0.5f,
                              mod->maxs.z);
        ladder->bottom = Vector3((mod->mins.x + mod->maxs.x) * 0.5f,
                                 (mod->mins.y + mod->maxs.y) * 0.5f,
                                 mod->mins.z);
        ladder->length = mod->maxs.z - mod->mins.z;
        ladder->width = std::max(mod->maxs.x - mod->mins.x, mod->maxs.y - mod->mins.y);
        ladder->dir = (mod->maxs.x - mod->mins.x >= mod->maxs.y - mod->mins.y) ? NAV_DIR_NORTH : NAV_DIR_EAST;

        // Find closest areas near top and bottom
        ladder->topForwardArea = m_grid.GetNearestArea(ladder->top, 200.0f);
        ladder->bottomArea = m_grid.GetNearestArea(ladder->bottom, 200.0f);

        m_ladders.push_back(ladder);
    }
}

NavArea* NavMesh::CreateArea(const NavExtent& extent, float neZ, float swZ) {
    uint32_t newId = 1;
    for (const NavArea* a : m_areas) {
        if (a && a->GetID() >= newId) {
            newId = a->GetID() + 1;
        }
    }
    NavArea* area = new NavArea(newId);
    area->SetExtent(extent);
    area->SetCornerHeights(neZ, swZ);
    m_areas.push_back(area);
    m_grid.AddArea(area);
    return area;
}

bool NavMesh::RemoveArea(uint32_t areaId) {
    auto it = std::find_if(m_areas.begin(), m_areas.end(), [areaId](NavArea* a) {
        return a && a->GetID() == areaId;
    });
    if (it == m_areas.end()) return false;
    NavArea* area = *it;

    for (NavArea* other : m_areas) {
        if (other && other != area) {
            other->Disconnect(area);
        }
    }

    m_grid.RemoveArea(area);
    m_areas.erase(it);
    delete area;
    return true;
}

NavArea* NavMesh::DuplicateArea(uint32_t sourceId, const Vector3& offset) {
    NavArea* src = GetAreaByID(sourceId);
    if (!src) return nullptr;

    NavExtent ext = src->GetExtent();
    ext.lo += offset;
    ext.hi += offset;
    float neZ = src->GetNEZ() + offset.z;
    float swZ = src->GetSWZ() + offset.z;

    NavArea* copy = CreateArea(ext, neZ, swZ);
    copy->SetAttributes(src->GetAttributes());
    copy->SetPlace(src->GetPlace());
    copy->SetPlaceName(src->GetPlaceName());
    return copy;
}

bool NavMesh::ConnectAreas(uint32_t fromId, uint32_t toId, bool bidirectional, int explicitDir) {
    NavArea* from = GetAreaByID(fromId);
    NavArea* to = GetAreaByID(toId);
    if (!from || !to || from == to) return false;

    NavDirType dirFrom;
    if (explicitDir >= 0 && explicitDir < NUM_NAV_DIRECTIONS) {
        dirFrom = static_cast<NavDirType>(explicitDir);
    } else {
        Vector3 delta = to->GetCenter() - from->GetCenter();
        if (std::abs(delta.x) > std::abs(delta.y)) {
            dirFrom = (delta.x > 0) ? NAV_DIR_EAST : NAV_DIR_WEST;
        } else {
            dirFrom = (delta.y > 0) ? NAV_DIR_NORTH : NAV_DIR_SOUTH;
        }
    }

    from->ConnectTo(to, dirFrom);

    if (bidirectional) {
        NavDirType dirTo = static_cast<NavDirType>((dirFrom + 2) % 4);
        to->ConnectTo(from, dirTo);
    }
    return true;
}

bool NavMesh::DisconnectAreas(uint32_t fromId, uint32_t toId, bool bidirectional) {
    NavArea* from = GetAreaByID(fromId);
    NavArea* to = GetAreaByID(toId);
    if (!from || !to) return false;

    from->Disconnect(to);
    if (bidirectional) {
        to->Disconnect(from);
    }
    return true;
}

void NavMesh::RebuildGrid(float cellSize) {
    m_grid.Reset();
    if (m_areas.empty()) {
        m_grid.Initialize(0.0f, 0.0f, 0.0f, 0.0f, cellSize);
        return;
    }

    float minX = 999999.0f, minY = 999999.0f;
    float maxX = -999999.0f, maxY = -999999.0f;

    for (NavArea* area : m_areas) {
        if (!area) continue;
        const auto& ext = area->GetExtent();
        minX = std::min(minX, ext.lo.x);
        minY = std::min(minY, ext.lo.y);
        maxX = std::max(maxX, ext.hi.x);
        maxY = std::max(maxY, ext.hi.y);
    }

    m_grid.Initialize(minX, maxX, minY, maxY, cellSize);
    for (NavArea* area : m_areas) {
        if (area) {
            m_grid.AddArea(area);
        }
    }
}

