Sampler2D fontTexture;

struct VertexInput
{
    float2 position;
    float2 uv;
    float4 color;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 uv;
    float4 color;
};

struct PushConstants
{
    float2 scale;
    float2 translate;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input, uniform PushConstants pushConstants)
{
    VertexOutput output;

    output.position =
        float4(input.position * pushConstants.scale + pushConstants.translate, 0.0, 1.0);
    output.uv = input.uv;
    // ImGui outputs colors in linear space, since we use a SRGB swapchain
    // surface format, we need to apply reverse gamma correction manually,
    // otherwise we'll get double correction.
    // NOTE: pow(color, 2.2) is an approximation.
    const float gamma = 2.2;
    output.color = pow(input.color, gamma);

    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOutput input)
{
    return input.color * fontTexture.Sample(input.uv);
}
