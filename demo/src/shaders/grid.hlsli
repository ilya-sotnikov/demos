#pragma once

#include "common.hlsli"

static const float GRID_MIN_HALF_SIZE = 100.0;
static const float GRID_SIZE_SCALE_FACTOR = 10.0;
static const float GRID_PIXEL_WIDTH = 3.0;
static const float3 GRID_COLOR = float3(0.2, 0.2, 0.2);
static const float3 GRID_COLOR_X = float3(1.0, 0.2, 0.32);
static const float3 GRID_COLOR_Z = float3(0.16, 0.56, 1.0);
static const float GRID_LINE_FADE_FACTOR = 0.01;
static const float GRID_DISTANCE_FADE_THRESHOLD = 0.5; // [0.0, 1.0].
static const float GRID_VERTICAL_DISTANCE_FADE_FACTOR = 1.0;

struct VertexOutput {
    float2 pos_world_xz : PosWorldXZ;
    float4 pos_clip : SV_Position;
};

static float calc_grid_half_size(float cam_pos_world_y) {
    return max(GRID_MIN_HALF_SIZE, abs(cam_pos_world_y) * GRID_SIZE_SCALE_FACTOR);
}
