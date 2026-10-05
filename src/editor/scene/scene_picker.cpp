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
