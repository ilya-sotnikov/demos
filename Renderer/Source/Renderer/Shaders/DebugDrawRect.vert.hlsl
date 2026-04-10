#include "Common.hlsli"
#include "Math.hlsli"
#include "DebugDrawRect.hlsli"

void Main(uint vertexId : SV_VertexID, out VertexOutput output)
{
    const uint rectVertexId = vertexId % 6;
    const uint rectId = vertexId / 6;

    DebugDrawRectData data = debugDrawRectBuffer[rectId];

    const float2 center = float2(
        (data.lbrtScreen.x + data.lbrtScreen.z) / 2.0,
        (data.lbrtScreen.y + data.lbrtScreen.w) / 2.0
    );
    const float2 extents = float2(
        data.lbrtScreen.x - data.lbrtScreen.z,
        data.lbrtScreen.y - data.lbrtScreen.w
    );

    const float2 vertices[] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),

        float2(-0.5,  0.5),
        float2( 0.5, -0.5),
        float2( 0.5,  0.5),
    };

    float2 pos = (vertices[rectVertexId] * extents + center) /
        float2(uniformBuffer.renderWidth, uniformBuffer.renderHeight);
    pos = pos * 2.0 - 1.0;

    output.positionClip = float4(pos, 0.0, 1.0);
    output.lbrtScreen = data.lbrtScreen;
    output.color = data.color;
}
