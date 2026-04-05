#include "Common.hlsli"
#include "Imgui.hlsli"

struct VertexInput
{
    float2 position : Position0;
    float2 uv : TexCoord0;
    float4 color : Color0;
};

[[vk::push_constant]]
PushConstantsImgui pushConstants;

VertexOutput Main(VertexInput input)
{
    VertexOutput output;

    output.position =
        float4(input.position * pushConstants.scale + pushConstants.translate, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;

    return output;
}
