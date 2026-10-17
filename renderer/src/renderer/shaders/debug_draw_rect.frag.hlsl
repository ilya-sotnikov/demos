
#include "common.hlsli"
#include "math.hlsli"
#include "debug_draw_rect.hlsli"

float4 main(VertexOutput input) : SV_Target {
    const float WIDTH_PIXELS = 2.0f;

    const bool within = (abs(input.position_clip.x - input.lbrt_screen.x) < WIDTH_PIXELS) ||
                        (abs(input.position_clip.x - input.lbrt_screen.z) < WIDTH_PIXELS) ||
                        (abs(input.position_clip.y - input.lbrt_screen.y) < WIDTH_PIXELS) ||
                        (abs(input.position_clip.y - input.lbrt_screen.w) < WIDTH_PIXELS);

    // Only fill pixels around edges.
    if (!within) {
        discard;
    }

    return float4(unpack_rgba8unorm_to_float4(input.color).rgb, 1.0f);
}
