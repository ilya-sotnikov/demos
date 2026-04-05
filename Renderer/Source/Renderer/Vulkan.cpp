#include "Vulkan.hpp"

#include "RendererCommon.hpp"
#include "Shaders/SharedConfig.hlsli"

#include "../Utils.hpp"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#endif

#include <spirv_cross/spirv_cross.hpp>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

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

struct Shader
{
    VkShaderModule module;
    VkShaderStageFlagBits stage;
    u32 pushConstantSize;
};

static bool CreateShader(
    Shader& shader,
    std::vector<VkDescriptorSetLayoutBinding>& descriptorSetLayoutBindings,
    VkDevice device,
    const char* shaderPath
)
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

    if (vkCreateShaderModule(device, &createInfo, nullptr, &shader.module) != VK_SUCCESS)
    {
        fprintf(stderr, "vulkan: vkCreateShaderModule failed for %s\n", shaderPath);
        return false;
    }

    spirv_cross::Compiler compiler{
        static_cast<u32*>(fileData.data),
        size_t(fileData.size) / sizeof(u32)
    };

    const spirv_cross::SmallVector<spirv_cross::EntryPoint> entryPoints
        = compiler.get_entry_points_and_stages();

    // NOTE: AFAIK in dxc multiple entry points are buggy. Also, I've encountered a strange bug (?)
    // with multiple compute shader entry points in Nsight Graphics, when profiling, some samples
    // always end up in the first entry point in a file when in reality it should not execute at
    // all. Maybe PEBKAC, but for now I'll just always use 1 entry point per file.
    ASSERT(entryPoints.size() == 1);

    switch (entryPoints[0].execution_model)
    {
    case spirv_cross::ExecutionModelVertex:
        shader.stage = VK_SHADER_STAGE_VERTEX_BIT;
        break;
    case spirv_cross::ExecutionModelFragment:
        shader.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        break;
    case spirv_cross::ExecutionModelGLCompute:
        shader.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        break;
    case spirv_cross::ExecutionModelMeshEXT:
        shader.stage = VK_SHADER_STAGE_MESH_BIT_EXT;
        break;
    case spirv_cross::ExecutionModelTaskEXT:
        shader.stage = VK_SHADER_STAGE_TASK_BIT_EXT;
        break;
    default:
        fprintf(stderr, "unhandled shader execution model: %d", entryPoints[0].execution_model);
        return false;
    }

    spirv_cross::ShaderResources shaderResources = compiler.get_shader_resources();

    for (const spirv_cross::Resource& r : shaderResources.separate_images)
    {
        VkDescriptorSetLayoutBinding binding{};
        if (compiler.get_decoration(r.id, spv::DecorationDescriptorSet) == 1)
        {
            // Skip descriptor set 1 (bindless textures).
            continue;
        }

        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);

        binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.separate_samplers)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.storage_images)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.sampled_images)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.uniform_buffers)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.storage_buffers)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        descriptorSetLayoutBindings.push_back(binding);
    }

    for (const spirv_cross::Resource& r : shaderResources.acceleration_structures)
    {
        VkDescriptorSetLayoutBinding binding{};
        binding.binding = compiler.get_decoration(r.id, spv::DecorationBinding);
        binding.descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
        binding.descriptorCount = 1;
        binding.stageFlags = VK_SHADER_STAGE_ALL;

        descriptorSetLayoutBindings.push_back(binding);
    }

    if (!shaderResources.push_constant_buffers.empty())
    {
        ASSERT(shaderResources.push_constant_buffers.size() == 1);

        const spirv_cross::SPIRType type
            = compiler.get_type(shaderResources.push_constant_buffers[0].base_type_id);
        const size_t size = compiler.get_declared_struct_size(type);

        shader.pushConstantSize = u32(size);
    }

    return true;
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

    usage |= VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT;

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

    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = buffer.buffer;
    buffer.deviceAddress = vkGetBufferDeviceAddress(device, &addressInfo);

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
    u32 depth,
    const char* name,
    u32 mipLevels,
    u32 arrayLayers
)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(vmaAllocator);
    DEBUG_ASSERT(height > 0);
    DEBUG_ASSERT(width > 0);
    DEBUG_ASSERT(depth > 0);
    DEBUG_ASSERT(mipLevels > 0);
    DEBUG_ASSERT(arrayLayers > 0);

    VkImageType imageType = VK_IMAGE_TYPE_2D;
    VkImageViewType imageViewType = VK_IMAGE_VIEW_TYPE_2D;
    if (depth > 1)
    {
        imageType = VK_IMAGE_TYPE_3D;
        imageViewType = VK_IMAGE_VIEW_TYPE_3D;
    }
    else if (arrayLayers > 1)
    {
        imageViewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    }

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = imageType;
    imageInfo.format = format;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = depth;
    imageInfo.mipLevels = mipLevels;
    imageInfo.arrayLayers = arrayLayers;
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
    viewInfo.viewType = imageViewType;
    viewInfo.image = image.image;
    viewInfo.format = format;
    viewInfo.subresourceRange = {};
    viewInfo.subresourceRange.aspectMask = aspectMask;
    viewInfo.subresourceRange.layerCount = arrayLayers;
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
    Vulkan::Pipeline& pipeline,
    VkDevice device,
    const char* shaderPath,
    VkDescriptorSetLayout extraDescriptorSetLayout,
    const char* debugName
)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(shaderPath);

    std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    descriptorSetLayoutBindings.reserve(32);
    Shader shader{};
    DEFER(vkDestroyShaderModule(device, shader.module, nullptr));

    if (!CreateShader(shader, descriptorSetLayoutBindings, device, shaderPath))
    {
        return false;
    }

    // Remove duplicates and insert in binding order for stable indices.
    bool bindingUsed[32]{};
    std::vector<VkDescriptorSetLayoutBinding> uniqueDescriptorSetLayoutBindings(
        descriptorSetLayoutBindings.size()
    );
    size_t uniqueBindingCount = 0;
    for (const VkDescriptorSetLayoutBinding& b : descriptorSetLayoutBindings)
    {
        ASSERT(b.binding < 32);
        if (!bindingUsed[b.binding])
        {
            ASSERT(b.binding < uniqueDescriptorSetLayoutBindings.size());
            uniqueDescriptorSetLayoutBindings[b.binding] = b;
            bindingUsed[b.binding] = true;
            ++uniqueBindingCount;
        }
    }
    uniqueDescriptorSetLayoutBindings.resize(uniqueBindingCount);

    for (size_t i = 0; i < uniqueDescriptorSetLayoutBindings.size(); ++i)
    {
        ASSERT(bindingUsed[i]);
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    descriptorSetLayoutInfo.bindingCount = u32(uniqueDescriptorSetLayoutBindings.size());
    descriptorSetLayoutInfo.pBindings = uniqueDescriptorSetLayoutBindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(
        device,
        &descriptorSetLayoutInfo,
        nullptr,
        &pipeline.descriptorSetLayout
    ));

    VkPushConstantRange pushConstantRange{};
    const bool usesPushConstants = shader.pushConstantSize > 0;
    if (usesPushConstants)
    {
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.size = shader.pushConstantSize;
        pipeline.pushConstantSize = shader.pushConstantSize;
    }

    const VkDescriptorSetLayout setLayouts[2] = {
        pipeline.descriptorSetLayout,
        extraDescriptorSetLayout,
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = extraDescriptorSetLayout ? 2 : 1;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = usesPushConstants ? 1 : 0;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipeline.layout));

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = shader.stage;
    pipelineInfo.stage.module = shader.module;
    pipelineInfo.stage.pName = "Main";
    pipelineInfo.layout = pipeline.layout;

    VK_CHECK(vkCreateComputePipelines(
        device,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &pipeline.pipeline
    ));

    std::vector<VkDescriptorUpdateTemplateEntry> descriptorUpdateTemplateEntries(
        uniqueDescriptorSetLayoutBindings.size()
    );

    for (size_t i = 0; i < uniqueDescriptorSetLayoutBindings.size(); ++i)
    {
        VkDescriptorUpdateTemplateEntry entry{};
        entry.dstBinding = uniqueDescriptorSetLayoutBindings[i].binding;
        entry.descriptorCount = 1;
        entry.descriptorType = uniqueDescriptorSetLayoutBindings[i].descriptorType;
        entry.offset = sizeof(DescriptorInfo) * i;
        entry.stride = sizeof(DescriptorInfo);

        descriptorUpdateTemplateEntries[i] = entry;
    }

    VkDescriptorUpdateTemplateCreateInfo descriptorUpdateTemplateInfo{};
    descriptorUpdateTemplateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
    descriptorUpdateTemplateInfo.descriptorUpdateEntryCount
        = u32(descriptorUpdateTemplateEntries.size());
    descriptorUpdateTemplateInfo.pDescriptorUpdateEntries = descriptorUpdateTemplateEntries.data();
    descriptorUpdateTemplateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS;
    descriptorUpdateTemplateInfo.pipelineBindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
    descriptorUpdateTemplateInfo.pipelineLayout = pipeline.layout;

    if (!descriptorUpdateTemplateEntries.empty())
    {
        VK_CHECK(vkCreateDescriptorUpdateTemplate(
            device,
            &descriptorUpdateTemplateInfo,
            nullptr,
            &pipeline.descriptorUpdateTemplate
        ));
    }

    if (debugName)
    {
        if (!Vulkan::DebugNameObject(
                device,
                VK_OBJECT_TYPE_PIPELINE,
                reinterpret_cast<u64>(pipeline.pipeline),
                debugName
            ))
        {
            return false;
        }
    }

    return true;
}

