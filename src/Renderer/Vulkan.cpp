#include "Vulkan.hpp"

#include "../Utils.hpp"

#include <string.h>

// gives ownership, call free()
struct FileData
{
    void* mData;
    long mSize;
};

static FileData FileRead(const char* path)
{
    assert(path);

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

    void* const res = malloc(static_cast<size_t>((fileSize + 1)) * sizeof(u8));
    if (!res)
    {
        fprintf(stderr, "%s: malloc failed (size %ld): %s\n", __func__, fileSize, strerror(errno));
        return result;
    }

    if (fread(res, sizeof(u8), static_cast<size_t>(fileSize), fp) != static_cast<size_t>(fileSize))
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

    result.mData = res;
    result.mSize = fileSize;

    return result;
}

namespace Vulkan
{

bool ExtensionIsAvailable(const char* name, Slice<VkExtensionProperties> extensions)
{
    assert(name);

    for (int i = 0; i < extensions.mCount; ++i)
    {
        if (!strcmp(name, extensions.mData[i].extensionName))
        {
            return true;
        }
    }
    return false;
}

QueueInfo GetQueue(VkPhysicalDevice device, VkQueueFlagBits flags, Arena scratch)
{
    u32 queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(device, &queueFamilyCount, nullptr);
    Slice<VkQueueFamilyProperties2> queueFamilies{};
    queueFamilies.mCount = static_cast<int>(queueFamilyCount);
    queueFamilies.mData = scratch.AllocOrDie<VkQueueFamilyProperties2>(queueFamilyCount);
    for (u32 i = 0; i < queueFamilyCount; ++i)
    {
        queueFamilies.mData[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
    }
    vkGetPhysicalDeviceQueueFamilyProperties2(device, &queueFamilyCount, queueFamilies.mData);

    QueueInfo queueInfo{};
    queueInfo.mQueueIndex = UINT32_MAX;
    queueInfo.mFamilyIndex = UINT32_MAX;
    for (u32 i = 0; i < queueFamilyCount; ++i)
    {
        if (queueFamilies.mData[i].queueFamilyProperties.queueFlags & flags)
        {
            queueInfo.mFamilyIndex = i;
            queueInfo.mQueueIndex = i;
            // queueInfo.Queue is set after creating a logical device and calling vkGetDeviceQueue.
            break;
        }
    }

    return queueInfo;
}

bool CreateShaderModule(VkShaderModule& result, VkDevice device, const char* shaderPath)
{
    assert(shaderPath);

    FileData fileData = FileRead(shaderPath);
    if (!fileData.mData)
    {
        return result;
    }
    DEFER(SAFE_FREE(fileData.mData));

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = static_cast<size_t>(fileData.mSize);
    createInfo.pCode = static_cast<u32*>(fileData.mData);

    if (vkCreateShaderModule(device, &createInfo, nullptr, &result) != VK_SUCCESS)
    {
        fprintf(stderr, "vkCreateShaderModule failed for %s\n", shaderPath);
        return result;
    }

    return result;
}

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
)
{
    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = aspectMask;
    subresourceRange.levelCount = 1;
    subresourceRange.layerCount = 1;

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

    VkDependencyInfo dependencyInfo{};
    dependencyInfo.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependencyInfo.imageMemoryBarrierCount = 1;
    dependencyInfo.pImageMemoryBarriers = &barrier;

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

bool FindMemoryType(
    u32& memoryTypeIndex,
    VkPhysicalDevice physicalDevice,
    u32 typeFilter,
    VkMemoryPropertyFlags properties
)
{
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memoryProperties);

    assert(memoryProperties.memoryTypeCount <= 32);

    for (u32 i = 0; i < memoryProperties.memoryTypeCount; ++i)
    {
        const bool typeIsAvailable = typeFilter & (1U << i);
        const bool propertyIsSet
            = (memoryProperties.memoryTypes[i].propertyFlags & properties) == properties;
        if (typeIsAvailable && propertyIsSet)
        {
            memoryTypeIndex = i;
            return true;
        }
    }

    return false;
}

bool CreateBuffer(
    VkBuffer& buffer,
    VkDeviceMemory& bufferMemory,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties
)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, buffer, &memoryRequirements);

    u32 memoryTypeIndex = 0;
    const bool memoryTypeResult = FindMemoryType(
        memoryTypeIndex,
        physicalDevice,
        memoryRequirements.memoryTypeBits,
        properties
    );
    if (!memoryTypeResult)
    {
        fprintf(stderr, "Vulkan failed to find a suitable memory type\n");
        return false;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = memoryTypeIndex;
    VK_CHECK(vkAllocateMemory(device, &allocateInfo, nullptr, &bufferMemory));
    VK_CHECK(vkBindBufferMemory(device, buffer, bufferMemory, 0));

    return true;
}

// WARN: allocates a command buffer and waits for the transfer queue to become
// idle every time, don't use in main loop.
bool CopyBuffer(
    VkBuffer dstBuffer,
    VkBuffer srcBuffer,
    VkDevice device,
    VkCommandPool commandPool,
    VkQueue queue,
    VkDeviceSize size
)
{
    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1;
    VkCommandBuffer commandBuffer{};
    VK_CHECK(vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));

    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &region);

    VK_CHECK(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));

    return true;
}

}
