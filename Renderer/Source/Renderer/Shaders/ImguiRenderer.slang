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
VertexOutput VertexMain(VertexInput input, uniform PushConstants pushConstants)
{
    VertexOutput output;

    output.position =
        float4(input.position * pushConstants.scale + pushConstants.translate, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;

    return output;
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input)
{
    return input.color * fontTexture.Sample(input.uv);
}
