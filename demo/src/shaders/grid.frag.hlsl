#include "grid.hlsli"
#include "math.hlsli"

// Based on the Pristine Grid by Ben Golus:
// https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
// Did not implement the article fully since I use fading, therefore some
// of the stuff there is unnecessary.

ConstantBuffer<UniformData> uniformBuffer : register(b0, space3);

const float calcGrid(float2 pos, float2 fwidthPos, float width)
{
    // The first part of phone-wire AA, so the draw width is at least 1 pixel.
    const float2 drawWidth = max(width, fwidthPos);

    // Using fwidth for choosing edge AA width to naturally handle AA
    // in the distance and for grazing angles.
    // 1.5 works a little better but doesn't cause excessive blur.
    const float2 lineAA = fwidthPos * 1.5;

    // Frac(pos) is a sawtooth wave, this formula transforms it to a triangle wave.
    const float2 gridUV = 1.0 - abs(frac(pos) * 2.0 - 1.0);

    // Drawing antialiased lines (2d grid) by using smoothstep on a triangle wave.
    float2 grid = 1.0 - smoothstep(drawWidth - lineAA, drawWidth + lineAA, gridUV);

    // The second part of phone-wire AA, fading the line out in the distance.
    grid *= saturate(width / drawWidth);

    return lerp(grid.x, 1.0, grid.y); // x * (1.0 - y) + y
}

const float calcLine(float pos, float fwidthPos, float width)
{
    const float drawWidth = max(width, fwidthPos);
    const float lineAA = fwidthPos * 1.5;
    const float lineUV = abs(pos * 2.0);
    float lineVal = 1.0 - smoothstep(drawWidth - lineAA, drawWidth + lineAA, lineUV);
    return lineVal * saturate(width / drawWidth);
}

float4 main(VertexOutput input) : SV_Target
{
    const float2 pos = input.positionWorldXZ;

    const float2 pos0 = pos * 0.1;  // Largest grid (10 m).
    const float2 pos1 = pos * 1.0;  // 1 m.
    const float2 pos2 = pos * 10.0; // 0.1 m.

    const float2 fwidth1 = fwidth(pos);
    const float2 fwidth0 = fwidth1 * 0.1;
    const float2 fwidth2 = fwidth1 * 10.0;

    const float grid0 = calcGrid(pos0, fwidth0, GRID_LINE_WIDTH0);
    const float grid1 = calcGrid(pos1, fwidth1, GRID_LINE_WIDTH1);
    const float grid2 = calcGrid(pos2, fwidth2, GRID_LINE_WIDTH2);

    float alpha = max(max(grid0, grid1), grid2);

    // Distance fading.
    const float3 camPos = uniformBuffer.cameraPositionWorld;
    const float2 posGrid = pos - camPos.xz;
    alpha /= max(1.0, dot(posGrid, posGrid) * GRID_DISTANCE_FADE_FACTOR);

    // Hiding the grid when the camera is close.
    alpha *= saturate(abs(camPos.y * GRID_VERTICAL_DISTANCE_FADE_FACTOR));

    const float lineX = calcLine(pos0.y, fwidth0.y, GRID_LINE_WIDTH0);
    const float lineZ = calcLine(pos0.x, fwidth0.x, GRID_LINE_WIDTH0);

    // Only drawing positive X and Z lines to emphasize their direction.
    const float w = GRID_LINE_WIDTH0 * 0.5;
    float3 color = GRID_COLOR;
    color = lerp(color, GRID_COLOR_X, lineX * smoothstep(w - fwidth0.x, w + fwidth0.x, pos0.x));
    color = lerp(color, GRID_COLOR_Z, lineZ * smoothstep(w - fwidth0.y, w + fwidth0.y, pos0.y));

    return float4(color, alpha);
}
