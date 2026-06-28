#pragma once

#include "Common.hpp"
#include "Math.hpp"

struct Camera
{
    enum class MoveDirection
    {
        Left,
        Right,
        Forward,
        Backward,
        Down,
        Up
    };

    void Move(MoveDirection move, f32 deltaTime);
    void UpdateVectors();
    void ChangeDirection(f32 deltaX, f32 deltaY);
    glm::mat4 GetViewMatrix() const;

    glm::vec3 mPosition;
    glm::vec3 mDirection;
    glm::vec3 mRight;
    glm::vec3 mWorldUp;

    f32 mYaw;
    f32 mPitch;

    f32 mSpeed;
    f32 mMouseSensitivity;

    f32 mPitchClamp;

    bool mLockDirection;
};
