#pragma once

#include <vector>
#include <string>
#include <cstdint>
#include "nav_types.h"

class NavArea {
public:
    NavArea();
    explicit NavArea(uint32_t id);

    uint32_t GetID() const { return m_id; }
    void SetID(uint32_t id) { m_id = id; }

    uint8_t GetAttributes() const { return m_attributeFlags; }
    void SetAttributes(uint8_t attr) { m_attributeFlags = attr; }
    bool HasAttributes(uint8_t attr) const { return (m_attributeFlags & attr) != 0; }

    const NavExtent& GetExtent() const { return m_extent; }
    void SetExtent(const NavExtent& extent);

    const Vector3& GetCenter() const { return m_center; }

    float GetNEZ() const { return m_neZ; }
    float GetSWZ() const { return m_swZ; }
    void SetCornerHeights(float neZ, float swZ) { m_neZ = neZ; m_swZ = swZ; }

    uint16_t GetPlace() const { return m_place; }
    void SetPlace(uint16_t place) { m_place = place; }

    const std::string& GetPlaceName() const { return m_placeName; }
    void SetPlaceName(const std::string& name) { m_placeName = name; }

    // Geometry queries
    float GetZ(float x, float y) const;
    float GetZ(const Vector3& pos) const { return GetZ(pos.x, pos.y); }
    bool Contains(const Vector3& pos, float maxZDelta = 40.0f) const;
    bool Contains2D(const Vector3& pos) const { return m_extent.Contains2D(pos); }
    Vector3 GetClosestPoint(const Vector3& pos) const;
    float GetDistanceSquaredToPoint(const Vector3& pos) const;
    Vector3 GetCorner(NavCornerType corner) const;
    float GetWidth() const { return m_extent.hi.x - m_extent.lo.x; }
    float GetLength() const { return m_extent.hi.y - m_extent.lo.y; }

    // Connections
    const std::vector<NavConnect>& GetAdjacentList(NavDirType dir) const { return m_connect[dir]; }
    std::vector<NavConnect>& GetAdjacentList(NavDirType dir) { return m_connect[dir]; }
    size_t GetAdjacentCount(NavDirType dir) const { return m_connect[dir].size(); }
    size_t GetConnectionCount() const {
        size_t count = 0;
        for (int d = 0; d < NUM_NAV_DIRECTIONS; ++d) {
            count += m_connect[d].size();
        }
        return count;
    }
    NavArea* GetAdjacentArea(NavDirType dir, size_t index) const;
    bool IsConnected(const NavArea* area, int dir = -1) const;
    void ConnectTo(NavArea* area, NavDirType dir);
    void Disconnect(NavArea* area);

    // Portals
    void ComputePortal(const NavArea* toArea, NavDirType dir, Vector3* outCenter, float* outHalfWidth) const;

    // Hiding spots
    const std::vector<NavHidingSpot>& GetHidingSpots() const { return m_hidingSpots; }
    std::vector<NavHidingSpot>& GetHidingSpots() { return m_hidingSpots; }

    // Approach areas
    const std::vector<NavApproachInfo>& GetApproachInfo() const { return m_approach; }
    std::vector<NavApproachInfo>& GetApproachInfo() { return m_approach; }

    // Encounters
    const std::vector<NavSpotEncounter>& GetSpotEncounters() const { return m_spotEncounters; }
    std::vector<NavSpotEncounter>& GetSpotEncounters() { return m_spotEncounters; }

    // Overlaps
    bool IsOverlapping(const NavArea* area) const;
    const std::vector<NavArea*>& GetOverlapList() const { return m_overlapList; }
    void AddOverlap(NavArea* area) { m_overlapList.push_back(area); }

    // Pathfinding state
    float GetTotalCost() const { return m_totalCost; }
    void SetTotalCost(float c) { m_totalCost = c; }

    float GetCostSoFar() const { return m_costSoFar; }
    void SetCostSoFar(float c) { m_costSoFar = c; }

    NavArea* GetParent() const { return m_parent; }
    void SetParent(NavArea* parent, NavTraverseType how) { m_parent = parent; m_parentHow = how; }
    NavTraverseType GetParentHow() const { return m_parentHow; }

    uint32_t GetMarker() const { return m_marker; }
    void SetMarker(uint32_t marker) { m_marker = marker; }

    uint32_t GetOpenMarker() const { return m_openMarker; }
    void SetOpenMarker(uint32_t marker) { m_openMarker = marker; }

public:
    uint32_t m_id;
    uint8_t m_attributeFlags;
    NavExtent m_extent;
    Vector3 m_center;
    float m_neZ;
    float m_swZ;
    uint16_t m_place;
    std::string m_placeName;

    std::vector<NavConnect> m_connect[NUM_NAV_DIRECTIONS];
    std::vector<NavHidingSpot> m_hidingSpots;
    std::vector<NavApproachInfo> m_approach;
    std::vector<NavSpotEncounter> m_spotEncounters;
    std::vector<NavArea*> m_overlapList;

    float m_totalCost;
    float m_costSoFar;
    NavArea* m_parent;
    NavTraverseType m_parentHow;
    uint32_t m_marker;
    uint32_t m_openMarker;
};
