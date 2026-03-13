#include "Vulkan.hpp"

#include "RendererCommon.hpp"
#include "Shaders/SharedConfig.slang"

#include "../Utils.hpp"

#include <string.h>

struct FileData
{
    void* data;
    long size;
};

// gives ownership, call free()
static FileData FileRead(const char* path)
{
    DEBUG_ASSERT(path);

    FileData result{};

    FILE* const fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "%s: fopen %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    DEFER(fclose(fp));

    if (fseek(fp, 0, SEEK_END))
    {
        fprintf(stderr, "%s: fseek SEEK_END %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    const long fileSize = ftell(fp);
    if (fileSize == -1)
    {
        fprintf(stderr, "%s: ftell %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    if (fseek(fp, 0, SEEK_SET))
    {
        fprintf(stderr, "%s: fseek SEEK_SET %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }

    void* const res = malloc(size_t((fileSize + 1)) * sizeof(u8));
    if (!res)
    {
        fprintf(stderr, "%s: malloc failed (size %ld): %s\n", __func__, fileSize, strerror(errno));
        return result;
    }

    if (fread(res, sizeof(u8), size_t(fileSize), fp) != size_t(fileSize))
    {

        if (feof(fp))
        {
            fprintf(stderr, "%s: fread %s failed: EOF\n", __func__, path);
        }
        else if (ferror(fp))
        {
            fprintf(stderr, "%s: fread %s failed: %s\n", __func__, path, strerror(errno));
        }
        free(res);
        return result;
    }

    result.data = res;
    result.size = fileSize;

    return result;
}

bool Vulkan::FindSupportedImageFormat(
    VkFormat& result,
    VkPhysicalDevice physicalDevice,
    VkImageUsageFlags usageFlags,
    std::initializer_list<VkFormat> formats
)
{
    for (VkFormat format : formats)
    {
        VkPhysicalDeviceImageFormatInfo2 formatInfo{};
        formatInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
        formatInfo.format = format;
        formatInfo.type = VK_IMAGE_TYPE_2D;
        formatInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        formatInfo.usage = usageFlags;

        VkImageFormatProperties2 imageProperties{};
        imageProperties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;

        if (vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, &formatInfo, &imageProperties)
            == VK_SUCCESS)
        {
            result = format;
            return true;
        }
    }

    return false;
}

bool Vulkan::ExtensionIsAvailable(const char* name, Slice<VkExtensionProperties> extensions)
{
    DEBUG_ASSERT(name);

    for (int i = 0; i < extensions.count; ++i)
    {
        if (!strcmp(name, extensions.data[i].extensionName))
        {
            return true;
        }
    }
    return false;
}

Vulkan::QueueInfo Vulkan::GetQueue(VkPhysicalDevice device, VkQueueFlagBits flags)
{
    DEBUG_ASSERT(device);

    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(device, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties2> queueFamilies(queueFamilyCount);
    for (u32 i = 0; i < queueFamilyCount; ++i)
    {
        queueFamilies[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(device, &queueFamilyCount, queueFamilies.data());

    Vulkan::QueueInfo queueInfo{};
    queueInfo.queueIdx = UINT32_MAX;
    queueInfo.familyIdx = UINT32_MAX;
    for (u32 i = 0; i < queueFamilyCount; ++i)
    {
        if (queueFamilies[i].queueFamilyProperties.queueFlags & flags)
        {
            DEBUG_ASSERT(queueFamilies[i].queueFamilyProperties.queueCount > 0);
            queueInfo.familyIdx = i;
            queueInfo.queueIdx = 0;
            // queueInfo.queue is set after creating a logical device and calling vkGetDeviceQueue.
            break;
        }
    }

    return queueInfo;
}

bool Vulkan::CreateShaderModule(VkShaderModule& result, VkDevice device, const char* shaderPath)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(shaderPath);

    FileData fileData = FileRead(shaderPath);
    if (!fileData.data)
    {
        return false;
    }
    DEFER(SAFE_FREE(fileData.data));

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = size_t(fileData.size);
    createInfo.pCode = static_cast<u32*>(fileData.data);

    if (vkCreateShaderModule(device, &createInfo, nullptr, &result) != VK_SUCCESS)
    {
        fprintf(stderr, "vkCreateShaderModule failed for %s\n", shaderPath);
        return false;
    }

    return true;
}

VkMemoryBarrier2 Vulkan::MemoryBarrier(
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask
)
{
    VkMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStageMask;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.dstAccessMask = dstAccessMask;

    return barrier;
}

VkBufferMemoryBarrier2 Vulkan::BufferMemoryBarrier(
    VkBuffer buffer,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask,
    VkDeviceSize size
)
{
    DEBUG_ASSERT(buffer);
    DEBUG_ASSERT(size > 0);

    VkBufferMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStageMask;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.buffer = buffer;
    barrier.size = size;

    return barrier;
}

VkImageMemoryBarrier2 Vulkan::ImageMemoryBarrier(
    VkImage image,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask,
    VkImageAspectFlags aspectMask,
    u32 levelCount,
    u32 layerCount
)
{
    DEBUG_ASSERT(image);

    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = aspectMask;
    subresourceRange.levelCount = levelCount;
    subresourceRange.layerCount = layerCount;

    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = srcStageMask;
    barrier.srcAccessMask = srcAccessMask;
    barrier.dstStageMask = dstStageMask;
    barrier.dstAccessMask = dstAccessMask;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = subresourceRange;

    return barrier;
}

void Vulkan::CmdMemoryBarrier(VkCommandBuffer cmd, std::initializer_list<VkMemoryBarrier2> barriers)
{
    DEBUG_ASSERT(cmd);
    DEBUG_ASSERT(barriers.size() > 0);

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.memoryBarrierCount = u32(barriers.size());
    dependencyInfo.pMemoryBarriers = barriers.begin();
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

void Vulkan::CmdBufferMemoryBarrier(
    VkCommandBuffer cmd,
    std::initializer_list<VkBufferMemoryBarrier2> barriers
)
{
    DEBUG_ASSERT(cmd);
    DEBUG_ASSERT(barriers.size() > 0);

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.bufferMemoryBarrierCount = u32(barriers.size());
    dependencyInfo.pBufferMemoryBarriers = barriers.begin();
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

void Vulkan::CmdImageMemoryBarrier(
    VkCommandBuffer cmd,
    std::initializer_list<VkImageMemoryBarrier2> barriers
)
{
    DEBUG_ASSERT(cmd);
    DEBUG_ASSERT(barriers.size() > 0);

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = u32(barriers.size());
    dependencyInfo.pImageMemoryBarriers = barriers.begin();
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

void Vulkan::CmdBarrier(
    VkCommandBuffer cmd,
    std::initializer_list<VkMemoryBarrier2> memoryBarriers,
    std::initializer_list<VkBufferMemoryBarrier2> bufferMemoryBarriers,
    std::initializer_list<VkImageMemoryBarrier2> imageMemoryBarriers
)
{
    DEBUG_ASSERT(cmd);
    DEBUG_ASSERT(
        (memoryBarriers.size() > 0) || (bufferMemoryBarriers.size() > 0)
        || (imageMemoryBarriers.size() > 0)
    );

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.memoryBarrierCount = u32(memoryBarriers.size());
    dependencyInfo.pMemoryBarriers = memoryBarriers.begin();
    dependencyInfo.bufferMemoryBarrierCount = u32(bufferMemoryBarriers.size());
    dependencyInfo.pBufferMemoryBarriers = bufferMemoryBarriers.begin();
    dependencyInfo.imageMemoryBarrierCount = u32(imageMemoryBarriers.size());
    dependencyInfo.pImageMemoryBarriers = imageMemoryBarriers.begin();
    vkCmdPipelineBarrier2(cmd, &dependencyInfo);
}

bool Vulkan::FindMemoryType(
    u32& memoryTypeIdx,
    VkPhysicalDevice physicalDevice,
    u32 typeFilter,
    VkMemoryPropertyFlags properties
)
{
    DEBUG_ASSERT(physicalDevice);

    VkPhysicalDeviceMemoryProperties2 memProp{};
    memProp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2;
    vkGetPhysicalDeviceMemoryProperties2(physicalDevice, &memProp);

    ASSERT(memProp.memoryProperties.memoryTypeCount <= 32);

    for (u32 i = 0; i < memProp.memoryProperties.memoryTypeCount; ++i)
    {
        const bool typeIsAvailable = typeFilter & (1U << i);
        const bool propertyIsSet
            = (memProp.memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
        if (typeIsAvailable && propertyIsSet)
        {
            memoryTypeIdx = i;
            return true;
        }
    }

    return false;
}

bool Vulkan::CreateBuffer(
    Vulkan::Buffer& buffer,
    VkDevice device,
    VmaAllocator vmaAllocator,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags requiredFlags,
    const char* debugName,
    VkDeviceSize minAlignment
)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(vmaAllocator);
    DEBUG_ASSERT(size > 0);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.requiredFlags = requiredFlags;

    if (minAlignment > 0)
    {
        VK_CHECK(vmaCreateBufferWithAlignment(
            vmaAllocator,
            &bufferInfo,
            &allocationInfo,
            minAlignment,
            &buffer.buffer,
            &buffer.allocation,
            nullptr
        ));
    }
    else
    {
        VK_CHECK(vmaCreateBuffer(
            vmaAllocator,
            &bufferInfo,
            &allocationInfo,
            &buffer.buffer,
            &buffer.allocation,
            nullptr
        ));
    }

    if (requiredFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        VK_CHECK(vmaMapMemory(vmaAllocator, buffer.allocation, &buffer.mapped));
    }

    if (usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)
    {
        VkBufferDeviceAddressInfo addressInfo{};
        addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
        addressInfo.buffer = buffer.buffer;
        buffer.deviceAddress = vkGetBufferDeviceAddress(device, &addressInfo);
    }

    if (debugName)
    {
        if (!Vulkan::DebugNameObject(
                device,
                VK_OBJECT_TYPE_BUFFER,
                reinterpret_cast<u64>(buffer.buffer),
                debugName
            ))
        {
            return false;
        }
    }

    return true;
}

void Vulkan::UnmapBuffer(Vulkan::Buffer& buffer, VmaAllocator vmaAllocator)
{
    DEBUG_ASSERT(vmaAllocator);

    if (buffer.mapped)
    {
        vmaUnmapMemory(vmaAllocator, buffer.allocation);
        buffer.mapped = nullptr;
    }
}

void Vulkan::DestroyBuffer(Vulkan::Buffer& buffer, VmaAllocator vmaAllocator)
{
    DEBUG_ASSERT(vmaAllocator);

    if (buffer.mapped)
    {
        vmaUnmapMemory(vmaAllocator, buffer.allocation);
        buffer.mapped = nullptr;
    }
    vmaDestroyBuffer(vmaAllocator, buffer.buffer, buffer.allocation);
    buffer.buffer = VK_NULL_HANDLE;
    buffer.allocation = VK_NULL_HANDLE;
}

bool Vulkan::CreateImage(
    Vulkan::Image& image,
    VkDevice device,
    VmaAllocator vmaAllocator,
    VkFormat format,
    VkImageUsageFlags usage,
    u32 width,
    u32 height,
    const char* name,
    u32 mipLevels
)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(vmaAllocator);
    DEBUG_ASSERT(height > 0);
    DEBUG_ASSERT(width > 0);
    DEBUG_ASSERT(mipLevels > 0);

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = format;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = usage;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    VK_CHECK(vmaCreateImage(
        vmaAllocator,
        &imageInfo,
        &allocationInfo,
        &image.image,
        &image.allocation,
        nullptr
    ));

    VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    switch (format)
    {
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM:
        aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        break;
    default:
        break;
    }

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.image = image.image;
    viewInfo.format = format;
    viewInfo.subresourceRange = {};
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.layerCount = 1;
    viewInfo.subresourceRange.levelCount = mipLevels;
    VK_CHECK(vkCreateImageView(device, &viewInfo, nullptr, &image.view));

    if (name)
    {
        if (!Vulkan::DebugNameObject(
                device,
                VK_OBJECT_TYPE_IMAGE,
                reinterpret_cast<u64>(image.image),
                name
            ))
        {
            return false;
        }

        if (!Vulkan::DebugNameObject(
                device,
                VK_OBJECT_TYPE_IMAGE_VIEW,
                reinterpret_cast<u64>(image.view),
                name
            ))
        {
            return false;
        }
    }

    return true;
}

void Vulkan::DestroyImage(Vulkan::Image& image, VkDevice device, VmaAllocator vmaAllocator)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(vmaAllocator);

    vkDestroyImageView(device, image.view, nullptr);
    image.view = VK_NULL_HANDLE;
    vmaDestroyImage(vmaAllocator, image.image, image.allocation);
    image.allocation = VK_NULL_HANDLE;
    image.image = VK_NULL_HANDLE;
}

bool Vulkan::CreateComputePipeline(
    VkPipeline& pipeline,
    VkDevice device,
    VkPipelineLayout layout,
    VkShaderModule shaderModule,
    const char* mainName,
    const char* debugName
)
{
    DEBUG_ASSERT(layout);
    DEBUG_ASSERT(shaderModule);
    DEBUG_ASSERT(mainName);

    VkPipelineShaderStageCreateInfo shaderStageInfo{};
    shaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    shaderStageInfo.module = shaderModule;
    shaderStageInfo.pName = mainName;

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage = shaderStageInfo;
    pipelineInfo.layout = layout;

    VK_CHECK(
        vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline)
    );

    if (debugName)
    {
        if (!Vulkan::DebugNameObject(
                device,
                VK_OBJECT_TYPE_PIPELINE,
                reinterpret_cast<u64>(pipeline),
                debugName
            ))
        {
            return false;
        }
    }

    return true;
}

bool Vulkan::DebugNameObject(
    VkDevice device,
    VkObjectType objectType,
    u64 objectHandle,
    const char* objectName
)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(objectHandle);
    DEBUG_ASSERT(objectName);

    (void)device;

    VkDebugUtilsObjectNameInfoEXT nameInfo{};
    nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    nameInfo.objectType = objectType;
    nameInfo.objectHandle = objectHandle;
    nameInfo.pObjectName = objectName;

#ifdef VULKAN_ENABLE_DEBUG_UTILS
    VK_CHECK(vkSetDebugUtilsObjectNameEXT(device, &nameInfo));
#endif

    return true;
}
