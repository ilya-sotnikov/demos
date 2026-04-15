#pragma once

#include "RendererCommon.hpp"

#include <initializer_list>
#include <vector>

struct SDL_Window;

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
    VkFormat format;
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

// Based on Sebastian Aaltonen's approach with designated initializers:
// https://youtu.be/m3bW8d4Brec?si=7V8xsxykqCHbskvu&t=2233
// NOTE: resource descriptions are passed as rvalue references because they may
// contain std::initializer_list, that has very short lifetime, rvalue refs force
// temporaries and that prevents creating a local resource description variable
// and passing it as an argument with a dangling pointer.

struct BufferDesc
{
    Vulkan::Buffer& buffer;
    VkDeviceSize size;
    VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    VkMemoryPropertyFlags requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    VkDeviceSize minAlignment = 0;
    const char* debugName = nullptr;
};

struct ImageDesc
{
    Vulkan::Image& image;
    std::initializer_list<VkFormat> formats;
    VkImageUsageFlags usage = VK_IMAGE_USAGE_STORAGE_BIT;
    u32 width;
    u32 height;
    u32 depth = 1;
    u32 mipLevels = 1;
    u32 arrayLayers = 1;
    const char* debugName = nullptr;
};

struct ComputePipelineDesc
{
    Vulkan::Pipeline& pipeline;
    const char* shaderPath;
    VkDescriptorSetLayout extraDescriptorSetLayout = VK_NULL_HANDLE;
    std::initializer_list<i32> specializationConstants;
    const char* debugName = nullptr;
};

struct GraphicsPipelineDesc
{
    Vulkan::Pipeline& pipeline;
    std::initializer_list<const char*> shaderPaths;
    VkDescriptorSetLayout extraDescriptorSetLayout = VK_NULL_HANDLE;
    VkGraphicsPipelineCreateInfo& pipelineInfo;
    std::initializer_list<i32> specializationConstants;
    const char* debugName = nullptr;
};

struct Device
{
public:
    bool Create(VkSurfaceKHR& surface, SDL_Window* window);
    void Destroy();

    // TODO: managed handles.
    bool CreateBuffer(const BufferDesc&& desc) const;
    void UnmapBuffer(Vulkan::Buffer& buffer) const;
    void DestroyBuffer(Vulkan::Buffer& buffer) const;

    bool CreateImage(const ImageDesc&& desc) const;
    void DestroyImage(Vulkan::Image& image) const;

    bool CreateComputePipeline(const ComputePipelineDesc&& desc) const;
    bool CreateGraphicsPipeline(const GraphicsPipelineDesc&& desc) const;
    void DestroyPipeline(Vulkan::Pipeline& pipeline) const;

    VkInstance mInstance;
    VkDevice mDevice;
    VkPhysicalDevice mPhysicalDevice;
    VmaAllocator mVmaAllocator;
    Vulkan::QueueInfo mQueueInfo;
    char mGpuName[VK_MAX_PHYSICAL_DEVICE_NAME_SIZE];
};

}
