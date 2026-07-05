#pragma once

// GLSL mod.
template <typename T>
T mod(T x, float y)
{
    return x - y * floor(x / y);
}

float max2(float2 v)
{
    return max(v.x, v.y);
}
