#include "common.hlsli"
#include "imgui.hlsli"
#include "math.hlsli"

[[vk::push_constant]]
PushConstantsImgui pushConstants;

void main(uint vertexId : SV_VertexID, out VertexOutput output)
{
    const ImguiVertex vertex = vertexBuffer[vertexId];
    output.position =
        float4(vertex.position * pushConstants.scale + pushConstants.translate, 0.0, 1.0);
    output.uv = vertex.uv;
    output.color = UnpackRGBA8UnormToFloat4(vertex.color);
}
