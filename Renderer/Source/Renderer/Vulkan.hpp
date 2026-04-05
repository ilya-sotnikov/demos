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
    // TODO: VkExtent2D?
};

struct Swapchain
{
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    VkSurfaceFormatKHR surfaceFormat;
    std::vector<Vulkan::Image> images;
    u32 minImageCount;
};

union DescriptorInfo
{
    VkDescriptorImageInfo image;
    VkDescriptorBufferInfo buffer;
    VkAccelerationStructureKHR accelerationStructure;

    DescriptorInfo() = default;

    DescriptorInfo(VkBuffer buffer, VkDeviceSize offset = 0, VkDeviceSize range = VK_WHOLE_SIZE)
        : buffer{buffer, offset, range}
    { }

    DescriptorInfo(
        VkImageView imageView,
        VkImageLayout imageLayout,
        VkSampler sampler = VK_NULL_HANDLE
    )
        : image{sampler, imageView, imageLayout}
    { }

    DescriptorInfo(VkSampler sampler)
        : image{sampler, VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED} { }

    DescriptorInfo(VkAccelerationStructureKHR accelerationStructure)
        : accelerationStructure{accelerationStructure}
    { }
};

struct Pipeline
{
    VkPipeline pipeline;
    VkPipelineLayout layout;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorUpdateTemplate descriptorUpdateTemplate;
    u32 pushConstantSize;
};

bool FindSupportedImageFormat(
    VkFormat& result,
    VkPhysicalDevice physicalDevice,
    VkImageUsageFlags usageFlags,
    std::initializer_list<VkFormat> formats
);
bool ExtensionIsAvailable(const char* name, Slice<VkExtensionProperties> extensions);
QueueInfo GetQueue(VkPhysicalDevice device, VkQueueFlagBits flags);

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
    u32 depth = 1,
    const char* name = "",
    u32 mipLevels = 1,
    u32 arrayLayers = 1
);
void DestroyImage(Vulkan::Image& image, VkDevice device, VmaAllocator vmaAllocator);

bool CreateComputePipeline(
    Vulkan::Pipeline& pipeline,
    VkDevice device,
    const char* shaderPath,
    VkDescriptorSetLayout extraDescriptorSetLayout,
    const char* debugName
);
bool CreateGraphicsPipeline(
    Vulkan::Pipeline& pipeline,
    VkDevice device,
    std::initializer_list<const char*> shaderPaths,
    VkDescriptorSetLayout extraDescriptorSetLayout,
    VkGraphicsPipelineCreateInfo& pipelineInfo,
    const char* debugName
);
void DestroyPipeline(Vulkan::Pipeline& pipeline, VkDevice device);

void CmdPushDescriptors(
    VkCommandBuffer cmd,
    const Vulkan::Pipeline& pipeline,
    std::initializer_list<Vulkan::DescriptorInfo> descriptorInfos
);

bool DebugNameObject(
    VkDevice device,
    VkObjectType objectType,
    u64 objectHandle,
    const char* objectName
);

}
