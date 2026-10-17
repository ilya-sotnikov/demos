#include "camera.hpp"

#include "math/mat4.hpp"
#include "math/utils.hpp"
#include "math/vec3.hpp"

void Camera::move(MoveDirection move, f32 delta_time) {
    const f32 delta_pos = m_speed * delta_time;

    switch (move) {
        case MoveDirection::FORWARD:
            m_position += m_direction * delta_pos;
            break;
        case MoveDirection::BACKWARD:
            m_position -= m_direction * delta_pos;
            break;
        case MoveDirection::RIGHT:
            m_position += m_right * delta_pos;
            break;
        case MoveDirection::LEFT:
            m_position -= m_right * delta_pos;
            break;
        case MoveDirection::UP:
            m_position += m_world_up * delta_pos;
            break;
        case MoveDirection::DOWN:
            m_position -= m_world_up * delta_pos;
            break;
    }
}

void Camera::change_direction(f32 delta_x, f32 delta_y) {
    if (m_lock_direction) {
        return;
    }

    m_yaw += delta_x * m_mouse_sensitivity;
    m_pitch += delta_y * m_mouse_sensitivity;

    m_pitch = math::clamp(m_pitch, -m_pitch_clamp, m_pitch_clamp);

    update_vectors();
}

void Camera::update_vectors() {
    m_direction[0] = sinf(m_yaw) * cosf(m_pitch);
    m_direction[1] = sinf(m_pitch);
    m_direction[2] = -cosf(m_yaw) * cosf(m_pitch);
    m_direction = math::normalize(m_direction);

    m_right = math::normalize(math::cross(m_direction, m_world_up));
}

Mat4 Camera::get_view_matrix() const {
    return math::look_at(m_position, m_position + m_direction, m_world_up);
}
