#include "Common.hlsli"

ConstantBuffer<UniformData> uniformBuffer;

Texture2D<float> inTexture;
[[vk::image_format("r16f")]]
RWTexture2D<float> outTextureRW;

[numthreads(RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_X, RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= uniformBuffer.ambientOcclusionWidth ||
        dtid.y >= uniformBuffer.ambientOcclusionHeight)
    {
        return;
    }

    const float depthView = -RENDERER_NEAR_PLANE / (inTexture[dtid.xy * 2] + 1e-5);

    outTextureRW[dtid.xy] = depthView;
}
