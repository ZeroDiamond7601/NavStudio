#include "nav_grid.h"
#include <algorithm>
#include <cmath>

NavGrid::NavGrid()
    : m_minX(0.0f), m_minY(0.0f),
      m_maxX(0.0f), m_maxY(0.0f),
      m_cellSize(300.0f),
      m_gridSizeX(0), m_gridSizeY(0) {
}

NavGrid::~NavGrid() {
    Reset();
}

void NavGrid::Reset() {
    m_cells.clear();
    m_idMap.clear();
    m_gridSizeX = 0;
    m_gridSizeY = 0;
}

void NavGrid::Initialize(float minX, float maxX, float minY, float maxY, float cellSize) {
    Reset();

    m_minX = minX;
    m_maxX = maxX;
    m_minY = minY;
    m_maxY = maxY;
    m_cellSize = (cellSize > 10.0f) ? cellSize : 300.0f;

    m_gridSizeX = static_cast<int>(std::ceil((m_maxX - m_minX) / m_cellSize)) + 1;
    m_gridSizeY = static_cast<int>(std::ceil((m_maxY - m_minY) / m_cellSize)) + 1;

    m_gridSizeX = std::max(1, std::min(1024, m_gridSizeX));
    m_gridSizeY = std::max(1, std::min(1024, m_gridSizeY));

    m_cells.resize(m_gridSizeX * m_gridSizeY);
}

int NavGrid::WorldToGridX(float x) const {
    if (m_gridSizeX <= 0) return 0;
    int gx = static_cast<int>((x - m_minX) / m_cellSize);
    return std::max(0, std::min(m_gridSizeX - 1, gx));
}

int NavGrid::WorldToGridY(float y) const {
    if (m_gridSizeY <= 0) return 0;
    int gy = static_cast<int>((y - m_minY) / m_cellSize);
    return std::max(0, std::min(m_gridSizeY - 1, gy));
}

int NavGrid::GridToIndex(int x, int y) const {
    return y * m_gridSizeX + x;
}

void NavGrid::AddArea(NavArea* area) {
    if (!area || m_cells.empty()) return;

    m_idMap[area->GetID()] = area;

    const NavExtent& ext = area->GetExtent();
    int minGx = WorldToGridX(ext.lo.x);
    int maxGx = WorldToGridX(ext.hi.x);
    int minGy = WorldToGridY(ext.lo.y);
    int maxGy = WorldToGridY(ext.hi.y);

    for (int gy = minGy; gy <= maxGy; gy++) {
        for (int gx = minGx; gx <= maxGx; gx++) {
            int idx = GridToIndex(gx, gy);
            if (idx >= 0 && idx < static_cast<int>(m_cells.size())) {
                auto& cell = m_cells[idx];
                if (std::find(cell.begin(), cell.end(), area) == cell.end()) {
                    cell.push_back(area);
                }
            }
        }
    }
}

void NavGrid::RemoveArea(NavArea* area) {
    if (!area || m_cells.empty()) return;

    m_idMap.erase(area->GetID());

    const NavExtent& ext = area->GetExtent();
    int minGx = WorldToGridX(ext.lo.x);
    int maxGx = WorldToGridX(ext.hi.x);
    int minGy = WorldToGridY(ext.lo.y);
    int maxGy = WorldToGridY(ext.hi.y);

    for (int gy = minGy; gy <= maxGy; gy++) {
        for (int gx = minGx; gx <= maxGx; gx++) {
            int idx = GridToIndex(gx, gy);
            if (idx >= 0 && idx < static_cast<int>(m_cells.size())) {
                auto& cell = m_cells[idx];
                cell.erase(std::remove(cell.begin(), cell.end(), area), cell.end());
            }
        }
    }
}

NavArea* NavGrid::GetAreaByID(uint32_t id) const {
    auto it = m_idMap.find(id);
    if (it != m_idMap.end()) {
        return it->second;
    }
    return nullptr;
}

NavArea* NavGrid::GetAreaAtPoint(const Vector3& pos, float maxZDelta) const {
    if (m_cells.empty()) return nullptr;

    int gx = WorldToGridX(pos.x);
    int gy = WorldToGridY(pos.y);
    int idx = GridToIndex(gx, gy);

    if (idx < 0 || idx >= static_cast<int>(m_cells.size())) return nullptr;

    const auto& cell = m_cells[idx];
    NavArea* bestArea = nullptr;
    float bestZDelta = maxZDelta;

    for (NavArea* area : cell) {
        if (area->Contains2D(pos)) {
            float areaZ = area->GetZ(pos.x, pos.y);
            float dz = std::fabs(pos.z - areaZ);
            if (dz <= bestZDelta) {
                bestZDelta = dz;
                bestArea = area;
            }
        }
    }

    return bestArea;
}

NavArea* NavGrid::GetNearestArea(const Vector3& pos, float maxDist, bool checkZ) const {
    if (m_cells.empty()) return nullptr;

    // First try direct hit
    NavArea* direct = GetAreaAtPoint(pos, 50.0f);
    if (direct) return direct;

    int centerGx = WorldToGridX(pos.x);
    int centerGy = WorldToGridY(pos.y);

    int searchRadiusCells = static_cast<int>(std::ceil(maxDist / m_cellSize));
    searchRadiusCells = std::max(1, std::min(10, searchRadiusCells));

    float bestDistSq = maxDist * maxDist;
    NavArea* bestArea = nullptr;

    for (int r = 0; r <= searchRadiusCells; r++) {
        int minGx = std::max(0, centerGx - r);
        int maxGx = std::min(m_gridSizeX - 1, centerGx + r);
        int minGy = std::max(0, centerGy - r);
        int maxGy = std::min(m_gridSizeY - 1, centerGy + r);

        for (int gy = minGy; gy <= maxGy; gy++) {
            for (int gx = minGx; gx <= maxGx; gx++) {
                // If r > 0, only process the perimeter ring
                if (r > 0 && (gx > minGx && gx < maxGx && gy > minGy && gy < maxGy)) {
                    continue;
                }

                int idx = GridToIndex(gx, gy);
                const auto& cell = m_cells[idx];

                for (NavArea* area : cell) {
                    Vector3 closePoint = area->GetClosestPoint(pos);
                    float dSq = closePoint.DistToSq(pos);

                    if (checkZ) {
                        float dz = std::fabs(pos.z - closePoint.z);
                        if (dz > 120.0f) {
                            // Penalize height differences if too steep
                            dSq += dz * dz * 2.0f;
                        }
                    }

                    if (dSq < bestDistSq) {
                        bestDistSq = dSq;
                        bestArea = area;
                    }
                }
            }
        }

        // If found within this ring, we can stop if distance is within cell boundary
        if (bestArea && bestDistSq <= (r * m_cellSize) * (r * m_cellSize)) {
            break;
        }
    }

    return bestArea;
}
