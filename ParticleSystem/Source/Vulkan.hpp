#pragma once

#include "Common.hpp"

#include <initializer_list>
#include <vector>
#include <volk.h>
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

namespace Vulkan
{

struct QueueInfo
{
    u32 familyIdx;
    u32 queueIdx;
    VkQueue queue;
};

struct Buffer
{
    VkBuffer buffer;
    VkDeviceMemory memory;
    void* mapped;
};

struct Image
{
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
};

struct SampledImage
{
    VkImage image;
    VkImageView view;
    VkDeviceMemory memory;
    VkSampler sampler;
};

struct Swapchain
{
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    VkSurfaceFormatKHR surfaceFormat;
    std::vector<Vulkan::Image> images;
    u32 minImageCount;
};

struct Pipeline
{
    VkPipelineLayout layout;
    VkPipeline pipeline;
    VkDescriptorSetLayout descriptorSetLayout;
};

union DescriptorResourceInfo
{
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;

    DescriptorResourceInfo() = default;

    DescriptorResourceInfo(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
        : buffer{buffer, offset, range}
    {
        DEBUG_ASSERT(buffer);
        DEBUG_ASSERT(range > 0);
    }

    DescriptorResourceInfo(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
        : image{sampler, imageView, imageLayout}
    {
        DEBUG_ASSERT(sampler);
        DEBUG_ASSERT(imageView);
        DEBUG_ASSERT(imageLayout);
    }
};

struct DescriptorBindingInfo
{
    const char* name;
    u32 binding;
    VkDescriptorType descriptorType;
    VkShaderStageFlags stageFlags;
    DescriptorResourceInfo resourceInfo;
};

bool ExtensionIsAvailable(const char* name, Slice<VkExtensionProperties> extensions);
QueueInfo GetQueue(VkPhysicalDevice device, VkQueueFlagBits flags);
bool CreateShaderModule(VkShaderModule& result, VkDevice device, const char* shaderPath);

VkImageMemoryBarrier2 ImageMemoryBarrier(
    VkImage image,
    VkImageAspectFlags aspectMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask
);
VkBufferMemoryBarrier2 BufferMemoryBarrier(
    VkBuffer buffer,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask,
    VkDeviceSize size = VK_WHOLE_SIZE
);
VkMemoryBarrier2 MemoryBarrier(
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask
);
void CmdMemoryBarrier(VkCommandBuffer cmd, std::initializer_list<VkMemoryBarrier2> barriers);
void CmdBufferMemoryBarrier(
    VkCommandBuffer cmd,
    std::initializer_list<VkBufferMemoryBarrier2> barriers
);
void CmdImageMemoryBarrier(
    VkCommandBuffer cmd,
    std::initializer_list<VkImageMemoryBarrier2> barriers
);
void CmdBarrier(
    VkCommandBuffer cmd,
    std::initializer_list<VkMemoryBarrier2> memoryBarriers,
    std::initializer_list<VkBufferMemoryBarrier2> bufferMemoryBarriers,
    std::initializer_list<VkImageMemoryBarrier2> imageMemoryBarriers
);
bool FindMemoryType(
    u32& memoryTypeIdx,
    VkPhysicalDevice physicalDevice,
    u32 typeFilter,
    VkMemoryPropertyFlags properties
);
bool CreateBuffer(
    Vulkan::Buffer& buffer,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    const char* debugName = nullptr
);
void DestroyBuffer(VkDevice device, Vulkan::Buffer& buffer);
bool UploadToBufferSlow(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandBuffer commandBuffer,
    VkQueue queue,
    VkBuffer dstBuffer,
    const void* data,
    size_t dataSize
);
bool CreateAndUploadBufferSlow(
    Vulkan::Buffer& buffer,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandBuffer commandBuffer,
    VkQueue queue,
    VkBufferUsageFlags usage,
    const void* data,
    size_t dataSize,
    const char* debugName
);
bool DebugNameObject(
    VkDevice device,
    VkObjectType objectType,
    u64 objectHandle,
    const char* objectName
);

}
