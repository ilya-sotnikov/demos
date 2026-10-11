#include "type_convert.hpp"

#include "utils.hpp"

VkMemoryPropertyFlags MemoryTypeToVk(rhi::MemoryType type)
{
    switch (type)
    {
    case rhi::MEMORY_TYPE_DEFAULT:
    case rhi::MEMORY_TYPE_DEFAULT_UNIFORM:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case rhi::MEMORY_TYPE_DEVICE:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    UNREACHABLE();
}

VkImageType TextureTypeToVk(rhi::TextureType type)
{
    switch (type)
    {
    case rhi::TEXTURE_TYPE_1D:
        return VK_IMAGE_TYPE_1D;
    case rhi::TEXTURE_TYPE_2D:
    case rhi::TEXTURE_TYPE_2D_ARRAY:
        return VK_IMAGE_TYPE_2D;
    case rhi::TEXTURE_TYPE_3D:
        return VK_IMAGE_TYPE_3D;
    }

    UNREACHABLE();
}

VkImageViewType TextureTypeToViewVk(rhi::TextureType type)
{
    switch (type)
    {
    case rhi::TEXTURE_TYPE_1D:
        return VK_IMAGE_VIEW_TYPE_1D;
    case rhi::TEXTURE_TYPE_2D:
        return VK_IMAGE_VIEW_TYPE_2D;
    case rhi::TEXTURE_TYPE_2D_ARRAY:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case rhi::TEXTURE_TYPE_3D:
        return VK_IMAGE_VIEW_TYPE_3D;
    }

    UNREACHABLE();
}

VkImageUsageFlags TextureUsageToVk(rhi::TextureUsageFlags usage)
{
    VkImageUsageFlags vkUsage{};

    if (usage & rhi::TEXTURE_USAGE_TRANSFER_SRC_BIT)
        vkUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usage & rhi::TEXTURE_USAGE_TRANSFER_DST_BIT)
        vkUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (usage & rhi::TEXTURE_USAGE_SAMPLED_BIT)
        vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage & rhi::TEXTURE_USAGE_STORAGE_BIT)
        vkUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (usage & rhi::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)
        vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usage & rhi::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    return vkUsage;
}

VkFormat FormatToVk(rhi::Format format)
{
    static const VkFormat sTable[] =
    {
#define RHI_XFMT(rhiFormat, vulkanFormat) vulkanFormat,
#include "../format_table.hpp"
    };

    DEBUG_ASSERT(u32(format) < ARRAY_SIZE(sTable));

    return sTable[u32(format)];
}

VkPipelineStageFlags2 StageToVk(rhi::StageFlags stage)
{
    VkPipelineStageFlags2 result{};

#define RHI_XSTAGE(rhi, bit, vulkan) \
    if (stage & bit) \
    { \
        result |= vulkan; \
    }
#include "../stage_table.hpp"

    return result;
}

VkAccessFlags2 AccessToVk(rhi::AccessFlags access)
{
    VkAccessFlags2 result{};

#define RHI_XACCESS(rhi, bit, vulkan) \
    if (access & bit) \
    { \
        result |= vulkan; \
    }
#include "../access_table.hpp"

    return result;
}

