#include "common.hlsli"
#include "fullscreen.hlsli"

void main(uint vertex_id : SV_VertexID, out VertexOutput output)
{
    // Fullscreen triangle.
    output.uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    output.position_clip = float4(output.uv * float2(2.0f, -2.0f) + float2(-1.0f, 1.0f), 0.0f, 1.0f);
}
