#pragma once

#include "Common.hlsli"

static const float GRID_SIZE = 20.0;
static const float GRID_DISTANCE_ATTENUATION_FACTOR = 0.1;
static const float GRID_MIN_PIXELS_BETWEEN_CELLS = 1.0;
static const float GRID_RATIO_LOD0 = 100.0;
static const float GRID_RATIO_LOD1 = 150.0;
static const float GRID_RATIO_LOD2 = 1000.0;
static const float GRID_KERNEL_WIDTH_FACTOR = 2.0;

struct VertexOutput
{
    float4 positionClip : SV_Position;
    float2 positionWorldXZ : PosWorldXZ;
};
