#include "nav_path.h"
#include "../bsp/bsp_file.h"
#include <queue>
#include <algorithm>
#include <cmath>

uint32_t NavPathFinder::s_masterMarker = 1;
std::mutex NavPathFinder::s_pathfinderMutex;

NavPath::NavPath() {
}

void NavPath::Clear() {
    m_segments.clear();
}

const NavPathSegment* NavPath::GetSegment(size_t index) const {
    if (index >= m_segments.size()) return nullptr;
    return &m_segments[index];
}

float NavPath::GetLength() const {
    if (m_segments.size() < 2) return 0.0f;

    float total = 0.0f;
    for (size_t i = 1; i < m_segments.size(); i++) {
        total += m_segments[i].pos.DistTo(m_segments[i - 1].pos);
    }
    return total;
}

const Vector3& NavPath::GetStart() const {
    static Vector3 s_zero(0, 0, 0);
    return m_segments.empty() ? s_zero : m_segments.front().pos;
}

const Vector3& NavPath::GetEnd() const {
    static Vector3 s_zero(0, 0, 0);
    return m_segments.empty() ? s_zero : m_segments.back().pos;
}

bool NavPath::GetPointAlongPath(float dist, Vector3* outPoint) const {
    if (!outPoint || m_segments.empty()) return false;
    if (m_segments.size() == 1 || dist <= 0.0f) {
        *outPoint = m_segments.front().pos;
        return true;
    }

    float accumulated = 0.0f;
    for (size_t i = 1; i < m_segments.size(); i++) {
        float segLen = m_segments[i].pos.DistTo(m_segments[i - 1].pos);
        if (accumulated + segLen >= dist) {
            float t = (segLen > 0.0001f) ? ((dist - accumulated) / segLen) : 0.0f;
            t = std::max(0.0f, std::min(1.0f, t));
            *outPoint = m_segments[i - 1].pos + (m_segments[i].pos - m_segments[i - 1].pos) * t;
            return true;
        }
        accumulated += segLen;
    }

    *outPoint = m_segments.back().pos;
    return true;
}

struct AreaCostCompare {
    inline bool operator()(const NavArea* a, const NavArea* b) const {
        return a->GetTotalCost() > b->GetTotalCost();
    }
};

bool NavPathFinder::BuildPath(
    const NavGrid& grid,
    const Vector3& startPos,
    const Vector3& goalPos,
    NavPath& outPath,
    int flags,
    const BSPFile* bsp
) {
    NavArea* startArea = grid.GetNearestArea(startPos, 1000.0f);
    if (!startArea) return false;

    NavArea* goalArea = grid.GetNearestArea(goalPos, 1000.0f);
    if (!goalArea) return false;

    return BuildPathBetweenAreas(startArea, goalArea, startPos, goalPos, outPath, flags, bsp);
}

