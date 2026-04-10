#include "Common.hlsli"

// Learned this trick (and countless more) from niagara:
// https://github.com/zeux/niagara
// NOTE: mip 0 level downsampling is not conservative, not an issue for subsequent mips,
// I don't see any artifacts and doubt that they can occur in practice. For the explanation:
// https://github.com/zeux/niagara/discussions/50

Texture2D<float> inImage;
RWTexture2D<float> outImageRW;
SamplerState minSampler;

[[vk::push_constant]]
PushConstantsDepthReduce pushConstants;

[shader("compute")]
[numthreads(RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_X, RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_Y, 1)]
void Main(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= pushConstants.outWidth || dtid.y >= pushConstants.outHeight)
    {
        return;
    }

    const float2 outImageSize = float2(pushConstants.outWidth, pushConstants.outHeight);
    const float2 uv = (float2(dtid.xy) + 0.5) / outImageSize;
    // Min reduction sampler (minimum depth of 2x2 texel quad).
    const float minDepth = inImage.SampleLevel(minSampler, uv, 0).r;

    outImageRW[dtid.xy] = minDepth;
}
