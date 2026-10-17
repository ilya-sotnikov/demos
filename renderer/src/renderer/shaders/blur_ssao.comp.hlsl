#include "common.hlsli"

// Referenced this:
// Robust Screen Space Ambient Occlusion in 1 ms in 1080p on PS4, Wojciech Sterna, GPU Zen.

Texture2D<float> depth_view_texture;
Texture2D<float> in_texture;
[[vk::image_format("r8")]]
RWTexture2D<float> out_texture;

[[vk::push_constant]]
PushConstantsSSAO push_constants;

// Sigma = 3.0.
static float GAUSS_WEIGHTS[] = {
    0.106289f,
    0.140321f,
    0.165770f,
    0.175240f,
    0.165770f,
    0.140321f,
    0.106289f,
};

// Bilateral depth-aware filter with 2 passes (vertical and horizontal).
// NOTE: although bilateral filters are not separable, it's mostly fine and definitely faster.
[numthreads(RENDERER_SSAO_BLUR_WORKGROUP_SIZE_X, RENDERER_SSAO_BLUR_WORKGROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint2 texture_size = uint2(push_constants.width, push_constants.height);

    if (dtid.x >= texture_size.x || dtid.y >= texture_size.y) {
        return;
    }

    const float depth = depth_view_texture[dtid.xy];

    float result = 0.0f;
    float weight_sum = 0.0f;

    // 7 samples in horizontal and vertical direction (separate passes).
    for (int i = -3; i <= 3; ++i) {
        int2 sample_coord =
            dtid.xy + i * int2(push_constants.pixel_offset_x, push_constants.pixel_offset_y);
        sample_coord = clamp(sample_coord, int2(0, 0), texture_size - 1);
        const float sample_depth = depth_view_texture[sample_coord];

        float delta_depth = 0.1f * abs(depth - sample_depth);
        delta_depth *= delta_depth;

        const float weight = GAUSS_WEIGHTS[3 + i] / (delta_depth + 1e-5f);

        result += weight * in_texture[sample_coord];
        weight_sum += weight;
    }

    out_texture[dtid.xy] = result / weight_sum;
}
