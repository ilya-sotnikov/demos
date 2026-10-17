#include "common.hlsli"
#include "math.hlsli"
#include "debug_draw_rect.hlsli"

void main(uint vertex_id : SV_VertexID, out VertexOutput output) {
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
