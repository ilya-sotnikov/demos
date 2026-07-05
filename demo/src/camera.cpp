#include "camera.hpp"

void Camera::move(MoveDirection move, f32 deltaTime)
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

void Camera::changeDirection(f32 deltaX, f32 deltaY)
{
    if (mLockDirection)
    {
        return;
    }

    mYaw += deltaX * mMouseSensitivity;
    mPitch += deltaY * mMouseSensitivity;

    mPitch = glm::clamp(mPitch, -mPitchClamp, mPitchClamp);

    updateVectors();
}

void Camera::updateVectors()
{
    mDirection[0] = sinf(mYaw) * cosf(mPitch);
    mDirection[1] = sinf(mPitch);
    mDirection[2] = cosf(mYaw) * cosf(mPitch);
    mDirection = glm::normalize(mDirection);

    mRight = glm::normalize(glm::cross(mWorldUp, mDirection));
}

glm::mat4 Camera::getViewMatrix() const
{
    return glm::lookAt(mPosition, mPosition + mDirection, mWorldUp);
}
