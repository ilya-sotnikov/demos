#pragma once

// GLSL mod.
template <typename T>
T Mod(T x, float y)
{
    return x - y * floor(x / y);
}

float Max2(float2 v)
{
    return max(v.x, v.y);
}
