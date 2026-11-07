#include "common.h.slang"
#include "math.h.slang"

struct VertexOutput {
    float4 position_clip : SV_Position;
    nointerpolation float4 lbrt_screen;
    nointerpolation uint32_t color;
};

ConstantBuffer<UniformData> uniform_buffer;
RWStructuredBuffer<DebugDrawRectData> debug_draw_rect_buffer;

[shader("vertex")]
void main_vert(uint vertex_id : SV_VulkanVertexID, out VertexOutput output) {
    const uint rect_vertex_id = vertex_id % 6;
    const uint rect_id = vertex_id / 6;

    DebugDrawRectData data = debug_draw_rect_buffer[rect_id];

    const float2 center = float2(
        (data.lbrt_screen.x + data.lbrt_screen.z) / 2.0f,
        (data.lbrt_screen.y + data.lbrt_screen.w) / 2.0f
    );
    const float2 extents = float2(
        data.lbrt_screen.x - data.lbrt_screen.z,
        data.lbrt_screen.y - data.lbrt_screen.w
    );

    const float2 vertices[] = {
        float2(-0.5f, -0.5f),
        float2( 0.5f, -0.5f),
        float2(-0.5f,  0.5f),

        float2(-0.5f,  0.5f),
        float2( 0.5f, -0.5f),
        float2( 0.5f,  0.5f),
    };

    float2 pos = (vertices[rect_vertex_id] * extents + center) /
        float2(uniform_buffer.render_width, uniform_buffer.render_height);
    pos = pos * 2.0f - 1.0f;

    output.position_clip = float4(pos, 0.0f, 1.0f);
    output.lbrt_screen = data.lbrt_screen;
    output.color = data.color;
}

[shader("fragment")]
float4 main_frag(VertexOutput input) : SV_Target {
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
