#pragma once

#include "RendererCommon.hpp"

#include <initializer_list>
#include <vector>

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
    void* mapped;
    VkDeviceAddress deviceAddress;
    VmaAllocation allocation;
};

struct Image
{
    VkImage image;
    VkImageView view;
    VmaAllocation allocation;
};

struct Swapchain
{
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    VkSurfaceFormatKHR surfaceFormat;
    std::vector<Vulkan::Image> images;
    u32 minImageCount;
};

// TODO: remove, since every pipeline is sharing a single layout and a single descriptor set layout.
struct Pipeline
{
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout descriptorSetLayout;
};

union DescriptorResourceInfo
{
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;

    DescriptorResourceInfo() = default;

    DescriptorResourceInfo(VkBuffer buffer, VkDeviceSize offset, VkDeviceSize range)
        : buffer{buffer, offset, range}
    { }

    DescriptorResourceInfo(VkSampler sampler, VkImageView imageView, VkImageLayout imageLayout)
        : image{sampler, imageView, imageLayout}
    { }
};

struct DescriptorBindingInfo
{
    const char* name;
    u32 binding;
    VkDescriptorType descriptorType;
    VkShaderStageFlags stageFlags;
    DescriptorResourceInfo resourceInfo;
};

bool FindSupportedImageFormat(
    VkFormat& result,
    VkPhysicalDevice physicalDevice,
    VkImageUsageFlags usageFlags,
    std::initializer_list<VkFormat> formats
);
bool ExtensionIsAvailable(const char* name, Slice<VkExtensionProperties> extensions);
QueueInfo GetQueue(VkPhysicalDevice device, VkQueueFlagBits flags);
bool CreateShaderModule(VkShaderModule& result, VkDevice device, const char* shaderPath);

VkImageMemoryBarrier2 ImageMemoryBarrier(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask,
    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
    u32 levelCount = VK_REMAINING_MIP_LEVELS,
    u32 layerCount = 1
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
    VkDevice device,
    VmaAllocator vmaAllocator,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags requiredFlags,
    const char* debugName = "",
    VkDeviceSize minAlignment = 0
);
void UnmapBuffer(Vulkan::Buffer& buffer, VmaAllocator vmaAllocator);
void DestroyBuffer(Vulkan::Buffer& buffer, VmaAllocator vmaAllocator);

bool CreateImage(
    Vulkan::Image& image,
    VkDevice device,
    VmaAllocator vmaAllocator,
    VkFormat format,
    VkImageUsageFlags usage,
    u32 width,
    u32 height,
    const char* name = "",
    u32 mipLevels = 1
);
void DestroyImage(Vulkan::Image& image, VkDevice device, VmaAllocator vmaAllocator);

bool CreateComputePipeline(
    VkPipeline& pipeline,
    VkDevice device,
    VkPipelineLayout layout,
    VkShaderModule shaderModule,
    const char* mainName,
    const char* debugName = ""
);

bool DebugNameObject(
    VkDevice device,
    VkObjectType objectType,
    u64 objectHandle,
    const char* objectName
);

}
