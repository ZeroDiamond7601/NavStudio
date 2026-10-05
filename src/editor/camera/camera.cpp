#include "editor/camera/camera.h"
#include <algorithm>

static const float DEG2RAD = 3.14159265358979323846f / 180.0f;

Camera::Camera()
    : m_mode(CAMERA_MODE_FPS)
    , m_position(0.0f, -500.0f, 300.0f)
    , m_target(0.0f, 0.0f, 0.0f)
    , m_worldUp(0.0f, 0.0f, 1.0f) // GoldSrc Z-Up coordinate convention
    , m_yaw(90.0f)
    , m_pitch(-20.0f)
    , m_speed(600.0f)
    , m_sensitivity(0.15f)
    , m_fov(70.0f)
    , m_nearPlane(1.0f)
    , m_farPlane(16384.0f)
    , m_orbitDistance(400.0f)
    , m_orthoSize(2000.0f)
{
    UpdateVectors();
}

void Camera::SetMode(CameraMode mode) {
    m_mode = mode;
    if (m_mode == CAMERA_MODE_ORBIT) {
        m_orbitDistance = (m_position - m_target).Length();
        if (m_orbitDistance < 10.0f) m_orbitDistance = 400.0f;
    } else if (m_mode == CAMERA_MODE_TOPDOWN_2D) {
        m_pitch = -89.9f;
        m_yaw = 90.0f;
    }
    UpdateVectors();
}

void Camera::SetTarget(const Vector3& target) {
    m_target = target;
    if (m_mode == CAMERA_MODE_ORBIT) {
        UpdateVectors();
    }
}

void Camera::FocusOn(const Vector3& center, float distance) {
    m_target = center;
    m_orbitDistance = distance;
    if (m_mode == CAMERA_MODE_ORBIT) {
        UpdateVectors();
    } else {
        m_position = center - m_forward * distance;
    }
}

void Camera::Update(float /*deltaTime*/) {
    // Dynamic smoothing can be added if needed
}

void Camera::ProcessKeyboard(int direction, float deltaTime) {
    float velocity = m_speed * deltaTime;

    if (m_mode == CAMERA_MODE_TOPDOWN_2D) {
        // In 2D mode, W/S moves along Y axis, A/D moves along X axis
        if (direction == 0) m_position.y += velocity; // Forward
        if (direction == 1) m_position.y -= velocity; // Backward
        if (direction == 2) m_position.x -= velocity; // Left
        if (direction == 3) m_position.x += velocity; // Right
        return;
    }

    if (direction == 0) m_position += m_forward * velocity;       // Forward
    if (direction == 1) m_position -= m_forward * velocity;       // Backward
    if (direction == 2) m_position -= m_right * velocity;         // Left
    if (direction == 3) m_position += m_right * velocity;         // Right
    if (direction == 4) m_position += m_worldUp * velocity;       // Up (E / Space)
    if (direction == 5) m_position -= m_worldUp * velocity;       // Down (Q / C)

    if (m_mode == CAMERA_MODE_ORBIT) {
        m_target = m_position + m_forward * m_orbitDistance;
    }
}

void Camera::ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch) {
    xoffset *= m_sensitivity;
    yoffset *= m_sensitivity;

    if (m_mode == CAMERA_MODE_TOPDOWN_2D) {
        // Pan the view
        m_position.x -= xoffset * (m_orthoSize / 500.0f);
        m_position.y += yoffset * (m_orthoSize / 500.0f);
        return;
    }

    m_yaw += xoffset;
    m_pitch += yoffset;

    if (constrainPitch) {
        if (m_pitch > 89.0f) m_pitch = 89.0f;
        if (m_pitch < -89.0f) m_pitch = -89.0f;
    }

    UpdateVectors();
}

void Camera::ProcessMouseScroll(float yoffset) {
    if (m_mode == CAMERA_MODE_TOPDOWN_2D) {
        m_orthoSize -= yoffset * 150.0f;
        if (m_orthoSize < 100.0f) m_orthoSize = 100.0f;
        if (m_orthoSize > 16000.0f) m_orthoSize = 16000.0f;
        return;
    }

    if (m_mode == CAMERA_MODE_ORBIT) {
        m_orbitDistance -= yoffset * 40.0f;
        if (m_orbitDistance < 20.0f) m_orbitDistance = 20.0f;
        UpdateVectors();
        return;
    }

    // In FPS flycam mode, mouse scroll adjusts translation speed
    m_speed += yoffset * 50.0f;
    if (m_speed < 50.0f) m_speed = 50.0f;
    if (m_speed > 4000.0f) m_speed = 4000.0f;
}

void Camera::UpdateVectors() {
    float yawRad = m_yaw * DEG2RAD;
    float pitchRad = m_pitch * DEG2RAD;

    Vector3 f;
    f.x = std::cos(pitchRad) * std::cos(yawRad);
    f.y = std::cos(pitchRad) * std::sin(yawRad);
    f.z = std::sin(pitchRad);
    m_forward = f.Normalized();

    m_right = m_forward.Cross(m_worldUp).Normalized();
    m_up = m_right.Cross(m_forward).Normalized();

    if (m_mode == CAMERA_MODE_ORBIT) {
        m_position = m_target - m_forward * m_orbitDistance;
    }
}

Matrix4 Camera::GetViewMatrix() const {
    if (m_mode == CAMERA_MODE_ORBIT) {
        return Matrix4::LookAt(m_position, m_target, m_worldUp);
    }
    return Matrix4::LookAt(m_position, m_position + m_forward, m_worldUp);
}

Matrix4 Camera::GetProjectionMatrix(float aspect) const {
    if (m_mode == CAMERA_MODE_TOPDOWN_2D) {
        float halfW = m_orthoSize * 0.5f * aspect;
        float halfH = m_orthoSize * 0.5f;
        return Matrix4::Ortho(-halfW, halfW, -halfH, halfH, m_nearPlane, m_farPlane);
    }
    return Matrix4::Perspective(m_fov * DEG2RAD, aspect, m_nearPlane, m_farPlane);
}
