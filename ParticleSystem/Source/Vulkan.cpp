#include "Vulkan.hpp"

#include "Utils.hpp"
#include "Shaders/SharedConfig.slang"

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
    VkImageAspectFlags aspectMask,
    VkImageLayout oldLayout,
    VkImageLayout newLayout,
    VkPipelineStageFlags2 srcStageMask,
    VkAccessFlags2 srcAccessMask,
    VkPipelineStageFlags2 dstStageMask,
    VkAccessFlags2 dstAccessMask
)
{
    DEBUG_ASSERT(image);

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
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags properties,
    const char* debugName
)
{
    DEBUG_ASSERT(physicalDevice);
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(size > 0);

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VK_CHECK(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer.buffer));

    VkMemoryRequirements memoryRequirements{};
    vkGetBufferMemoryRequirements(device, buffer.buffer, &memoryRequirements);

    u32 memoryTypeIdx = 0;
    const bool memoryTypeResult = FindMemoryType(
        memoryTypeIdx,
        physicalDevice,
        memoryRequirements.memoryTypeBits,
        properties
    );
    if (!memoryTypeResult)
    {
        fprintf(stderr, "vulkan: failed to find a suitable memory type\n");
        return false;
    }

    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = memoryTypeIdx;
    VK_CHECK(vkAllocateMemory(device, &allocateInfo, nullptr, &buffer.memory));
    VK_CHECK(vkBindBufferMemory(device, buffer.buffer, buffer.memory, 0));

    if (properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
    {
        VK_CHECK(vkMapMemory(device, buffer.memory, 0, size, 0, &buffer.mapped));
    }

    if (debugName)
    {
        return Vulkan::DebugNameObject(
            device,
            VK_OBJECT_TYPE_BUFFER,
            reinterpret_cast<u64>(buffer.buffer),
            debugName
        );
    }

    return true;
}

void Vulkan::DestroyBuffer(VkDevice device, Vulkan::Buffer& buffer)
{
    DEBUG_ASSERT(device);

    if (buffer.mapped)
    {
        vkUnmapMemory(device, buffer.memory);
        buffer.mapped = nullptr;
    }
    vkFreeMemory(device, buffer.memory, nullptr);
    buffer.memory = VK_NULL_HANDLE;
    vkDestroyBuffer(device, buffer.buffer, nullptr);
    buffer.buffer = VK_NULL_HANDLE;
}

bool Vulkan::UploadToBufferSlow(
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandBuffer commandBuffer,
    VkQueue queue,
    VkBuffer dstBuffer,
    const void* data,
    size_t dataSize
)
{
    DEBUG_ASSERT(physicalDevice);
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(commandBuffer);
    DEBUG_ASSERT(queue);
    DEBUG_ASSERT(dstBuffer);
    DEBUG_ASSERT(data);
    DEBUG_ASSERT(dataSize > 0);

    Vulkan::Buffer stagingBuffer{};
    if (!Vulkan::CreateBuffer(
            stagingBuffer,
            physicalDevice,
            device,
            dataSize,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
        ))
    {
        return false;
    }
    DEFER(Vulkan::DestroyBuffer(device, stagingBuffer));

    memcpy(stagingBuffer.mapped, data, dataSize);

    const VkCommandBuffer cmd = commandBuffer;

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));

    VkBufferCopy region{};
    region.size = dataSize;

    vkCmdCopyBuffer(cmd, stagingBuffer.buffer, dstBuffer, 1, &region);

    VK_CHECK(vkEndCommandBuffer(cmd));

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmd;

    VK_CHECK(vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(queue));

    return true;
}

bool Vulkan::CreateAndUploadBufferSlow(
    Vulkan::Buffer& buffer,
    VkPhysicalDevice physicalDevice,
    VkDevice device,
    VkCommandBuffer commandBuffer,
    VkQueue queue,
    VkBufferUsageFlags usage,
    const void* data,
    size_t dataSize,
    const char* debugName
)
{
    DEBUG_ASSERT(physicalDevice);
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(commandBuffer);
    DEBUG_ASSERT(queue);
    DEBUG_ASSERT(usage);
    DEBUG_ASSERT(data);
    DEBUG_ASSERT(dataSize > 0);

    bool result = Vulkan::CreateBuffer(
        buffer,
        physicalDevice,
        device,
        dataSize,
        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        debugName
    );
    if (!result)
    {
        return false;
    }

    result = Vulkan::UploadToBufferSlow(
        physicalDevice,
        device,
        commandBuffer,
        queue,
        buffer.buffer,
        data,
        dataSize
    );
    if (!result)
    {
        return false;
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
