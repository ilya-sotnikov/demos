#include "Grid.hlsli"
#include "Math.hlsli"

// https://www.shadertoy.com/view/XtBfzz
// https://web.archive.org/web/20260411180030/https://iquilezles.org/articles/checkerfiltering/

ConstantBuffer<UniformData> uniformBuffer : register(b0, space3);

float Grid(float2 p, float2 dx, float2 dy, float ratio)
{
    // Filter kernel width.
    const float2 w = GRID_KERNEL_WIDTH_FACTOR * max(abs(dx), abs(dy)) + 1e-3;

    // Analytic (box) filtering.
    const float2 a = p + 0.5 * w;
    const float2 b = p - 0.5 * w;
    const float2 i = (floor(a) + min(frac(a) * ratio, 1.0) -
                      floor(b) - min(frac(b) * ratio, 1.0)) / (ratio * w);

    // Grid pattern.
    return (1.0 - i.x) * (1.0 - i.y);
}

float4 Main(VertexOutput input) : SV_Target
{
    const float2 posWorld = input.positionWorldXZ;
    const float2 fragToCam = posWorld - uniformBuffer.cameraPositionWorld.xz;

    const float2 p0 = posWorld * 100.0;
    const float2 p1 = posWorld * 10.0;
    const float2 p2 = posWorld;
    const float g0 = Grid(p0, ddx(p0), ddy(p0), GRID_RATIO_LOD0);
    const float g1 = Grid(p1, ddx(p1), ddy(p1), GRID_RATIO_LOD1);
    const float g2 = Grid(p2, ddx(p2), ddy(p2), GRID_RATIO_LOD2);

    float alpha = 1.0 - g0 * g1 * g2;
    const float distAttenuation =
        max(1.0, dot(fragToCam, fragToCam) * GRID_DISTANCE_ATTENUATION_FACTOR);
    alpha /= distAttenuation;

    return float4(1.0, 1.0, 1.0, alpha);
}
