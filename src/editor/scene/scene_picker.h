#ifndef SCENE_PICKER_H
#define SCENE_PICKER_H

#include "math/vector3.h"
#include "editor/math/matrix4.h"
#include "nav/nav_area.h"
#include "editor/scene/editor_scene.h"
#include "editor/scene/editor_handles.h"

class ScenePicker {
public:
    static Ray ScreenPointToRay(
        float screenX, float screenY,
        float viewportWidth, float viewportHeight,
        const Matrix4& viewMatrix,
        const Matrix4& projMatrix
    );

    // Tests ray against all NavAreas in the scene and returns the closest hit area ID (or 0 if none)
    static uint32_t PickNavArea(const EditorScene& scene, const Ray& ray, Vector3* outHitPoint = nullptr);

    // Tests ray against all NavLadders in the scene and returns the closest hit ladder ID (or 0 if none)
    static uint32_t PickLadder(const EditorScene& scene, const Ray& ray, float* outT = nullptr);

    // Tests ray against all Waypoints in the scene and returns the closest hit waypoint ID (or 0 if none)
    static uint32_t PickWaypoint(const EditorScene& scene, const Ray& ray, float* outT = nullptr);

    // Tests ray against entities and returns the closest hit entity index (or -1 if none)
    static int PickEntity(const EditorScene& scene, const Ray& ray, float* outT = nullptr);

    // Tests ray against BSP world geometry and returns exact collision point
    static bool PickBSPFloor(const EditorScene& scene, const Ray& ray, Vector3* outHitPoint);

    // Tests screen point against handles of selected area (gizmo arrows, edges, corners)
    static SelectedHandleType PickAreaHandles(
        const EditorScene& scene,
        float screenX, float screenY,
        float viewportWidth, float viewportHeight,
        const Matrix4& viewMatrix,
        const Matrix4& projMatrix,
        float maxPixelDist = 12.0f
    );

    // Tests screen point against all area edges in the scene (for Bridge Tool)
    static bool PickAnyAreaEdge(
        const EditorScene& scene,
        float screenX, float screenY,
        float viewportWidth, float viewportHeight,
        const Matrix4& viewMatrix,
        const Matrix4& projMatrix,
        uint32_t& outAreaId,
        SelectedHandleType& outEdge,
        float maxPixelDist = 18.0f
    );

    // Tests screen point against all connections in the scene
    static bool PickConnection(
        const EditorScene& scene,
        float screenX, float screenY,
        float viewportWidth, float viewportHeight,
        const Matrix4& viewMatrix,
        const Matrix4& projMatrix,
        uint32_t& outFromId, uint32_t& outToId, int& outDir,
        float maxPixelDist = 12.0f
    );

    // Tests 2D screen marquee rectangle against all NavAreas in the scene
    static std::vector<uint32_t> PickAreasInRect(
        const EditorScene& scene,
        float rectMinX, float rectMinY,
        float rectMaxX, float rectMaxY,
        float viewportWidth, float viewportHeight,
        const Matrix4& viewMatrix,
        const Matrix4& projMatrix
    );

    // Tests 2D screen marquee rectangle against all Waypoints in the scene
    static std::vector<uint32_t> PickWaypointsInRect(
        const EditorScene& scene,
        float rectMinX, float rectMinY,
        float rectMaxX, float rectMaxY,
        float viewportWidth, float viewportHeight,
        const Matrix4& viewMatrix,
        const Matrix4& projMatrix
    );

private:
    static bool RayIntersectsAABB(
        const Ray& ray,
        const Vector3& mins, const Vector3& maxs,
        float& outT
    );

    static bool RayIntersectsTriangle(
        const Ray& ray,
        const Vector3& v0, const Vector3& v1, const Vector3& v2,
        float& outT, Vector3& outHitPoint
    );
};

#endif // SCENE_PICKER_H
