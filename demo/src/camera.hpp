#pragma once

#include "common.hpp"
#include "math.hpp"

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

    void move(MoveDirection move, f32 deltaTime);
    void updateVectors();
    void changeDirection(f32 deltaX, f32 deltaY);
    glm::mat4 getViewMatrix() const;

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
