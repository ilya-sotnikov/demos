#pragma once

static const int DEBUG_DRAW_COUNT_RECT_OFFSET = 0;

[[vk::binding(30)]]
RWByteAddressBuffer debug_draw_count_buffer;
[[vk::binding(31)]]
RWStructuredBuffer<DebugDrawRectData> debug_draw_rect_buffer;

void debug_draw_rect(float4 lbrt_screen, uint32_t color) {
    uint count;
    debug_draw_count_buffer.InterlockedAdd(DEBUG_DRAW_COUNT_RECT_OFFSET, 1, count);
    if (count < RENDERER_DEBUG_DRAW_RECT_MAX_COUNT) {
        DebugDrawRectData data;
        data.lbrt_screen = lbrt_screen;
        data.color = color;
        debug_draw_rect_buffer[count] = data;
    }
}

