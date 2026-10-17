#include "common.hlsli"

// Referenced this:
// Robust Screen Space Ambient Occlusion in 1 ms in 1080p on PS4, Wojciech Sterna, GPU Zen.

ConstantBuffer<UniformData> uniform_buffer;
SamplerState nearest_sampler;

Texture2D<float> depth_texture;
Texture2D<float> depth_view_texture;
Texture2D<float> in_texture;
[[vk::image_format("r8")]]
RWTexture2D<float> out_texture;

// Bilinear depth-aware upsampling.
[numthreads(RENDERER_SSAO_UPSAMPLE_WORKGROUP_SIZE_X, RENDERER_SSAO_UPSAMPLE_WORKGROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    const uint2 texture_size = uint2(uniform_buffer.render_width, uniform_buffer.render_height);

    if (dtid.x >= texture_size.x || dtid.y >= texture_size.y) {
        return;
    }

    const float depth_view = -RENDERER_NEAR_PLANE / (depth_texture[dtid.xy] + 1e-5f);

    const float2 texture_size_inv = uniform_buffer.render_texture_size_inv;
    const float2 pixel_coord = dtid.xy + 0.5f;
    const float2 pixel_uv = pixel_coord * texture_size_inv;

    // NOTE: According to the Vulkan spec:
    // SPIR-V instructions with Gather in the name return a vector derived from 4 texels
    // in the base level of the image view. The rules for the VK_FILTER_LINEAR minification
    // filter are applied to identify the four selected texels.
    // Just below there's a formula for selecting texels, we are just interested in order:
    // RGBA = 01, 11, 10, 00
    // We'll remap them to:
    // RGBA = 00, 10, 01, 11
    const float4 depths_view = depth_view_texture.GatherRed(nearest_sampler, pixel_uv).abrg;
    const float4 depth_view_diffs = abs(depth_view.rrrr - depths_view);

    const float4 occlusions = in_texture.GatherRed(nearest_sampler, pixel_uv).abrg;

    // https://en.wikipedia.org/wiki/Bilinear_interpolation#On_the_unit_square
    const float2 fractional = frac(pixel_coord);
    const float a = (1.0f - fractional.x) * (1.0f - fractional.y);
    const float b = fractional.x * (1.0f - fractional.y);
    const float c = (1.0f - fractional.x) * fractional.y;
    const float d = fractional.x * fractional.y;

    float result = 0.0f;
    float weight_sum = 0.0f;

    const float weight00 = a / (depth_view_diffs.x + 1e-5f);
    result += weight00 * occlusions.x;
    weight_sum += weight00;

    const float weight10 = b / (depth_view_diffs.y + 1e-5f);
    result += weight10 * occlusions.y;
    weight_sum += weight10;

    const float weight01 = c / (depth_view_diffs.z + 1e-5f);
    result += weight01 * occlusions.z;
    weight_sum += weight01;

    const float weight11 = d / (depth_view_diffs.w + 1e-5f);
    result += weight11 * occlusions.w;
    weight_sum += weight11;

    result /= weight_sum;

    out_texture[dtid.xy] = result;
}