bool Vulkan::CreateGraphicsPipeline(
    Vulkan::Pipeline& pipeline,
    VkDevice device,
    std::initializer_list<const char*> shaderPaths,
    VkDescriptorSetLayout extraDescriptorSetLayout,
    VkGraphicsPipelineCreateInfo& pipelineInfo,
    const char* debugName
)
{
    DEBUG_ASSERT(device);
    DEBUG_ASSERT(shaderPaths.size() > 0);

    std::vector<VkDescriptorSetLayoutBinding> descriptorSetLayoutBindings;
    descriptorSetLayoutBindings.reserve(32);
    std::vector<Shader> shaders(shaderPaths.size());

    // clang-format off
    DEFER(
        for (Shader& shader : shaders)
        {
            vkDestroyShaderModule(device, shader.module, nullptr);
        }
    );
    // clang-format on

    for (size_t i = 0; i < shaderPaths.size(); ++i)
    {
        if (!CreateShader(
                shaders[i],
                descriptorSetLayoutBindings,
                device,
                *(shaderPaths.begin() + i)
            ))
        {
            return false;
        }
    }

    u32 pushConstantSize = 0;
    for (Shader& shader : shaders)
    {
        ASSERT(
            (pushConstantSize == 0 || shader.pushConstantSize == 0)
            || (pushConstantSize == shader.pushConstantSize)
        );
        pushConstantSize = pushConstantSize == 0 ? shader.pushConstantSize : pushConstantSize;
    }

    // Remove duplicates and insert in binding order for stable indices.
    bool bindingUsed[32]{};
    std::vector<VkDescriptorSetLayoutBinding> uniqueDescriptorSetLayoutBindings(
        descriptorSetLayoutBindings.size()
    );
    size_t uniqueBindingCount = 0;
    for (const VkDescriptorSetLayoutBinding& b : descriptorSetLayoutBindings)
    {
        ASSERT(b.binding < 32);
        if (!bindingUsed[b.binding])
        {
            ASSERT(b.binding < uniqueDescriptorSetLayoutBindings.size());
            uniqueDescriptorSetLayoutBindings[b.binding] = b;
            bindingUsed[b.binding] = true;
            ++uniqueBindingCount;
        }
    }
    uniqueDescriptorSetLayoutBindings.resize(uniqueBindingCount);

    for (size_t i = 0; i < uniqueDescriptorSetLayoutBindings.size(); ++i)
    {
        ASSERT(bindingUsed[i]);
    }

    VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
    descriptorSetLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    descriptorSetLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT;
    descriptorSetLayoutInfo.bindingCount = u32(uniqueDescriptorSetLayoutBindings.size());
    descriptorSetLayoutInfo.pBindings = uniqueDescriptorSetLayoutBindings.data();

    VK_CHECK(vkCreateDescriptorSetLayout(
        device,
        &descriptorSetLayoutInfo,
        nullptr,
        &pipeline.descriptorSetLayout
    ));

    VkPushConstantRange pushConstantRange{};
    const bool usesPushConstants = pushConstantSize > 0;
    if (usesPushConstants)
    {
        pushConstantRange.stageFlags = VK_SHADER_STAGE_ALL;
        pushConstantRange.size = pushConstantSize;
        pipeline.pushConstantSize = pushConstantSize;
    }

    const VkDescriptorSetLayout setLayouts[2] = {
        pipeline.descriptorSetLayout,
        extraDescriptorSetLayout,
    };

    VkPipelineLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layoutInfo.setLayoutCount = extraDescriptorSetLayout ? 2 : 1;
    layoutInfo.pSetLayouts = setLayouts;
    layoutInfo.pushConstantRangeCount = usesPushConstants ? 1 : 0;
    layoutInfo.pPushConstantRanges = &pushConstantRange;
    VK_CHECK(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pipeline.layout));

    std::vector<VkPipelineShaderStageCreateInfo> shaderStageInfos(shaders.size());
    for (size_t i = 0; i < shaders.size(); ++i)
    {
        shaderStageInfos[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        shaderStageInfos[i].stage = shaders[i].stage;
        shaderStageInfos[i].module = shaders[i].module;
        shaderStageInfos[i].pName = "Main";
    }

    pipelineInfo.layout = pipeline.layout;
    pipelineInfo.stageCount = u32(shaderStageInfos.size());
    pipelineInfo.pStages = shaderStageInfos.data();
    pipelineInfo.layout = pipeline.layout;

    VK_CHECK(vkCreateGraphicsPipelines(
        device,
        VK_NULL_HANDLE,
        1,
        &pipelineInfo,
        nullptr,
        &pipeline.pipeline
    ));

    std::vector<VkDescriptorUpdateTemplateEntry> descriptorUpdateTemplateEntries(
        uniqueDescriptorSetLayoutBindings.size()
    );

    for (size_t i = 0; i < uniqueDescriptorSetLayoutBindings.size(); ++i)
    {
        VkDescriptorUpdateTemplateEntry entry{};
        entry.dstBinding = uniqueDescriptorSetLayoutBindings[i].binding;
        entry.descriptorCount = 1;
        entry.descriptorType = uniqueDescriptorSetLayoutBindings[i].descriptorType;
        entry.offset = sizeof(DescriptorInfo) * i;
        entry.stride = sizeof(DescriptorInfo);

        descriptorUpdateTemplateEntries[i] = entry;
    }

    VkDescriptorUpdateTemplateCreateInfo descriptorUpdateTemplateInfo{};
    descriptorUpdateTemplateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
    descriptorUpdateTemplateInfo.descriptorUpdateEntryCount
        = u32(descriptorUpdateTemplateEntries.size());
    descriptorUpdateTemplateInfo.pDescriptorUpdateEntries = descriptorUpdateTemplateEntries.data();
    descriptorUpdateTemplateInfo.templateType = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS;
    descriptorUpdateTemplateInfo.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    descriptorUpdateTemplateInfo.pipelineLayout = pipeline.layout;

    if (!descriptorUpdateTemplateEntries.empty())
    {
        VK_CHECK(vkCreateDescriptorUpdateTemplate(
            device,
            &descriptorUpdateTemplateInfo,
            nullptr,
            &pipeline.descriptorUpdateTemplate
        ));
    }

    if (debugName)
    {
        if (!Vulkan::DebugNameObject(
                device,
                VK_OBJECT_TYPE_PIPELINE,
                reinterpret_cast<u64>(pipeline.pipeline),
                debugName
            ))
        {
            return false;
        }
    }

    return true;
}

