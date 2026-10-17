#include "common.hlsli"

// Learned this trick (and countless more) from niagara:
// https://github.com/zeux/niagara
// NOTE: mip 0 level downsampling is not conservative, not an issue for subsequent mips,
// I don't see any artifacts and doubt that they can occur in practice. For the explanation:
// https://github.com/zeux/niagara/discussions/50

Texture2D<float> in_texture;
RWTexture2D<float> out_texture_rw;
SamplerState min_sampler;

[[vk::push_constant]]
PushConstantsDepthReduce push_constants;

[numthreads(RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_X, RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= push_constants.out_width || dtid.y >= push_constants.out_height) {
        return;
    }

    const float2 out_texture_size = float2(push_constants.out_width, push_constants.out_height);
    const float2 uv = (float2(dtid.xy) + 0.5f) / out_texture_size;
    // Min reduction sampler (minimum depth of 2x2 texel quad).
    const float min_depth = in_texture.SampleLevel(min_sampler, uv, 0).r;

    out_texture_rw[dtid.xy] = min_depth;
}
