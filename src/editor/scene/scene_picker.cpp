#include "editor/scene/scene_picker.h"
#include <cmath>
#include <algorithm>
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
    Vector3 c(0.0f, 0.0f, 0.0f);
    const NavArea* selArea = nullptr;
    const EditorEntity* selEnt = nullptr;

    const auto& selIds = scene.GetSelectedAreaIDs();
    if (scene.HasNAV() && !selIds.empty()) {
        if (selIds.size() > 1) {
            Vector3 cluster(0.0f, 0.0f, 0.0f);
            size_t validCount = 0;
            for (uint32_t sid : selIds) {
                const NavArea* a = scene.GetNAV().GetAreaByID(sid);
                if (a) {
                    cluster += a->GetCenter();
                    validCount++;
                }
            }
            if (validCount > 0) {
                c = cluster * (1.0f / static_cast<float>(validCount));
                c.z += 4.0f;
            }
        } else {
            selArea = scene.GetNAV().GetAreaByID(selIds.front());
            if (selArea) {
                c = selArea->GetCenter();
                c.z += 4.0f;
            }
        }
    } else if (scene.GetSelectedEntityIndex() >= 0) {
        selEnt = scene.GetSelectedEntity();
        if (selEnt) {
            c = selEnt->origin;
        }
    }

    if (!selArea && selIds.empty() && !selEnt) return HANDLE_NONE;

    Matrix4 viewProj = projMatrix * viewMatrix;
    Matrix4 invView = viewMatrix.Inverse();
    Vector3 camPos(invView.m[12], invView.m[13], invView.m[14]);

    float camDist = (camPos - c).Length();
    float gLen = std::max(48.0f, std::min(450.0f, camDist * 0.105f));
    float coneH = gLen * 0.22f;
    float cubeDist = gLen * 0.72f;
    float rotR = gLen * 0.58f;
    float screenR = gLen * 0.88f;

    GizmoMode mode = scene.GetGizmoMode();
    ScreenPoint2D sCenter = ProjectToScreen(c, viewProj, viewportWidth, viewportHeight);

    // 1. Center Translation Diamond (Priority: center)
    if (sCenter.valid && (mode == GIZMO_MODE_COMBINED || mode == GIZMO_MODE_TRANSLATE)) {
        float dCenter = std::hypot(screenX - sCenter.x, screenY - sCenter.y);
        if (dCenter <= 14.0f) {
            return HANDLE_GIZMO_CENTER;
        }
    }

    // 2. Planar Quads (XY Ground, XZ East-West Vert, YZ North-South Vert)
    float planeDist = gLen * 0.35f;
    float planeSize = gLen * 0.18f;
    float planeMid = planeDist + planeSize * 0.5f;

    if (mode == GIZMO_MODE_COMBINED || mode == GIZMO_MODE_TRANSLATE) {
        ScreenPoint2D spXY = ProjectToScreen(c + Vector3(planeMid, planeMid, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D spXZ = ProjectToScreen(c + Vector3(planeMid, 0.0f, planeMid), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D spYZ = ProjectToScreen(c + Vector3(0.0f, planeMid, planeMid), viewProj, viewportWidth, viewportHeight);

        float bestPlaneDist = 15.0f;
        SelectedHandleType bestPlane = HANDLE_NONE;
        if (spXY.valid) {
            float d = std::hypot(screenX - spXY.x, screenY - spXY.y);
            if (d <= bestPlaneDist) { bestPlaneDist = d; bestPlane = HANDLE_PLANE_XY; }
        }
        if (spXZ.valid) {
            float d = std::hypot(screenX - spXZ.x, screenY - spXZ.y);
            if (d <= bestPlaneDist) { bestPlaneDist = d; bestPlane = HANDLE_PLANE_XZ; }
        }
        if (spYZ.valid) {
            float d = std::hypot(screenX - spYZ.x, screenY - spYZ.y);
            if (d <= bestPlaneDist) { bestPlaneDist = d; bestPlane = HANDLE_PLANE_YZ; }
        }
        if (bestPlane != HANDLE_NONE) return bestPlane;
    } else if (mode == GIZMO_MODE_SCALE) {
        ScreenPoint2D spXY = ProjectToScreen(c + Vector3(planeMid, planeMid, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D spXZ = ProjectToScreen(c + Vector3(planeMid, 0.0f, planeMid), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D spYZ = ProjectToScreen(c + Vector3(0.0f, planeMid, planeMid), viewProj, viewportWidth, viewportHeight);

        float bestPlaneDist = 15.0f;
        SelectedHandleType bestPlane = HANDLE_NONE;
        if (spXY.valid) {
            float d = std::hypot(screenX - spXY.x, screenY - spXY.y);
            if (d <= bestPlaneDist) { bestPlaneDist = d; bestPlane = HANDLE_SCALE_PLANE_XY; }
        }
        if (spXZ.valid) {
            float d = std::hypot(screenX - spXZ.x, screenY - spXZ.y);
            if (d <= bestPlaneDist) { bestPlaneDist = d; bestPlane = HANDLE_SCALE_PLANE_XZ; }
        }
        if (spYZ.valid) {
            float d = std::hypot(screenX - spYZ.x, screenY - spYZ.y);
            if (d <= bestPlaneDist) { bestPlaneDist = d; bestPlane = HANDLE_SCALE_PLANE_YZ; }
        }
        if (bestPlane != HANDLE_NONE) return bestPlane;
    }

    // 3. Scale Cubes (+X, +Y, +Z at cubeDist)
    if (mode == GIZMO_MODE_COMBINED || mode == GIZMO_MODE_SCALE) {
        ScreenPoint2D scX = ProjectToScreen(c + Vector3(cubeDist, 0.0f, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D scY = ProjectToScreen(c + Vector3(0.0f, cubeDist, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D scZ = ProjectToScreen(c + Vector3(0.0f, 0.0f, cubeDist), viewProj, viewportWidth, viewportHeight);

        if (scX.valid && std::hypot(screenX - scX.x, screenY - scX.y) <= 12.0f) return HANDLE_SCALE_X;
        if (scY.valid && std::hypot(screenX - scY.x, screenY - scY.y) <= 12.0f) return HANDLE_SCALE_Y;
        if (scZ.valid && std::hypot(screenX - scZ.x, screenY - scZ.y) <= 12.0f) return HANDLE_SCALE_Z;
    }

    // 3. Translate Arrow Cone Tips (+X, +Y, +Z)
    if (mode == GIZMO_MODE_COMBINED || mode == GIZMO_MODE_TRANSLATE) {
        ScreenPoint2D sTipX = ProjectToScreen(c + Vector3(gLen + coneH * 0.5f, 0.0f, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sTipY = ProjectToScreen(c + Vector3(0.0f, gLen + coneH * 0.5f, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sTipZ = ProjectToScreen(c + Vector3(0.0f, 0.0f, gLen + coneH * 0.5f), viewProj, viewportWidth, viewportHeight);

        if (sTipX.valid && std::hypot(screenX - sTipX.x, screenY - sTipX.y) <= 13.0f) return HANDLE_GIZMO_X;
        if (sTipY.valid && std::hypot(screenX - sTipY.x, screenY - sTipY.y) <= 13.0f) return HANDLE_GIZMO_Y;
        if (sTipZ.valid && std::hypot(screenX - sTipZ.x, screenY - sTipZ.y) <= 13.0f) return HANDLE_GIZMO_Z;
    }

    // 5. Outer Uniform Scale Circle (in Scale Mode)
    Vector3 fwd = (camPos - c).Normalized();
    Vector3 upGuide(0.0f, 0.0f, 1.0f);
    if (std::abs(fwd.z) > 0.92f) upGuide = Vector3(0.0f, 1.0f, 0.0f);
    Vector3 rDir = fwd.Cross(upGuide).Normalized();
    Vector3 uDir = rDir.Cross(fwd).Normalized();

    if (mode == GIZMO_MODE_SCALE) {
        float minD = 999.0f;
        const int kSegs = 32;
        float radius = screenR * 1.05f;
        Vector3 prevP = c + rDir * radius;
        ScreenPoint2D prevS = ProjectToScreen(prevP, viewProj, viewportWidth, viewportHeight);
        for (int k = 1; k <= kSegs; ++k) {
            float ang = 2.0f * 3.14159265358979323846f * static_cast<float>(k) / static_cast<float>(kSegs);
            Vector3 curP = c + rDir * (radius * std::cos(ang)) + uDir * (radius * std::sin(ang));
            ScreenPoint2D curS = ProjectToScreen(curP, viewProj, viewportWidth, viewportHeight);
            if (prevS.valid && curS.valid) {
                float d = DistToSegment2D(screenX, screenY, prevS.x, prevS.y, curS.x, curS.y);
                if (d < minD) minD = d;
            }
            prevS = curS;
        }
        if (minD <= 9.0f) {
            return HANDLE_SCALE_UNIFORM;
        }
    }

    // 6. Rotate Rings (Yaw Z in XY, Pitch X in YZ, Roll Y in XZ, Screen trackball)
    if (mode == GIZMO_MODE_COMBINED || mode == GIZMO_MODE_ROTATE) {
        float rotTolerance = 8.0f;
        const int kSegs = 32;

        auto TestCircleSegments = [&](const Vector3& ruDir, const Vector3& rvDir, float radius) -> float {
            float minD = 999.0f;
            Vector3 prevP = c + ruDir * radius;
            ScreenPoint2D prevS = ProjectToScreen(prevP, viewProj, viewportWidth, viewportHeight);
            for (int k = 1; k <= kSegs; ++k) {
                float ang = 2.0f * 3.14159265358979323846f * static_cast<float>(k) / static_cast<float>(kSegs);
                Vector3 curP = c + ruDir * (radius * std::cos(ang)) + rvDir * (radius * std::sin(ang));
                ScreenPoint2D curS = ProjectToScreen(curP, viewProj, viewportWidth, viewportHeight);
                if (prevS.valid && curS.valid) {
                    float d = DistToSegment2D(screenX, screenY, prevS.x, prevS.y, curS.x, curS.y);
                    if (d < minD) minD = d;
                }
                prevS = curS;
            }
            return minD;
        };

        float dRotZ = TestCircleSegments(Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 1.0f, 0.0f), rotR);
        float dRotX = TestCircleSegments(Vector3(0.0f, 1.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), rotR);
        float dRotY = TestCircleSegments(Vector3(1.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 1.0f), rotR);
        float dRotScreen = TestCircleSegments(rDir, uDir, screenR);

        float bestRotDist = rotTolerance;
        SelectedHandleType bestRot = HANDLE_NONE;

        if (dRotZ < bestRotDist) { bestRotDist = dRotZ; bestRot = HANDLE_ROTATE_Z; }
        if (dRotX < bestRotDist) { bestRotDist = dRotX; bestRot = HANDLE_ROTATE_X; }
        if (dRotY < bestRotDist) { bestRotDist = dRotY; bestRot = HANDLE_ROTATE_Y; }
        if (dRotScreen < bestRotDist) { bestRotDist = dRotScreen; bestRot = HANDLE_ROTATE_SCREEN; }

        if (bestRot != HANDLE_NONE) {
            return bestRot;
        }
    }

    // 5. Translate Arrow Shafts
    if (mode == GIZMO_MODE_COMBINED || mode == GIZMO_MODE_TRANSLATE) {
        ScreenPoint2D sBaseX = ProjectToScreen(c + Vector3(gLen, 0.0f, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sBaseY = ProjectToScreen(c + Vector3(0.0f, gLen, 0.0f), viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sBaseZ = ProjectToScreen(c + Vector3(0.0f, 0.0f, gLen), viewProj, viewportWidth, viewportHeight);

        float bestArrowDist = maxPixelDist;
        SelectedHandleType bestArrow = HANDLE_NONE;

        if (sCenter.valid && sBaseX.valid) {
            float d = DistToSegment2D(screenX, screenY, sCenter.x, sCenter.y, sBaseX.x, sBaseX.y);
            if (d < bestArrowDist) { bestArrowDist = d; bestArrow = HANDLE_GIZMO_X; }
        }
        if (sCenter.valid && sBaseY.valid) {
            float d = DistToSegment2D(screenX, screenY, sCenter.x, sCenter.y, sBaseY.x, sBaseY.y);
            if (d < bestArrowDist) { bestArrowDist = d; bestArrow = HANDLE_GIZMO_Y; }
        }
        if (sCenter.valid && sBaseZ.valid) {
            float d = DistToSegment2D(screenX, screenY, sCenter.x, sCenter.y, sBaseZ.x, sBaseZ.y);
            if (d < bestArrowDist) { bestArrowDist = d; bestArrow = HANDLE_GIZMO_Z; }
        }

        if (bestArrow != HANDLE_NONE) {
            return bestArrow;
        }
    }

    // 6. Corner Handles (Area only)
    if (selArea) {
        Vector3 cNW = selArea->GetCorner(NAV_CORNER_NORTH_WEST); cNW.z += 2.0f;
        Vector3 cNE = selArea->GetCorner(NAV_CORNER_NORTH_EAST); cNE.z += 2.0f;
        Vector3 cSE = selArea->GetCorner(NAV_CORNER_SOUTH_EAST); cSE.z += 2.0f;
        Vector3 cSW = selArea->GetCorner(NAV_CORNER_SOUTH_WEST); cSW.z += 2.0f;

        ScreenPoint2D sNW = ProjectToScreen(cNW, viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sNE = ProjectToScreen(cNE, viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sSE = ProjectToScreen(cSE, viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sSW = ProjectToScreen(cSW, viewProj, viewportWidth, viewportHeight);

        if (sNW.valid && std::hypot(screenX - sNW.x, screenY - sNW.y) <= 12.0f) return HANDLE_CORNER_NW;
        if (sNE.valid && std::hypot(screenX - sNE.x, screenY - sNE.y) <= 12.0f) return HANDLE_CORNER_NE;
        if (sSE.valid && std::hypot(screenX - sSE.x, screenY - sSE.y) <= 12.0f) return HANDLE_CORNER_SE;
        if (sSW.valid && std::hypot(screenX - sSW.x, screenY - sSW.y) <= 12.0f) return HANDLE_CORNER_SW;

        // 7. Edges (North, East, South, West)
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

    return HANDLE_NONE;
}

bool ScenePicker::PickAnyAreaEdge(
    const EditorScene& scene,
    float screenX, float screenY,
    float viewportWidth, float viewportHeight,
    const Matrix4& viewMatrix,
    const Matrix4& projMatrix,
    uint32_t& outAreaId,
    SelectedHandleType& outEdge,
    float maxPixelDist
) {
    outAreaId = 0;
    outEdge = HANDLE_NONE;
    if (!scene.HasNAV()) return false;

    const auto& nav = scene.GetNAV();
    if (!nav.IsLoaded() || nav.GetAreaCount() == 0) return false;

    Matrix4 viewProj = projMatrix * viewMatrix;

    float bestDist = maxPixelDist;
    uint32_t bestAreaId = 0;
    SelectedHandleType bestEdge = HANDLE_NONE;

    // Ray to test area under cursor first for quick prioritization
    Ray ray = ScreenPointToRay(screenX, screenY, (float)viewportWidth, (float)viewportHeight, viewMatrix, projMatrix);
    uint32_t directAreaId = PickNavArea(scene, ray);

    auto TestAreaEdges = [&](const NavArea* area) {
        if (!area) return;
        Vector3 cNW = area->GetCorner(NAV_CORNER_NORTH_WEST); cNW.z += 2.0f;
        Vector3 cNE = area->GetCorner(NAV_CORNER_NORTH_EAST); cNE.z += 2.0f;
        Vector3 cSE = area->GetCorner(NAV_CORNER_SOUTH_EAST); cSE.z += 2.0f;
        Vector3 cSW = area->GetCorner(NAV_CORNER_SOUTH_WEST); cSW.z += 2.0f;

        ScreenPoint2D sNW = ProjectToScreen(cNW, viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sNE = ProjectToScreen(cNE, viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sSE = ProjectToScreen(cSE, viewProj, viewportWidth, viewportHeight);
        ScreenPoint2D sSW = ProjectToScreen(cSW, viewProj, viewportWidth, viewportHeight);

        if (sNW.valid && sNE.valid) {
            float d = DistToSegment2D(screenX, screenY, sNW.x, sNW.y, sNE.x, sNE.y);
            if (d < bestDist) { bestDist = d; bestEdge = HANDLE_EDGE_NORTH; bestAreaId = area->GetID(); }
        }
        if (sNE.valid && sSE.valid) {
            float d = DistToSegment2D(screenX, screenY, sNE.x, sNE.y, sSE.x, sSE.y);
            if (d < bestDist) { bestDist = d; bestEdge = HANDLE_EDGE_EAST; bestAreaId = area->GetID(); }
        }
        if (sSW.valid && sSE.valid) {
            float d = DistToSegment2D(screenX, screenY, sSW.x, sSW.y, sSE.x, sSE.y);
            if (d < bestDist) { bestDist = d; bestEdge = HANDLE_EDGE_SOUTH; bestAreaId = area->GetID(); }
        }
        if (sNW.valid && sSW.valid) {
            float d = DistToSegment2D(screenX, screenY, sNW.x, sNW.y, sSW.x, sSW.y);
            if (d < bestDist) { bestDist = d; bestEdge = HANDLE_EDGE_WEST; bestAreaId = area->GetID(); }
        }
    };

    if (directAreaId != 0) {
        TestAreaEdges(nav.GetAreaByID(directAreaId));
    }

    for (const auto* area : nav.GetAreas()) {
        if (!area || area->GetID() == directAreaId) continue;
        ScreenPoint2D sCenter = ProjectToScreen(area->GetCenter(), viewProj, viewportWidth, viewportHeight);
        if (sCenter.valid && std::hypot(screenX - sCenter.x, screenY - sCenter.y) > 400.0f) {
            continue;
        }
        TestAreaEdges(area);
    }

    if (bestAreaId != 0 && bestEdge != HANDLE_NONE) {
        outAreaId = bestAreaId;
        outEdge = bestEdge;
        return true;
    }
    return false;
}

bool ScenePicker::PickConnection(
    const EditorScene& scene,
    float screenX, float screenY,
    float viewportWidth, float viewportHeight,
    const Matrix4& viewMatrix,
    const Matrix4& projMatrix,
    uint32_t& outFromId, uint32_t& outToId, int& outDir,
    float maxPixelDist
) {
    outFromId = 0;
    outToId = 0;
    outDir = -1;
    if (!scene.HasNAV()) return false;
    const NavMesh& nav = scene.GetNAV();
    Matrix4 viewProj = projMatrix * viewMatrix;

    float bestDist = maxPixelDist;
    float bestDepth = std::numeric_limits<float>::max();
    bool found = false;

    for (const auto* area : nav.GetAreas()) {
        if (!area) continue;
        Vector3 centerA = area->GetCenter();
        for (int d = 0; d < 4; ++d) {
            for (const auto& conn : area->GetAdjacentList(static_cast<NavDirType>(d))) {
                const NavArea* target = conn.area;
                if (!target) continue;
                Vector3 centerB = target->GetCenter();

                bool isTwoWay = target->IsConnected(area);
                Vector3 delta = centerB - centerA;
                float dLen = delta.Length();
                if (dLen < 1.0f) continue;

                Vector3 fwd = delta * (1.0f / dLen);
                Vector3 lateral(-fwd.y, fwd.x, 0.0f);
                float laneOffset = isTwoWay ? 3.5f : 0.0f;
                Vector3 start = centerA + lateral * laneOffset + Vector3(0.0f, 0.0f, 4.0f);
                Vector3 end = centerB + lateral * laneOffset + Vector3(0.0f, 0.0f, 4.0f);

                ScreenPoint2D spA = ProjectToScreen(start, viewProj, viewportWidth, viewportHeight);
                ScreenPoint2D spB = ProjectToScreen(end, viewProj, viewportWidth, viewportHeight);

                if (!spA.valid && !spB.valid) continue;

                float dist = DistToSegment2D(screenX, screenY, spA.x, spA.y, spB.x, spB.y);
                if (dist <= bestDist) {
                    Vector3 mid = (start + end) * 0.5f;
                    Vector4 clip = viewProj * Vector4(mid.x, mid.y, mid.z, 1.0f);
                    float depth = clip.w;
                    if (dist < bestDist - 1.5f || (std::fabs(dist - bestDist) <= 1.5f && depth < bestDepth)) {
                        bestDist = dist;
                        bestDepth = depth;
                        outFromId = area->GetID();
                        outToId = target->GetID();
                        outDir = d;
                        found = true;
                    }
                }
            }
        }
    }

    return found;
}
