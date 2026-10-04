#pragma once

#include <vector>
#include <unordered_map>
#include <cstdint>
#include "nav_types.h"
#include "nav_area.h"

class NavGrid {
public:
    NavGrid();
    ~NavGrid();

    void Initialize(float minX, float maxX, float minY, float maxY, float cellSize = 300.0f);
    void Reset();

    void AddArea(NavArea* area);
    void RemoveArea(NavArea* area);

    NavArea* GetAreaByID(uint32_t id) const;
    NavArea* GetAreaAtPoint(const Vector3& pos, float maxZDelta = 40.0f) const;
    NavArea* GetNearestArea(const Vector3& pos, float maxDist = 1000.0f, bool checkZ = true) const;

    float GetCellSize() const { return m_cellSize; }
    int GetGridSizeX() const { return m_gridSizeX; }
    int GetGridSizeY() const { return m_gridSizeY; }

private:
    int WorldToGridX(float x) const;
    int WorldToGridY(float y) const;
    int GridToIndex(int x, int y) const;

private:
    float m_minX;
    float m_minY;
    float m_maxX;
    float m_maxY;
    float m_cellSize;

    int m_gridSizeX;
    int m_gridSizeY;

    std::vector<std::vector<NavArea*>> m_cells;
    std::unordered_map<uint32_t, NavArea*> m_idMap;
};
