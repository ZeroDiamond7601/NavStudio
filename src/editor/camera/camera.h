#ifndef CAMERA_H
#define CAMERA_H

#include "math/vector3.h"
#include "editor/math/matrix4.h"
#include <array>

enum CameraMode {
    CAMERA_MODE_FPS,
    CAMERA_MODE_ORBIT,
    CAMERA_MODE_TOPDOWN_2D
};

class Camera {
public:
    Camera();

    void Update(float deltaTime);
    void ProcessKeyboard(int direction, float deltaTime);
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true);
    void ProcessMouseScroll(float yoffset);

    Matrix4 GetViewMatrix() const;
    Matrix4 GetProjectionMatrix(float aspect) const;

    Vector3 GetForward() const { return m_forward; }
    Vector3 GetRight() const { return m_right; }
    Vector3 GetUp() const { return m_up; }

    Vector3 GetPosition() const { return m_position; }
    void SetPosition(const Vector3& pos) { m_position = pos; UpdateVectors(); }

    void SetTarget(const Vector3& target);
    void FocusOn(const Vector3& center, float distance = 400.0f);
    void SetAngles(float pitch, float yaw);

    CameraMode GetMode() const { return m_mode; }
    void SetMode(CameraMode mode);

    float GetYaw() const { return m_yaw; }
    float GetPitch() const { return m_pitch; }
    float GetSpeed() const { return m_speed; }
    void SetSpeed(float s) { m_speed = s; }

    float GetSensitivity() const { return m_sensitivity; }
    void SetSensitivity(float s) { m_sensitivity = s; }

    bool GetInvertY() const { return m_invertY; }
    void SetInvertY(bool inv) { m_invertY = inv; }

    float GetFov() const { return m_fov; }
    void SetFov(float fov) { m_fov = fov; }

    float GetNearPlane() const { return m_nearPlane; }
    float GetFarPlane() const { return m_farPlane; }

    // Orthographic bounds for 2D mode
    float GetOrthoSize() const { return m_orthoSize; }
    void SetOrthoSize(float size) { m_orthoSize = size; }

    // DCC Navigation & View Presets (Blender style Orbit & Pan)
    void Orbit(float deltaYaw, float deltaPitch);
    void Pan(float deltaX, float deltaY);
    void SnapToPreset(int preset); // 0: Top, 1: Front, 2: Side, 3: Isometric 3D

    // Camera Bookmarks (Slots 0..9)
    struct Bookmark {
        Vector3 pos{0.0f, 0.0f, 0.0f};
        float pitch{0.0f};
        float yaw{-90.0f};
        bool valid{false};
    };

    void SaveBookmark(int slot);
    bool RecallBookmark(int slot);
    bool HasBookmark(int slot) const;
    const Bookmark& GetBookmark(int slot) const { return m_bookmarks[slot]; }

private:
    void UpdateVectors();

    CameraMode m_mode;
    Vector3 m_position;
    Vector3 m_target;
    Vector3 m_forward;
    Vector3 m_right;
    Vector3 m_up;
    Vector3 m_worldUp;

    float m_yaw;
    float m_pitch;
    float m_speed;
    float m_sensitivity;
    bool m_invertY{false};
    float m_fov;
    float m_nearPlane;
    float m_farPlane;
    float m_orbitDistance;
    float m_orthoSize;

    std::array<Bookmark, 10> m_bookmarks{};
};

#endif // CAMERA_H
