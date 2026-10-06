#include "editor/scene/scene_picker.h"
#include <cmath>
#include <limits>

Ray ScenePicker::ScreenPointToRay(
    float screenX, float screenY,
    float viewportWidth, float viewportHeight,
    const Matrix4& viewMatrix,
    const Matrix4& projMatrix
) {
    Ray ray;
    if (viewportWidth <= 0.0f || viewportHeight <= 0.0f) {
        ray.origin = Vector3(0, 0, 0);
        ray.direction = Vector3(0, 0, -1);
        return ray;
    }

    // Convert screen coordinates to Normalized Device Coordinates (NDC)
    float ndcX = (2.0f * screenX) / viewportWidth - 1.0f;
    float ndcY = 1.0f - (2.0f * screenY) / viewportHeight;

    Matrix4 viewProj = projMatrix * viewMatrix;
    Matrix4 invViewProj = viewProj.Inverse();

    // Near and far points in NDC
    Vector3 nearPointNDC(ndcX, ndcY, -1.0f);
    Vector3 farPointNDC(ndcX, ndcY, 1.0f);

    Vector3 nearWorld = invViewProj.MultiplyPoint(nearPointNDC);
    Vector3 farWorld = invViewProj.MultiplyPoint(farPointNDC);

    ray.origin = nearWorld;
    ray.direction = (farWorld - nearWorld).Normalized();
    return ray;
}

bool ScenePicker::RayIntersectsTriangle(
    const Ray& ray,
    const Vector3& v0, const Vector3& v1, const Vector3& v2,
    float& outT, Vector3& outHitPoint
) {
    Vector3 edge1 = v1 - v0;
    Vector3 edge2 = v2 - v0;
    Vector3 h = ray.direction.Cross(edge2);
    float a = edge1.Dot(h);

    if (std::abs(a) < 1e-7f) return false;

    float f = 1.0f / a;
    Vector3 s = ray.origin - v0;
    float u = f * s.Dot(h);
    if (u < 0.0f || u > 1.0f) return false;

    Vector3 q = s.Cross(edge1);
    float v = f * ray.direction.Dot(q);
    if (v < 0.0f || u + v > 1.0f) return false;

    float t = f * edge2.Dot(q);
    if (t > 1e-4f) {
        outT = t;
        outHitPoint = ray.origin + ray.direction * t;
        return true;
    }
    return false;
}

bool ScenePicker::RayIntersectsAABB(
    const Ray& ray,
    const Vector3& mins, const Vector3& maxs,
    float& outT
) {
    float tMin = 0.0f;
    float tMax = std::numeric_limits<float>::max();

    float ro[3] = { ray.origin.x, ray.origin.y, ray.origin.z };
    float rd[3] = { ray.direction.x, ray.direction.y, ray.direction.z };
    float bMin[3] = { mins.x, mins.y, mins.z };
    float bMax[3] = { maxs.x, maxs.y, maxs.z };

    for (int i = 0; i < 3; ++i) {
        if (std::abs(rd[i]) < 1e-7f) {
            if (ro[i] < bMin[i] || ro[i] > bMax[i]) return false;
        } else {
            float ood = 1.0f / rd[i];
            float t1 = (bMin[i] - ro[i]) * ood;
            float t2 = (bMax[i] - ro[i]) * ood;
            if (t1 > t2) std::swap(t1, t2);
            tMin = std::max(tMin, t1);
            tMax = std::min(tMax, t2);
            if (tMin > tMax) return false;
        }
    }

    outT = tMin;
    return true;
}

int ScenePicker::PickEntity(const EditorScene& scene, const Ray& ray, float* outT) {
    const auto& entRenderer = scene.GetEntityRenderer();
    if (!entRenderer.GetShowEntities() || !entRenderer.IsLoaded()) return -1;

    const auto& entities = entRenderer.GetEntities();
    int closestIndex = -1;
    float closestT = std::numeric_limits<float>::max();

    for (size_t i = 0; i < entities.size(); ++i) {
        const auto& ent = entities[i];
        if (!entRenderer.IsEntityVisible(ent)) continue;

        float t = 0.0f;
        if (RayIntersectsAABB(ray, ent.worldMins, ent.worldMaxs, t)) {
            if (t > 0.0f && t < closestT) {
                closestT = t;
                closestIndex = static_cast<int>(i);
            }
        }
    }

    if (closestIndex >= 0 && outT) {
        *outT = closestT;
    }
    return closestIndex;
}

