#pragma once

#include "Common.hpp"
#include "Math/Types.hpp"

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
    Mat4 GetViewMatrix() const;

    Vec3 mPosition;
    Vec3 mDirection;
    Vec3 mRight;
    Vec3 mWorldUp; // FPS camera doesn't need local up vector.

    f32 mYaw;
    f32 mPitch;

    f32 mSpeed;
    f32 mMouseSensitivity;

    f32 mPitchClamp;

    bool mLockDirection;
};
