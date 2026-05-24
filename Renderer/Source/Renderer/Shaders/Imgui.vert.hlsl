#include "Common.hlsli"
#include "Imgui.hlsli"
#include "Math.hlsli"

[[vk::push_constant]]
PushConstantsImgui pushConstants;

void Main(uint vertexId : SV_VertexID, out VertexOutput output)
{
    const ImguiVertex vertex = vertexBuffer[vertexId];
    output.position =
        float4(vertex.position * pushConstants.scale + pushConstants.translate, 0.0, 1.0);
    output.uv = vertex.uv;
    output.color = UnpackRGBA8UnormToFloat4(vertex.color);
}
