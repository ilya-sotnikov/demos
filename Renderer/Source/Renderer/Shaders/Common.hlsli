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
