#include "nav_area.h"
#include <algorithm>
#include <cmath>

NavArea::NavArea()
    : m_id(0), m_attributeFlags(0),
      m_extent(), m_center(),
      m_neZ(0.0f), m_swZ(0.0f),
      m_place(UNDEFINED_PLACE), m_placeName(""),
      m_totalCost(0.0f), m_costSoFar(0.0f),
      m_parent(nullptr), m_parentHow(NAV_TRAVERSE_NORTH),
      m_marker(0), m_openMarker(0) {
}

NavArea::NavArea(uint32_t id)
    : m_id(id), m_attributeFlags(0),
      m_extent(), m_center(),
      m_neZ(0.0f), m_swZ(0.0f),
      m_place(UNDEFINED_PLACE), m_placeName(""),
      m_totalCost(0.0f), m_costSoFar(0.0f),
      m_parent(nullptr), m_parentHow(NAV_TRAVERSE_NORTH),
      m_marker(0), m_openMarker(0) {
}

void NavArea::SetExtent(const NavExtent& extent) {
    m_extent = extent;
    m_center.x = (m_extent.lo.x + m_extent.hi.x) * 0.5f;
    m_center.y = (m_extent.lo.y + m_extent.hi.y) * 0.5f;
    m_center.z = (m_extent.lo.z + m_extent.hi.z) * 0.5f;
}

float NavArea::GetZ(float x, float y) const {
    float dx = m_extent.hi.x - m_extent.lo.x;
    float dy = m_extent.hi.y - m_extent.lo.y;

    if (dx <= 0.0001f || dy <= 0.0001f) {
        return m_center.z;
    }

    float u = (x - m_extent.lo.x) / dx;
    float v = (y - m_extent.lo.y) / dy;

    u = std::max(0.0f, std::min(1.0f, u));
    v = std::max(0.0f, std::min(1.0f, v));

    // Corner heights:
    // NW (u=0, v=0) -> lo.z
    // NE (u=1, v=0) -> neZ
    // SE (u=1, v=1) -> hi.z
    // SW (u=0, v=1) -> swZ
    float zNW = m_extent.lo.z;
    float zNE = m_neZ;
    float zSE = m_extent.hi.z;
    float zSW = m_swZ;

    return (1.0f - u) * (1.0f - v) * zNW +
           u * (1.0f - v) * zNE +
           u * v * zSE +
           (1.0f - u) * v * zSW;
}

bool NavArea::Contains(const Vector3& pos, float maxZDelta) const {
    if (!Contains2D(pos)) {
        return false;
    }

    float areaZ = GetZ(pos.x, pos.y);
    return std::fabs(pos.z - areaZ) <= maxZDelta;
}

Vector3 NavArea::GetClosestPoint(const Vector3& pos) const {
    Vector3 closest;
    closest.x = std::max(m_extent.lo.x, std::min(m_extent.hi.x, pos.x));
    closest.y = std::max(m_extent.lo.y, std::min(m_extent.hi.y, pos.y));
    closest.z = GetZ(closest.x, closest.y);
    return closest;
}

float NavArea::GetDistanceSquaredToPoint(const Vector3& pos) const {
    Vector3 close = GetClosestPoint(pos);
    return close.DistToSq(pos);
}

Vector3 NavArea::GetCorner(NavCornerType corner) const {
    switch (corner) {
        case NAV_CORNER_NORTH_WEST:
            return Vector3(m_extent.lo.x, m_extent.lo.y, m_extent.lo.z);
        case NAV_CORNER_NORTH_EAST:
            return Vector3(m_extent.hi.x, m_extent.lo.y, m_neZ);
        case NAV_CORNER_SOUTH_EAST:
            return Vector3(m_extent.hi.x, m_extent.hi.y, m_extent.hi.z);
        case NAV_CORNER_SOUTH_WEST:
            return Vector3(m_extent.lo.x, m_extent.hi.y, m_swZ);
        default:
            return m_center;
    }
}

NavArea* NavArea::GetAdjacentArea(NavDirType dir, size_t index) const {
    if (index >= m_connect[dir].size()) return nullptr;
    return m_connect[dir][index].area;
}

bool NavArea::IsConnected(const NavArea* area, int dir) const {
    if (!area) return false;

    if (dir >= 0 && dir < NUM_NAV_DIRECTIONS) {
        for (const auto& conn : m_connect[dir]) {
            if (conn.area == area || conn.id == area->GetID()) return true;
        }
        return false;
    }

    for (int d = 0; d < NUM_NAV_DIRECTIONS; d++) {
        for (const auto& conn : m_connect[d]) {
            if (conn.area == area || conn.id == area->GetID()) return true;
        }
    }
    return false;
}

void NavArea::ConnectTo(NavArea* area, NavDirType dir) {
    if (!area || IsConnected(area, dir)) return;
    m_connect[dir].push_back(NavConnect(area->GetID(), area));
}

void NavArea::Disconnect(NavArea* area) {
    if (!area) return;
    for (int d = 0; d < NUM_NAV_DIRECTIONS; d++) {
        auto& list = m_connect[d];
        list.erase(std::remove_if(list.begin(), list.end(), [area](const NavConnect& c) {
            return c.area == area || c.id == area->GetID();
        }), list.end());
    }
}

void NavArea::ComputePortal(const NavArea* toArea, NavDirType dir, Vector3* outCenter, float* outHalfWidth) const {
    if (!outCenter) return;

    if (!toArea) {
        *outCenter = m_center;
        if (outHalfWidth) *outHalfWidth = 0.0f;
        return;
    }

    if (dir == NAV_DIR_NORTH || dir == NAV_DIR_SOUTH) {
        float edgeY = (dir == NAV_DIR_NORTH) ? m_extent.lo.y : m_extent.hi.y;
        float left = std::max(m_extent.lo.x, toArea->m_extent.lo.x);
        float right = std::min(m_extent.hi.x, toArea->m_extent.hi.x);

        if (left > right) {
            std::swap(left, right);
        }

        outCenter->x = (left + right) * 0.5f;
        outCenter->y = edgeY;
        outCenter->z = GetZ(outCenter->x, outCenter->y);

        if (outHalfWidth) {
            *outHalfWidth = std::max(0.0f, (right - left) * 0.5f);
        }
    } else { // EAST or WEST
        float edgeX = (dir == NAV_DIR_EAST) ? m_extent.hi.x : m_extent.lo.x;
        float top = std::max(m_extent.lo.y, toArea->m_extent.lo.y);
        float bottom = std::min(m_extent.hi.y, toArea->m_extent.hi.y);

        if (top > bottom) {
            std::swap(top, bottom);
        }

        outCenter->x = edgeX;
        outCenter->y = (top + bottom) * 0.5f;
        outCenter->z = GetZ(outCenter->x, outCenter->y);

        if (outHalfWidth) {
            *outHalfWidth = std::max(0.0f, (bottom - top) * 0.5f);
        }
    }
}

bool NavArea::IsOverlapping(const NavArea* area) const {
    if (!area) return false;
    return (m_extent.lo.x < area->m_extent.hi.x && m_extent.hi.x > area->m_extent.lo.x &&
            m_extent.lo.y < area->m_extent.hi.y && m_extent.hi.y > area->m_extent.lo.y);
}