uint32_t ScenePicker::PickNavArea(const EditorScene& scene, const Ray& ray, Vector3* outHitPoint) {
    if (!scene.HasNAV()) return 0;

    const auto& areas = scene.GetNAV().GetAreas();
    uint32_t closestAreaId = 0;
    float closestDist = std::numeric_limits<float>::max();
    Vector3 bestHit;

    for (const NavArea* area : areas) {
        if (!area) continue;

        Vector3 cNW = area->GetCorner(NAV_CORNER_NORTH_WEST);
        Vector3 cNE = area->GetCorner(NAV_CORNER_NORTH_EAST);
        Vector3 cSE = area->GetCorner(NAV_CORNER_SOUTH_EAST);
        Vector3 cSW = area->GetCorner(NAV_CORNER_SOUTH_WEST);

        float t = 0.0f;
        Vector3 hit;

        // Quad triangle 1: (NW, NE, SE)
        if (RayIntersectsTriangle(ray, cNW, cNE, cSE, t, hit)) {
            if (t < closestDist) {
                closestDist = t;
                closestAreaId = area->GetID();
                bestHit = hit;
            }
        }
        // Quad triangle 2: (NW, SE, SW)
        if (RayIntersectsTriangle(ray, cNW, cSE, cSW, t, hit)) {
            if (t < closestDist) {
                closestDist = t;
                closestAreaId = area->GetID();
                bestHit = hit;
            }
        }
    }

    if (closestAreaId != 0 && outHitPoint) {
        *outHitPoint = bestHit;
    }

    return closestAreaId;
}

bool ScenePicker::PickBSPFloor(const EditorScene& scene, const Ray& ray, Vector3* outHitPoint) {
    if (!scene.HasBSP()) return false;

    Vector3 end = ray.origin + ray.direction * 16384.0f;
    BSPTraceResult tr;
    if (scene.GetBSP().TraceWorld(ray.origin, end, HULL_POINT, &tr)) {
        if (outHitPoint) {
            *outHitPoint = tr.endpos;
        }
        return true;
    }

    return false;
}

struct ScreenPoint2D {
    float x;
    float y;
    bool valid;
};

static ScreenPoint2D ProjectToScreen(const Vector3& worldPos, const Matrix4& viewProj, float width, float height) {
    Vector4 clip = viewProj * Vector4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
    if (clip.w <= 0.001f) return { 0.0f, 0.0f, false };
    float ndcX = clip.x / clip.w;
    float ndcY = clip.y / clip.w;
    float sx = (ndcX * 0.5f + 0.5f) * width;
    float sy = (1.0f - (ndcY * 0.5f + 0.5f)) * height;
    return { sx, sy, true };
}

static float DistToSegment2D(float px, float py, float x1, float y1, float x2, float y2) {
    float dx = x2 - x1;
    float dy = y2 - y1;
    float lenSq = dx * dx + dy * dy;
    if (lenSq < 1e-4f) {
        float ex = px - x1, ey = py - y1;
        return std::sqrt(ex * ex + ey * ey);
    }
    float t = ((px - x1) * dx + (py - y1) * dy) / lenSq;
    t = std::max(0.0f, std::min(1.0f, t));
    float projX = x1 + t * dx;
    float projY = y1 + t * dy;
    float ex = px - projX, ey = py - projY;
    return std::sqrt(ex * ex + ey * ey);
}

