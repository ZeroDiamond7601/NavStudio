#pragma once

#include <vector>
#include <cstdint>
#include "nav_types.h"
#include "nav_area.h"
#include "nav_grid.h"

class BSPFile;

enum NavPathFlags {
    NAV_PATH_DEFAULT       = 0,
    NAV_PATH_AVOID_CROUCH  = 0x01,
    NAV_PATH_AVOID_JUMP    = 0x02,
    NAV_PATH_PREFER_SAFE   = 0x04,
    NAV_PATH_SMOOTH        = 0x08
};

class NavPath {
public:
    NavPath();

    void Clear();
    bool IsValid() const { return !m_segments.empty(); }
    size_t GetSegmentCount() const { return m_segments.size(); }
    const NavPathSegment* GetSegment(size_t index) const;

    float GetLength() const;
    const Vector3& GetStart() const;
    const Vector3& GetEnd() const;

    void AddSegment(const NavPathSegment& seg) { m_segments.push_back(seg); }
    const std::vector<NavPathSegment>& GetSegments() const { return m_segments; }

    bool GetPointAlongPath(float dist, Vector3* outPoint) const;

private:
    std::vector<NavPathSegment> m_segments;
};

#include <mutex>

class NavPathFinder {
public:
    static bool BuildPath(
        const NavGrid& grid,
        const Vector3& startPos,
        const Vector3& goalPos,
        NavPath& outPath,
        int flags = NAV_PATH_DEFAULT,
        const BSPFile* bsp = nullptr
    );

    static bool BuildPathBetweenAreas(
        NavArea* startArea,
        NavArea* goalArea,
        const Vector3& startPos,
        const Vector3& goalPos,
        NavPath& outPath,
        int flags = NAV_PATH_DEFAULT,
        const BSPFile* bsp = nullptr
    );

    static std::mutex& GetMutex() { return s_pathfinderMutex; }

private:
    static uint32_t s_masterMarker;
    static std::mutex s_pathfinderMutex;
};