void Vulkan::DestroyPipeline(Vulkan::Pipeline& pipeline, VkDevice device)
{
    DEBUG_ASSERT(device);

    vkDestroyPipelineLayout(device, pipeline.layout, nullptr);
    pipeline.layout = VK_NULL_HANDLE;
    vkDestroyDescriptorSetLayout(device, pipeline.descriptorSetLayout, nullptr);
    pipeline.descriptorSetLayout = VK_NULL_HANDLE;
    vkDestroyPipeline(device, pipeline.pipeline, nullptr);
    pipeline.pipeline = VK_NULL_HANDLE;
    vkDestroyDescriptorUpdateTemplate(device, pipeline.descriptorUpdateTemplate, nullptr);
    pipeline.descriptorUpdateTemplate = VK_NULL_HANDLE;
}

void Vulkan::CmdPushDescriptors(
    VkCommandBuffer cmd,
    const Vulkan::Pipeline& pipeline,
    std::initializer_list<Vulkan::DescriptorInfo> descriptorInfos
)
{
    DEBUG_ASSERT(cmd);
    DEBUG_ASSERT(pipeline.pipeline);
    DEBUG_ASSERT(pipeline.descriptorSetLayout);
    DEBUG_ASSERT(pipeline.descriptorUpdateTemplate);
    DEBUG_ASSERT(pipeline.layout);
    DEBUG_ASSERT(descriptorInfos.size() > 0);

    vkCmdPushDescriptorSetWithTemplate(
        cmd,
        pipeline.descriptorUpdateTemplate,
        pipeline.layout,
        0,
        descriptorInfos.begin()
    );
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