SelectedHandleType ScenePicker::PickAreaHandles(
    const EditorScene& scene,
    float screenX, float screenY,
    float viewportWidth, float viewportHeight,
    const Matrix4& viewMatrix,
    const Matrix4& projMatrix,
    float maxPixelDist
) {
    if (!scene.HasNAV() || scene.GetSelectedAreaID() == 0) return HANDLE_NONE;
    const NavArea* sel = scene.GetSelectedArea();
    if (!sel) return HANDLE_NONE;

    Matrix4 viewProj = projMatrix * viewMatrix;

    Vector3 c = sel->GetCenter();
    c.z += 4.0f;
    ScreenPoint2D sCenter = ProjectToScreen(c, viewProj, viewportWidth, viewportHeight);

    // 1. Center Handle (priority: directly around gizmo origin)
    if (sCenter.valid) {
        float dCenter = std::hypot(screenX - sCenter.x, screenY - sCenter.y);
        if (dCenter <= 14.0f) {
            return HANDLE_GIZMO_CENTER;
        }

        // 2. Gizmo Axis Arrows
        float gLen = 48.0f;
        ScreenPoint2D sX = ProjectToScreen(c + Vector3(gLen, 0.0f, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sY = ProjectToScreen(c + Vector3(0.0f, gLen, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sZ = ProjectToScreen(c + Vector3(0.0f, 0.0f, gLen), viewProj, viewportWidth, viewportHeight);

        float bestGizmoDist = maxPixelDist;
        SelectedHandleType bestGizmo = HANDLE_NONE;

        if (sX.valid) {
            float d = DistToSegment2D(screenX, screenY, sCenter.x, sCenter.y, sX.x, sX.y);
            if (d < bestGizmoDist) { bestGizmoDist = d; bestGizmo = HANDLE_GIZMO_X; }
        }
        if (sY.valid) {
            float d = DistToSegment2D(screenX, screenY, sCenter.x, sCenter.y, sY.x, sY.y);
            if (d < bestGizmoDist) { bestGizmoDist = d; bestGizmo = HANDLE_GIZMO_Y; }
        }
        if (sZ.valid) {
            float d = DistToSegment2D(screenX, screenY, sCenter.x, sCenter.y, sZ.x, sZ.y);
            if (d < bestGizmoDist) { bestGizmoDist = d; bestGizmo = HANDLE_GIZMO_Z; }
        }

        if (bestGizmo != HANDLE_NONE) {
            return bestGizmo;
        }
    }

    // 3. Corner Handles
    Vector3 cNW = sel->GetCorner(NAV_CORNER_NORTH_WEST); cNW.z += 2.0f;
    Vector3 cNE = sel->GetCorner(NAV_CORNER_NORTH_EAST); cNE.z += 2.0f;
    Vector3 cSE = sel->GetCorner(NAV_CORNER_SOUTH_EAST); cSE.z += 2.0f;
    Vector3 cSW = sel->GetCorner(NAV_CORNER_SOUTH_WEST); cSW.z += 2.0f;

    ScreenPoint2D sNW = ProjectToScreen(cNW, viewProj, viewportWidth, viewportHeight);
    ScreenPoint2D sNE = ProjectToScreen(cNE, viewProj, viewportWidth, viewportHeight);
    ScreenPoint2D sSE = ProjectToScreen(cSE, viewProj, viewportWidth, viewportHeight);
    ScreenPoint2D sSW = ProjectToScreen(cSW, viewProj, viewportWidth, viewportHeight);

    if (sNW.valid && std::hypot(screenX - sNW.x, screenY - sNW.y) <= 12.0f) return HANDLE_CORNER_NW;
    if (sNE.valid && std::hypot(screenX - sNE.x, screenY - sNE.y) <= 12.0f) return HANDLE_CORNER_NE;
    if (sSE.valid && std::hypot(screenX - sSE.x, screenY - sSE.y) <= 12.0f) return HANDLE_CORNER_SE;
    if (sSW.valid && std::hypot(screenX - sSW.x, screenY - sSW.y) <= 12.0f) return HANDLE_CORNER_SW;

    // 4. Edges (North, East, South, West)
    float edgeTolerance = 10.0f;
    float bestEdgeDist = edgeTolerance;
    SelectedHandleType bestEdge = HANDLE_NONE;

    if (sNW.valid && sNE.valid) {
        float d = DistToSegment2D(screenX, screenY, sNW.x, sNW.y, sNE.x, sNE.y);
        if (d < bestEdgeDist) { bestEdgeDist = d; bestEdge = HANDLE_EDGE_NORTH; }
    }
    if (sNE.valid && sSE.valid) {
        float d = DistToSegment2D(screenX, screenY, sNE.x, sNE.y, sSE.x, sSE.y);
        if (d < bestEdgeDist) { bestEdgeDist = d; bestEdge = HANDLE_EDGE_EAST; }
    }
    if (sSW.valid && sSE.valid) {
        float d = DistToSegment2D(screenX, screenY, sSW.x, sSW.y, sSE.x, sSE.y);
        if (d < bestEdgeDist) { bestEdgeDist = d; bestEdge = HANDLE_EDGE_SOUTH; }
    }
    if (sNW.valid && sSW.valid) {
        float d = DistToSegment2D(screenX, screenY, sNW.x, sNW.y, sSW.x, sSW.y);
        if (d < bestEdgeDist) { bestEdgeDist = d; bestEdge = HANDLE_EDGE_WEST; }
    }

    return bestEdge;
}
