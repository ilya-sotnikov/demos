#pragma once

static const int DEBUG_DRAW_COUNT_RECT_OFFSET = 0;

[[vk::binding(30)]]
RWByteAddressBuffer debugDrawCountBuffer;
[[vk::binding(31)]]
RWStructuredBuffer<DebugDrawRectData> debugDrawRectBuffer;

void DebugDrawRect(float4 lbrtScreen, uint32_t color)
{
    uint count;
    debugDrawCountBuffer.InterlockedAdd(DEBUG_DRAW_COUNT_RECT_OFFSET, 1, count);
    if (count < RENDERER_DEBUG_DRAW_RECT_MAX_COUNT)
    {
        DebugDrawRectData data;
        data.lbrtScreen = lbrtScreen;
        data.color = color;
        debugDrawRectBuffer[count] = data;
    }
}

