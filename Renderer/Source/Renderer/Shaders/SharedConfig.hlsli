#pragma once

// Shared with C++.

#define VULKAN_ENABLE_DEBUG_UTILS

static const float RENDERER_NEAR_PLANE = 0.1f;
static const int RENDERER_MAX_FRAMES_IN_FLIGHT = 2;
static const int RENDERER_CULL_WORKGROUP_SIZE = 64;
static const int RENDERER_RENDER_WORKGROUP_SIZE_X = 8;
static const int RENDERER_RENDER_WORKGROUP_SIZE_Y = 8;
static const int RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_X = 8;
static const int RENDERER_TAA_RESOLVE_WORKGROUP_SIZE_Y = 8;
