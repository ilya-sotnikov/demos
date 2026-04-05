#include "Common.hlsli"
#include "Fullscreen.hlsli"

void Main(uint vertexId : SV_VertexID, out VertexOutput output)
{
    // Fullscreen triangle.
    output.uv = float2((vertexId << 1) & 2, vertexId & 2);
    output.positionClip = float4(output.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
}
