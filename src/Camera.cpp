#include "Camera.hpp"

#include "Math/Utils.hpp"
#include "Math/Vec3.hpp"
#include "Math/Mat4.hpp"

void Camera::Move(MoveDirection move, f32 deltaTime)
{
    const f32 deltaPos = mSpeed * deltaTime;

    switch (move)
    {
    case MoveDirection::Forward:
        mPosition += mDirection * deltaPos;
        break;
    case MoveDirection::Backward:
        mPosition -= mDirection * deltaPos;
        break;
    case MoveDirection::Right:
        mPosition += mRight * deltaPos;
        break;
    case MoveDirection::Left:
        mPosition -= mRight * deltaPos;
        break;
    case MoveDirection::Up:
        mPosition += mWorldUp * deltaPos;
        break;
    case MoveDirection::Down:
        mPosition -= mWorldUp * deltaPos;
        break;
    }
}

void Camera::ChangeDirection(f32 deltaX, f32 deltaY)
{
    if (mLockDirection)
    {
        return;
    }

    mYaw += deltaX * mMouseSensitivity;
    mPitch += deltaY * mMouseSensitivity;

    mPitch = Clamp(mPitch, -mPitchClamp, mPitchClamp);

    UpdateVectors();
}

void Camera::UpdateVectors()
{
    mDirection[0] = sinf(mYaw) * cosf(mPitch);
    mDirection[1] = sinf(mPitch);
    mDirection[2] = -cosf(mYaw) * cosf(mPitch);
    mDirection = Normalize(mDirection);

    mRight = Normalize(Cross(mDirection, mWorldUp));
}

Mat4 Camera::GetViewMatrix() const
{
    return LookAt(mPosition, mPosition + mDirection, mWorldUp);
}
