#pragma once

#include "../Common.hpp"

#define VK_NO_PROTOTYPES
#include "volk/volk.h"
#include <vulkan/vulkan.h>
#include <vulkan/vk_enum_string_helper.h>

#ifndef VK_CHECK_ACTION
#define VK_CHECK_ACTION return false
#endif

#ifndef VK_CHECK_PRINT_ERROR
#define VK_CHECK_PRINT_ERROR(vulkanResult) \
    do \
    { \
        fprintf( \
            stderr, \
            "Vulkan error (%s:%d): %s\n", \
            __FILE__, \
            __LINE__, \
            string_VkResult(vulkanResult) \
        ); \
    } \
    while (0)
#endif

#define VK_CHECK(x) \
    do \
    { \
        const VkResult vulkanResultTmp = x; \
        if (vulkanResultTmp != VK_SUCCESS) \
        { \
            VK_CHECK_PRINT_ERROR(vulkanResultTmp); \
            VK_CHECK_ACTION; \
        } \
    } \
    while (0)
