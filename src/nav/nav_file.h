#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "nav_types.h"
#include "nav_area.h"
#include "nav_grid.h"

class BSPFile;

class NavMesh {
public:
    NavMesh();
    ~NavMesh();

    bool Load(const std::string& filepath);
    bool LoadFromMemory(const uint8_t* buffer, size_t size);
    void Unload();
    bool IsLoaded() const { return m_loaded; }

    bool Save(const std::string& filepath) const;

    size_t GetAreaCount() const { return m_areas.size(); }
    NavArea* GetArea(size_t index) const;
    NavArea* GetAreaByID(uint32_t id) const { return m_grid.GetAreaByID(id); }
    NavArea* GetNearestArea(const Vector3& pos, float maxDist = 1000.0f) const {
        return m_grid.GetNearestArea(pos, maxDist);
    }
    NavArea* GetAreaAtPoint(const Vector3& pos) const {
        return m_grid.GetAreaAtPoint(pos);
    }

    const std::vector<NavArea*>& GetAreas() const { return m_areas; }
    const NavGrid& GetGrid() const { return m_grid; }
    NavGrid& GetGrid() { return m_grid; }

    uint32_t GetVersion() const { return m_version; }
    void SetVersion(uint32_t version) { m_version = version; }
    uint32_t GetBspSize() const { return m_bspSize; }
    void SetBspSize(uint32_t size) { m_bspSize = size; }
    void SetLoaded(bool loaded) { m_loaded = loaded; }

    const std::vector<std::string>& GetPlaceNames() const { return m_placeNames; }
    std::string GetPlaceName(uint16_t placeId) const;
    void RebuildGrid(float cellSize = 300.0f);

    void BuildLadders(const BSPFile* bsp);
    const std::vector<NavLadder*>& GetLadders() const { return m_ladders; }
    std::vector<NavLadder*>& GetLadders() { return m_ladders; }
    NavLadder* GetLadderByID(uint32_t id) const;
    NavLadder* CreateLadder(const Vector3& top, const Vector3& bottom, float width, NavDirType dir);
    bool RemoveLadder(uint32_t id);
    void ClearLadders();

    NavArea* CreateArea(const NavExtent& extent, float neZ, float swZ);
    bool RemoveArea(uint32_t id);
    NavArea* DuplicateArea(uint32_t sourceId, const Vector3& offset = Vector3(32.0f, 32.0f, 0.0f));
    bool ConnectAreas(uint32_t fromId, uint32_t toId, bool bidirectional = true, int explicitDir = -1);
    bool DisconnectAreas(uint32_t fromId, uint32_t toId, bool bidirectional = false);

private:
    bool PostLoad();

private:
    bool m_loaded;
    uint32_t m_version;
    uint32_t m_bspSize;

    std::vector<NavArea*> m_areas;
    std::vector<std::string> m_placeNames;
    NavGrid m_grid;
    std::vector<NavLadder*> m_ladders;
};
