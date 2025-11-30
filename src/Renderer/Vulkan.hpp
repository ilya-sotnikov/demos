#pragma once

#include "Common.hpp"
#include "../Arena.hpp"

namespace Vulkan
{

struct QueueInfo
{
    u32 mFamilyIndex;
    u32 mQueueIndex;
    VkQueue mQueue;
};

struct Buffer
{
    VkBuffer mBuffer;
    VkDeviceMemory mMemory;
    void* mMapped;
};

struct Image
{
    VkImage mImage;
    VkImageView mView;
    VkDeviceMemory mMemory;
};

struct SampledImage
{
    VkImage mImage;
    VkImageView mView;
    VkDeviceMemory mMemory;
    VkSampler mSampler;
};

bool ExtensionIsAvailable(const char* name, Slice<VkExtensionProperties> extensions);
bool FindDepthFormat(VkFormat& result, VkPhysicalDevice physicalDevice);
bool FindDepthStencilFormat(VkFormat& result, VkPhysicalDevice physicalDevice);
QueueInfo GetQueue(VkPhysicalDevice device, VkQueueFlagBits flags, Arena scratch);
bool CreateShaderModule(VkShaderModule& result, VkDevice device, const char* shaderPath);

void ImageMemoryBarrier(
    VkCommandBuffer commandBuffer,
    VkImage image,
    VkImageAspectFlags aspectMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask
);
bool FindMemoryType(
    u32& memoryTypeIndex,
    VkPhysicalDevice physicalDevice,
    u32 typeFilter,
    VkMemoryPropertyFlags properties
);
bool CreateBuffer(
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties
);
bool CopyBuffer(
    VkBuffer dstBuffer,
    VkBuffer srcBuffer,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue queue,
    VkDeviceSize size
);

}
