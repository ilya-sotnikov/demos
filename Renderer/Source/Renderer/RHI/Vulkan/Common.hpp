#pragma once

#define VK_NO_PROTOTYPES
#include <volk.h>
#define VMA_VULKAN_VERSION 1004000
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wnullability-completeness"
#pragma clang diagnostic ignored "-Wnullability-extension"
#pragma clang diagnostic ignored "-Wunused-parameter"
#pragma clang diagnostic ignored "-Wmissing-field-initializers"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wunused-variable"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4100)
#pragma warning(disable : 4189)
#pragma warning(disable : 4324)
#pragma warning(disable : 26110)
#endif

#include <vk_mem_alloc.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

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
            "vulkan error (%s:%d): %s\n", \
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
        const VkResult vulkanResultTmp_ = x; \
        if (vulkanResultTmp_ != VK_SUCCESS) \
        { \
            VK_CHECK_PRINT_ERROR(vulkanResultTmp_); \
            VK_CHECK_ACTION; \
        } \
    } \
    while (0)
