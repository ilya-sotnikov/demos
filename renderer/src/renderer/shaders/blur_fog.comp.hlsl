#include "common.hlsli"

ConstantBuffer<UniformData> uniform_buffer;
Texture2D<float> in_texture;
[[vk::image_format("r16f")]]
RWTexture2D<float> out_texture;

[[vk::push_constant]]
PushConstantsFogBlur push_constants;

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

// Gaussian blur with 2 passes (vertical and horizontal).
[numthreads(RENDERER_FOG_BLUR_WORKGROUP_SIZE_X, RENDERER_FOG_BLUR_WORKGROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint2 texture_size = uint2(uniform_buffer.render_width, uniform_buffer.render_height);

    if (dtid.x >= texture_size.x || dtid.y >= texture_size.y) {
        return;
    }

    float result = 0.0f;
    float weight_sum = 0.0f;

    // 7 samples in horizontal and vertical direction (separate passes).
    for (int i = -3; i <= 3; ++i) {
        int2 sample_coord = dtid.xy + select(push_constants.horizontal == 1, int2(i, 0),  int2(0, i));
        sample_coord = clamp(sample_coord, int2(0, 0), texture_size - 1);

        const float weight = GAUSS_WEIGHTS[3 + i];

        result += weight * in_texture[sample_coord];
        weight_sum += weight;
    }

    out_texture[dtid.xy] = result / weight_sum;
}