bool NavPathFinder::BuildPathBetweenAreas(
    NavArea* startArea,
    NavArea* goalArea,
    const Vector3& startPos,
    const Vector3& goalPos,
    NavPath& outPath,
    int flags,
    const BSPFile* bsp
) {
    outPath.Clear();
    if (!startArea || !goalArea) return false;

    std::lock_guard<std::mutex> lock(s_pathfinderMutex);

    // Trivial case: within the same area
    if (startArea == goalArea) {
        outPath.AddSegment(NavPathSegment(startArea, NAV_TRAVERSE_NORTH, startPos));
        outPath.AddSegment(NavPathSegment(goalArea, NAV_TRAVERSE_NORTH, goalPos));
        return true;
    }

    uint32_t marker = ++s_masterMarker;

    // Priority queue for A* open list
    std::priority_queue<NavArea*, std::vector<NavArea*>, AreaCostCompare> openList;

    startArea->SetParent(nullptr, NAV_TRAVERSE_NORTH);
    startArea->SetCostSoFar(0.0f);
    startArea->SetTotalCost(startArea->GetCenter().DistTo(goalPos));
    startArea->SetMarker(marker);
    startArea->SetOpenMarker(marker);

    openList.push(startArea);

    bool reachedGoal = false;
    NavArea* closestArea = startArea;
    float closestDistToGoal = startArea->GetCenter().DistToSq(goalPos);

    while (!openList.empty()) {
        NavArea* current = openList.top();
        openList.pop();

        current->SetOpenMarker(0); // Popped from open list to closed

        if (current == goalArea) {
            reachedGoal = true;
            break;
        }

        float distSq = current->GetCenter().DistToSq(goalPos);
        if (distSq < closestDistToGoal) {
            closestDistToGoal = distSq;
            closestArea = current;
        }

        // Expand neighbors in 4 directions
        for (int d = 0; d < NUM_NAV_DIRECTIONS; d++) {
            NavDirType dir = static_cast<NavDirType>(d);
            const auto& adjList = current->GetAdjacentList(dir);

            for (const auto& conn : adjList) {
                NavArea* neighbor = conn.area;
                if (!neighbor) continue;

                // Height check
                float dz = neighbor->GetCenter().z - current->GetCenter().z;
                if (dz > 58.0f) {
                    // Too high to jump without a ladder
                    continue;
                }
                if (dz < -200.0f) {
                    // Fatal fall
                    continue;
                }

                float stepDist = current->GetCenter().DistTo(neighbor->GetCenter());
                float stepCost = stepDist;

                // Attribute modifiers
                if (neighbor->HasAttributes(NAV_ATTR_CROUCH) && (flags & NAV_PATH_AVOID_CROUCH)) {
                    stepCost *= 2.5f;
                }
                if (dz > 18.0f && (flags & NAV_PATH_AVOID_JUMP)) {
                    stepCost *= 2.0f;
                }
                if (flags & NAV_PATH_PREFER_SAFE) {
                    // Prefer areas that have cover / hiding spots nearby
                    if (neighbor->GetHidingSpots().empty()) {
                        stepCost *= 1.2f;
                    }
                }

                float newCostSoFar = current->GetCostSoFar() + stepCost;

                if (neighbor->GetMarker() != marker) {
                    // Unvisited
                    neighbor->SetMarker(marker);
                    neighbor->SetOpenMarker(marker);
                    neighbor->SetParent(current, static_cast<NavTraverseType>(dir));
                    neighbor->SetCostSoFar(newCostSoFar);
                    neighbor->SetTotalCost(newCostSoFar + neighbor->GetCenter().DistTo(goalPos));
                    openList.push(neighbor);
                } else if (neighbor->GetOpenMarker() == marker && newCostSoFar < neighbor->GetCostSoFar()) {
                    // Better path found to area on open list
                    neighbor->SetParent(current, static_cast<NavTraverseType>(dir));
                    neighbor->SetCostSoFar(newCostSoFar);
                    neighbor->SetTotalCost(newCostSoFar + neighbor->GetCenter().DistTo(goalPos));
                    openList.push(neighbor);
                }
            }
        }
    }

    NavArea* endpointArea = reachedGoal ? goalArea : closestArea;
    if (!endpointArea) return false;

    // Backtrack path with cycle protection
    std::vector<NavArea*> areaPath;
    std::vector<NavTraverseType> howPath;

    size_t maxAreas = 10000;
    for (NavArea* a = endpointArea; a != nullptr && areaPath.size() < maxAreas; a = a->GetParent()) {
        areaPath.push_back(a);
        howPath.push_back(a->GetParentHow());
        if (a == startArea) break;
    }

    std::reverse(areaPath.begin(), areaPath.end());
    std::reverse(howPath.begin(), howPath.end());

    if (areaPath.empty()) return false;

    // Build path segments with portal-refined waypoints
    outPath.AddSegment(NavPathSegment(startArea, NAV_TRAVERSE_NORTH, startPos));

    for (size_t i = 1; i < areaPath.size(); i++) {
        NavArea* prev = areaPath[i - 1];
        NavArea* curr = areaPath[i];
        NavTraverseType how = howPath[i];

        Vector3 waypoint;
        if (how < 4) {
            float halfWidth = 0.0f;
            prev->ComputePortal(curr, static_cast<NavDirType>(how), &waypoint, &halfWidth);
        } else {
            waypoint = curr->GetCenter();
        }

        outPath.AddSegment(NavPathSegment(curr, how, waypoint));
    }

    // Append final goal position
    outPath.AddSegment(NavPathSegment(endpointArea, NAV_TRAVERSE_NORTH, goalPos));

    // Optional smoothing if BSP collision engine is provided
    if ((flags & NAV_PATH_SMOOTH) && bsp && bsp->IsLoaded() && outPath.GetSegmentCount() > 3) {
        std::vector<NavPathSegment> smoothed;
        smoothed.push_back(outPath.GetSegments().front());

        size_t anchor = 0;
        while (anchor < outPath.GetSegmentCount() - 1) {
            size_t next = anchor + 1;
            for (size_t check = outPath.GetSegmentCount() - 1; check > anchor + 1; check--) {
                BSPTraceResult tr;
                Vector3 p1 = outPath.GetSegments()[anchor].pos;
                Vector3 p2 = outPath.GetSegments()[check].pos;
                p1.z += 18.0f; // Eye/torso level
                p2.z += 18.0f;

                if (!bsp->TraceWorld(p1, p2, HULL_POINT, &tr) || tr.fraction >= 0.99f) {
                    next = check;
                    break;
                }
            }

            smoothed.push_back(outPath.GetSegments()[next]);
            anchor = next;
        }

        outPath.Clear();
        for (const auto& s : smoothed) {
            outPath.AddSegment(s);
        }
    }

    return true;
}
