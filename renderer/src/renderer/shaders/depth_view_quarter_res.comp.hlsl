#include "common.hlsli"

[[vk::push_constant]]
PushConstantsSSAO push_constants;

Texture2D<float> in_texture;
[[vk::image_format("r16f")]]
RWTexture2D<float> out_texture_rw;

[numthreads(RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_X, RENDERER_DEPTH_REDUCE_WORKGROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    if (dtid.x >= push_constants.width || dtid.y >= push_constants.height) {
        return;
    }

    const float depth_view = -RENDERER_NEAR_PLANE / (in_texture[dtid.xy * 2] + 1e-5f);

    out_texture_rw[dtid.xy] = depth_view;
}
