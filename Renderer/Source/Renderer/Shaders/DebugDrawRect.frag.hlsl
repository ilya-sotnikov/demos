
#include "Common.hlsli"
#include "Math.hlsli"
#include "DebugDrawRect.hlsli"

float4 Main(VertexOutput input) : SV_Target
{
    const float WIDTH_PIXELS = 2.0;

    const bool within = (abs(input.positionClip.x - input.lbrtScreen.x) < WIDTH_PIXELS) ||
                        (abs(input.positionClip.x - input.lbrtScreen.z) < WIDTH_PIXELS) ||
                        (abs(input.positionClip.y - input.lbrtScreen.y) < WIDTH_PIXELS) ||
                        (abs(input.positionClip.y - input.lbrtScreen.w) < WIDTH_PIXELS);

    // Only fill pixels around edges.
    if (!within)
    {
        discard;
    }

    return float4(UnpackRGBA8UnormToFloat4(input.color).rgb, 1.0);
}