VkImageLayout TextureLayoutToVk(rhi::TextureLayout layout)
{
    switch (layout)
    {
    case rhi::TEXTURE_LAYOUT_UNDEFINED:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case rhi::TEXTURE_LAYOUT_GENERAL:
        return VK_IMAGE_LAYOUT_GENERAL;
    case rhi::TEXTURE_LAYOUT_PRESENT_SRC:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    UNREACHABLE();
}

VkPrimitiveTopology PrimitiveTopologyToVk(rhi::Topology topology)
{
    switch (topology)
    {
    case rhi::TOPOLOGY_TRIANGLE_LIST:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case rhi::TOPOLOGY_TRIANGLE_FAN:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case rhi::TOPOLOGY_TRIANGLE_STRIP:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }

    UNREACHABLE();
}

VkCullModeFlags CullToVk(rhi::Cull cull)
{
    switch (cull)
    {
    case rhi::CULL_CCW:
        return VK_CULL_MODE_FRONT_BIT;
    case rhi::CULL_CW:
        return VK_CULL_MODE_BACK_BIT;
    case rhi::CULL_NONE:
        return VK_CULL_MODE_NONE;
    case rhi::CULL_ALL:
        return VK_CULL_MODE_FRONT_AND_BACK;
    }

    UNREACHABLE();
}

VkColorComponentFlags ColorComponentToVk(rhi::ColorComponentFlags flags)
{
    VkColorComponentFlags result{};

    if (flags & rhi::COLOR_COMPONENT_R_BIT)
        result |= VK_COLOR_COMPONENT_R_BIT;
    if (flags & rhi::COLOR_COMPONENT_G_BIT)
        result |= VK_COLOR_COMPONENT_G_BIT;
    if (flags & rhi::COLOR_COMPONENT_B_BIT)
        result |= VK_COLOR_COMPONENT_B_BIT;
    if (flags & rhi::COLOR_COMPONENT_A_BIT)
        result |= VK_COLOR_COMPONENT_A_BIT;

    return result;
}

VkFilter FilterToVk(rhi::Filter filter)
{
    switch (filter)
    {
    case rhi::FILTER_NEAREST:
        return VK_FILTER_NEAREST;
    case rhi::FILTER_LINEAR:
        return VK_FILTER_LINEAR;
    }

    UNREACHABLE();
}

VkSamplerMipmapMode SamplerMipmapModeToVk(rhi::SamplerMipmapMode mode)
{
    switch (mode)
    {
    case rhi::SAMPLER_MIPMAP_MODE_NEAREST:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case rhi::SAMPLER_MIPMAP_MODE_LINEAR:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    UNREACHABLE();
}

VkSamplerAddressMode SamplerAddressModeToVk(rhi::SamplerAddressMode mode)
{
    switch (mode)
    {
    case rhi::SAMPLER_ADDRESS_MODE_REPEAT:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case rhi::SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case rhi::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case rhi::SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case rhi::SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    }

    UNREACHABLE();
}

VkCompareOp CompareOpToVk(rhi::CompareOp op)
{
    switch (op)
    {
    case rhi::COMPARE_OP_NEVER:
        return VK_COMPARE_OP_NEVER;
    case rhi::COMPARE_OP_LESS:
        return VK_COMPARE_OP_LESS;
    case rhi::COMPARE_OP_EQUAL:
        return VK_COMPARE_OP_EQUAL;
    case rhi::COMPARE_OP_LESS_OR_EQUAL:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case rhi::COMPARE_OP_GREATER:
        return VK_COMPARE_OP_GREATER;
    case rhi::COMPARE_OP_NOT_EQUAL:
        return VK_COMPARE_OP_NOT_EQUAL;
    case rhi::COMPARE_OP_GREATER_OR_EQUAL:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case rhi::COMPARE_OP_ALWAYS:
        return VK_COMPARE_OP_ALWAYS;
    }

    UNREACHABLE();
}

VkAttachmentLoadOp AttachmentLoadOpToVk(rhi::AttachmentLoadOp op)
{
    switch (op)
    {
    case rhi::ATTACHMENT_LOAD_OP_LOAD:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case rhi::ATTACHMENT_LOAD_OP_CLEAR:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case rhi::ATTACHMENT_LOAD_OP_DONT_CARE:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    case rhi::ATTACHMENT_LOAD_OP_NONE:
        return VK_ATTACHMENT_LOAD_OP_NONE;
    }

    UNREACHABLE();
}

VkAttachmentStoreOp AttachmentStoreOpToVk(rhi::AttachmentStoreOp op)
{
    switch (op)
    {
    case rhi::ATTACHMENT_STORE_OP_STORE:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case rhi::ATTACHMENT_STORE_OP_DONT_CARE:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    case rhi::ATTACHMENT_STORE_OP_NONE:
        return VK_ATTACHMENT_STORE_OP_NONE;
    }

    UNREACHABLE();
}

VkIndexType IndexTypeToVk(rhi::IndexType type)
{
    switch (type)
    {
    case rhi::INDEX_TYPE_U8:
        return VK_INDEX_TYPE_UINT8;
    case rhi::INDEX_TYPE_U16:
        return VK_INDEX_TYPE_UINT16;
    case rhi::INDEX_TYPE_U32:
        return VK_INDEX_TYPE_UINT32;
    }

    UNREACHABLE();
}

VkBlendFactor BlendFactorToVk(rhi::BlendFactor factor)
{
    switch (factor)
    {
    case rhi::BLEND_FACTOR_ZERO:
        return VK_BLEND_FACTOR_ZERO;
    case rhi::BLEND_FACTOR_ONE:
        return VK_BLEND_FACTOR_ONE;
    case rhi::BLEND_FACTOR_SRC_COLOR:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case rhi::BLEND_FACTOR_DST_COLOR:
        return VK_BLEND_FACTOR_DST_COLOR;
    case rhi::BLEND_FACTOR_SRC_ALPHA:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case rhi::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }

    UNREACHABLE();
}

VkBlendOp BlendOpToVk(rhi::BlendOp op)
{
    switch (op)
    {
    case rhi::BLEND_OP_ADD:
        return VK_BLEND_OP_ADD;
    case rhi::BLEND_OP_SUBTRACT:
        return VK_BLEND_OP_SUBTRACT;
    case rhi::BLEND_OP_REVERSE_SUBTRACT:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case rhi::BLEND_OP_MIN:
        return VK_BLEND_OP_MIN;
    case rhi::BLEND_OP_MAX:
        return VK_BLEND_OP_MAX;
    }

    UNREACHABLE();
}

rhi::Format FormatToRHI(VkFormat format)
{
    switch (format)
    {
#define RHI_XFMT(rhiFormat, vulkanFormat) \
    case vulkanFormat: \
        return rhi::rhiFormat;
#include "../format_table.hpp"

    default:
        fprintf(stderr, "unhandled format %d\n", format);
        UNREACHABLE();
    }
}
