#pragma once

#include "Common.hpp"
#include "Math/Types.hpp"

struct Camera {
    enum class MoveDirection { LEFT, RIGHT, FORWARD, BACKWARD, DOWN, UP };

    void move(MoveDirection move, f32 delta_time);
    void update_vectors();
    void change_direction(f32 delta_x, f32 delta_y);
    Mat4 get_view_matrix() const;

    Vec3 m_position;
    Vec3 m_direction;
    Vec3 m_right;
    Vec3 m_world_up;

    f32 m_yaw;
    f32 m_pitch;

    f32 m_speed;
    f32 m_mouse_sensitivity;

    f32 m_pitch_clamp;

    bool m_lock_direction;
};
