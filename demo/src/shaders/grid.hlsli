#pragma once

#include "common.hlsli"

static const float3 GRID_COLOR = float3(0.5, 0.5, 0.5);
static const float3 GRID_COLOR_X = float3(1.0, 0.2, 0.32);
static const float3 GRID_COLOR_Z = float3(0.16, 0.56, 1.0);
static const float GRID_SIZE = 50.0;
static const float GRID_LINE_WIDTH0 = 0.002;
static const float GRID_LINE_WIDTH1 = 0.01;
static const float GRID_LINE_WIDTH2 = 0.01;
static const float GRID_DISTANCE_FADE_FACTOR = 0.01;
static const float GRID_VERTICAL_DISTANCE_FADE_FACTOR = 4.0;

struct VertexOutput
{
    float4 positionClip : SV_Position;
    float2 positionWorldXZ : PosWorldXZ;
};
