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
