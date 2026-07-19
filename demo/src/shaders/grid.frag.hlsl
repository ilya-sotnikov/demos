#include "grid.hlsli"
#include "math.hlsli"

// Loosely based on the ideas from the Pristine Grid by Ben Golus:
// https://bgolus.medium.com/the-best-darn-grid-shader-yet-727f9278b9d8
// And The Machinery's blog archive:
// https://ruby0x1.github.io/machinery_blog_archive/post/borderland-between-rendering-and-editor-part-1/index.html

ConstantBuffer<UniformData> uniform_buffer : register(b0, space3);

const float calc_grid(float2 pos, float2 fwidth_pos) {
    // Drawing the grid and the first part of phone-wire AA.
    // Dividing by fwidth_pos makes sure that the width is at least fwidth_pos pixels.
    // Frac(pos) is a sawtooth wave, this formula transforms it to a triangle wave.
    float2 grid = 1.0 - abs(saturate(frac(pos) / fwidth_pos) * 2.0 - 1.0);
    // The second part of phone-wire AA, fading the line out in the distance.
    grid *= saturate(GRID_LINE_FADE_FACTOR / fwidth_pos);

    return lerp(grid.x, 1.0, grid.y); // x * (1.0 - y) + y
}

const float calc_line(float pos, float fwidth_pos) {
    const float l = 1.0 - abs(saturate(pos / fwidth_pos) * 2.0 - 1.0);
    return l * saturate(GRID_LINE_FADE_FACTOR / fwidth_pos);;
}

float4 main(VertexOutput input) : SV_Target {
    const float2 pos = input.pos_world_xz;
    const float3 cam_pos_world = uniform_buffer.camera_position_world;

    // Grid should be dynamically sized to handle object sizes of the orders of meters
    // and tens of kilometers. We'll scale the grid based on the camera distance from
    // the grid plane. Multiplying positions makes the grid smaller, dividing -- bigger.
    const float log_dist = log10(max(1.0, abs(cam_pos_world.y)));
    const float lod_level = floor(log_dist);

    const float mul0 = pow(10.0, -lod_level - 1.0); // Largest grid.
    const float mul1 = mul0 * 10.0;
    const float mul2 = mul0 * 100.0; // Smallest grid.

    const float2 pos0 = pos * mul0;
    const float2 pos1 = pos * mul1;
    const float2 pos2 = pos * mul2;

    const float2 fwidth_pos = fwidth(pos);
    const float2 fwidth0 = fwidth_pos * mul0 * GRID_PIXEL_WIDTH;
    const float2 fwidth1 = fwidth_pos * mul1 * GRID_PIXEL_WIDTH;
    const float2 fwidth2 = fwidth_pos * mul2 * GRID_PIXEL_WIDTH;

    const float grid0 = calc_grid(pos0, fwidth0);
    const float grid1 = calc_grid(pos1, fwidth1);
    const float grid2 = calc_grid(pos2, fwidth2);

    float alpha = max(max(grid0, grid1), grid2);

    // Distance fading.
    const float2 pos_grid = pos - cam_pos_world.xz;
    const float half_size = calc_grid_half_size(cam_pos_world.y);
    alpha *= 1.0 - smoothstep(
        half_size * GRID_DISTANCE_FADE_THRESHOLD,
        half_size,
        length(pos_grid)
    );

    // Hiding the grid when the camera is close.
    alpha *= saturate(abs(cam_pos_world.y * GRID_VERTICAL_DISTANCE_FADE_FACTOR));

    const float line_x = calc_line(pos0.y, fwidth0.y);
    const float line_z = calc_line(pos0.x, fwidth0.x);

    // Only drawing positive X and Z lines to emphasize their direction.
    float3 color = GRID_COLOR;
    color = lerp(color, GRID_COLOR_X, line_x * step(0.0, pos0.x));
    color = lerp(color, GRID_COLOR_Z, line_z * step(0.0, pos0.y));

#if 0
    if (pos.x > 0.0 && pos.y > 0.0) {
        if (pos.x < 0.1 && pos.y < 0.1)              color = float3(0.0, 0.0, 1.0);
        else if (pos.x < 1.0 && pos.y < 1.0)         color = float3(1.0, 0.0, 0.0);
        else if (pos.x < 10.0 && pos.y < 10.0)       color = float3(0.0, 1.0, 0.0);
        else if (pos.x < 100.0 && pos.y < 100.0)     color = float3(1.0, 0.5, 0.0);
        else if (pos.x < 1000.0 && pos.y < 1000.0)   color = float3(0.0, 0.7, 1.0);
        else if (pos.x < 10000.0 && pos.y < 10000.0) color = float3(1.0, 0.0, 1.0);
    }
#endif

    return float4(color, alpha);
}
