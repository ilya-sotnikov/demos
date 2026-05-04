#include "Common.hlsli"

ConstantBuffer<UniformData> uniformBuffer;

Texture2D<float> inImage;
[[vk::image_format("r16f")]]
RWTexture2D<float> outImageRW;

[numthreads(RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_X, RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= uniformBuffer.ambientOcclusionWidth ||
        dtid.y >= uniformBuffer.ambientOcclusionHeight)
    {
        return;
    }

    const float depthView = -RENDERER_NEAR_PLANE / (inImage[dtid.xy * 2] + 1e-5);

    outImageRW[dtid.xy] = depthView;
}
