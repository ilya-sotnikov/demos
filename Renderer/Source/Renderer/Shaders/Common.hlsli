#pragma once

#include "SharedConfig.hlsli"
#include "SharedDef.hlsli"

struct VkDrawIndexedIndirectCommand
{
    uint32_t    indexCount;
    uint32_t    instanceCount;
    uint32_t    firstIndex;
    int32_t     vertexOffset;
    uint32_t    firstInstance;
};

struct VkDrawIndirectCommand
{
    uint32_t    vertexCount;
    uint32_t    instanceCount;
    uint32_t    firstVertex;
    uint32_t    firstInstance;
};

static const uint32_t COLOR_RED     = 0xff0000ff;
static const uint32_t COLOR_GREEN   = 0xff00ff00;
static const uint32_t COLOR_BLUE    = 0xffff0000;
static const uint32_t COLOR_YELLOW  = 0xff00ffff;
static const uint32_t COLOR_MAGENTA = 0xffff00ff;

#ifdef VULKAN_ENABLE_DEBUG_UTILS
#define ASSERT(expr) \
    do { \
        if (!(expr)) \
        { \
            printf(__FILE__ ":%d shader assertion failed " #expr "\n", __LINE__); \
        } \
    } \
    while (false)
#else
#define ASSERT(expr)
#endif
