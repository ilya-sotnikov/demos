#include "common.hlsli"
#include "math.hlsli"

[[vk::push_constant]]
PushConstantsTAA push_constants;

ConstantBuffer<UniformData> uniform_buffer;
Texture2D render_texture;
Texture2D depth_texture;
Texture2D velocity_texture;
Texture2D prev_resolved_render_texture;
RWTexture2D<float3> resolved_render_texture_rw;
SamplerState linear_sampler;

// Took an implementation from this article:
// https://alextardif.com/TAA.html

// Additional resources that explain these techniques in more depth:
// https://www.elopezr.com/temporal-aa-and-the-quest-for-the-holy-trail/
// https://advances.realtimerendering.com/s2014/epic/TemporalAA.pptx
// https://graphicrants.blogspot.com/2013/12/tone-mapping.html
// http://behindthepixels.io/assets/files/TemporalAA.pdf
// https://developer.download.nvidia.com/gameworks/events/GDC2016/msalvi_temporal_supersampling.pdf
// https://github.com/playdeadgames/temporal

// TODO: thin stuff flickers.
// TODO: changes color a little.
// TODO: more explanations in comments.
// TODO: optimize.

// TODO: now that we have a visibility buffer, is it useful for history rejection?

[numthreads(RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_X, RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_Y, 1)]
void main(uint3 dtid : SV_DispatchThreadID) {
    int2 render_texture_size = int2(uniform_buffer.render_width, uniform_buffer.render_height);

    if (dtid.x >= render_texture_size.x || dtid.y >= render_texture_size.y) {
        return;
    }

    if (push_constants.enable == 0) {
        resolved_render_texture_rw[dtid.xy] = render_texture[dtid.xy].rgb;
        return;
    }

    float3 curr_sample_total = 0.0f;
    float curr_sample_weight = 0.0f;
    float3 neighborhood_min = 10000.0f;
    float3 neighborhood_max = -10000.0f;
    float3 m1 = 0.0f;
    float3 m2 = 0.0f;
    float closest_depth = 0.0f;
    int2 closest_depth_pixel_position = int2(0, 0);

    // 3x3 pixel neighborhood grid.
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            int2 pixel_position = dtid.xy + int2(x, y);
            pixel_position = clamp(pixel_position, 0, render_texture_size - 1);

            const float3 neighbor = max(0, render_texture[pixel_position].rgb);

            // A filter over a neighborhood negates jitter.
            const float sub_sample_distance = length(float2(x, y));
            const float sub_sample_weight = filter_mitchell_netravali(sub_sample_distance);

            curr_sample_total += neighbor * sub_sample_weight;
            curr_sample_weight += sub_sample_weight;

            // For neighborhood clamping.
            neighborhood_min = min(neighborhood_min, neighbor);
            neighborhood_max = max(neighborhood_max, neighbor);

            // For variance clipping.
            m1 += neighbor;
            m2 += neighbor * neighbor;

            // For sampling the velocity buffer.
            const float current_depth = depth_texture[pixel_position].r;
            if (current_depth > closest_depth) {
                closest_depth = current_depth;
                closest_depth_pixel_position = pixel_position;
            }
        }
    }

    const float2 motion_vector =
        velocity_texture[closest_depth_pixel_position].xy * float2(0.5f, 0.5f);
    const float2 uv = float2(dtid.xy + 0.5f) / float2(render_texture_size);
    const float2 prev_uv = uv - motion_vector;
    const float3 curr_sample = curr_sample_total / curr_sample_weight;

    if (any(prev_uv != saturate(prev_uv))) {
        resolved_render_texture_rw[dtid.xy] = curr_sample;
        return;
    }

    float3 prev_sample = sample_texture_catmull_rom(
        prev_resolved_render_texture,
        linear_sampler,
        prev_uv,
        float2(render_texture_size)
    ).rgb;

    const float sample_count_inv = 1.0f / 9.0f;
    const float gamma = 1.0f;
    const float3 mu = m1 * sample_count_inv;
    const float3 sigma = sqrt(abs((m2 * sample_count_inv) - (mu * mu)));
    const float3 min_color = mu - gamma * sigma;
    const float3 max_color = mu + gamma * sigma;

    prev_sample =
        clip_aabb_center(min_color, max_color, clamp(prev_sample, neighborhood_min, neighborhood_max));

    const float3 curr_compressed = curr_sample * rcp(max(curr_sample) + 1.0f);
    const float3 prev_compressed = prev_sample * rcp(max(prev_sample) + 1.0f);
    const float curr_luminance = luminance(curr_compressed);
    const float prev_luminance = luminance(prev_compressed);

    float curr_weight = push_constants.blend_weight;
    float prev_weight = 1.0f - curr_weight;
    curr_weight *= 1.0f / (1.0f + curr_luminance);
    prev_weight *= 1.0f / (1.0f + prev_luminance);

    const float3 result =
        (curr_sample * curr_weight + prev_sample * prev_weight) / max(curr_weight + prev_weight, 0.00001f);

    resolved_render_texture_rw[dtid.xy] = result;
}
