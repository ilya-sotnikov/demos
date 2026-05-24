#include "TypeConvert.hpp"

#include "../../../Utils.hpp"

VkMemoryPropertyFlags MemoryTypeToVk(RHI::MemoryType type)
{
    switch (type)
    {
    case RHI::MEMORY_TYPE_DEFAULT:
    case RHI::MEMORY_TYPE_DEFAULT_UNIFORM:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT
            | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    case RHI::MEMORY_TYPE_DEVICE:
        return VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    }

    UNREACHABLE();
}

VkImageType TextureTypeToVk(RHI::TextureType type)
{
    switch (type)
    {
    case RHI::TEXTURE_TYPE_1D:
        return VK_IMAGE_TYPE_1D;
    case RHI::TEXTURE_TYPE_2D:
    case RHI::TEXTURE_TYPE_2D_ARRAY:
        return VK_IMAGE_TYPE_2D;
    case RHI::TEXTURE_TYPE_3D:
        return VK_IMAGE_TYPE_3D;
    }

    UNREACHABLE();
}

VkImageViewType TextureTypeToViewVk(RHI::TextureType type)
{
    switch (type)
    {
    case RHI::TEXTURE_TYPE_1D:
        return VK_IMAGE_VIEW_TYPE_1D;
    case RHI::TEXTURE_TYPE_2D:
        return VK_IMAGE_VIEW_TYPE_2D;
    case RHI::TEXTURE_TYPE_2D_ARRAY:
        return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case RHI::TEXTURE_TYPE_3D:
        return VK_IMAGE_VIEW_TYPE_3D;
    }

    UNREACHABLE();
}

VkImageUsageFlags TextureUsageToVk(RHI::TextureUsageFlags usage)
{
    VkImageUsageFlags vkUsage{};

    if (usage & RHI::TEXTURE_USAGE_TRANSFER_SRC_BIT)
        vkUsage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (usage & RHI::TEXTURE_USAGE_TRANSFER_DST_BIT)
        vkUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (usage & RHI::TEXTURE_USAGE_SAMPLED_BIT)
        vkUsage |= VK_IMAGE_USAGE_SAMPLED_BIT;
    if (usage & RHI::TEXTURE_USAGE_STORAGE_BIT)
        vkUsage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (usage & RHI::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)
        vkUsage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (usage & RHI::TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
        vkUsage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    return vkUsage;
}

VkFormat FormatToVk(RHI::Format format)
{
    static const VkFormat sTable[] =
    {
#define RHI_XFMT(rhiFormat, vulkanFormat) vulkanFormat,
#include "../FormatTable.hpp"
    };

    DEBUG_ASSERT(u32(format) < ARRAY_SIZE(sTable));

    return sTable[u32(format)];
}

VkPipelineStageFlags2 StageToVk(RHI::StageFlags stage)
{
    VkPipelineStageFlags2 result{};

#define RHI_XSTAGE(rhi, bit, vulkan) \
    if (stage & bit) \
    { \
        result |= vulkan; \
    }
#include "../StageTable.hpp"

    return result;
}

VkAccessFlags2 AccessToVk(RHI::AccessFlags access)
{
    VkAccessFlags2 result{};

#define RHI_XACCESS(rhi, bit, vulkan) \
    if (access & bit) \
    { \
        result |= vulkan; \
    }
#include "../AccessTable.hpp"

    return result;
}

VkImageLayout TextureLayoutToVk(RHI::TextureLayout layout)
{
    switch (layout)
    {
    case RHI::TEXTURE_LAYOUT_UNDEFINED:
        return VK_IMAGE_LAYOUT_UNDEFINED;
    case RHI::TEXTURE_LAYOUT_GENERAL:
        return VK_IMAGE_LAYOUT_GENERAL;
    case RHI::TEXTURE_LAYOUT_PRESENT_SRC:
        return VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    UNREACHABLE();
}

VkPrimitiveTopology PrimitiveTopologyToVk(RHI::Topology topology)
{
    switch (topology)
    {
    case RHI::TOPOLOGY_TRIANGLE_LIST:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case RHI::TOPOLOGY_TRIANGLE_FAN:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    case RHI::TOPOLOGY_TRIANGLE_STRIP:
        return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    }

    UNREACHABLE();
}

VkCullModeFlags CullToVk(RHI::Cull cull)
{
    switch (cull)
    {
    case RHI::CULL_CCW:
        return VK_CULL_MODE_FRONT_BIT;
    case RHI::CULL_CW:
        return VK_CULL_MODE_BACK_BIT;
    case RHI::CULL_NONE:
        return VK_CULL_MODE_NONE;
    case RHI::CULL_ALL:
        return VK_CULL_MODE_FRONT_AND_BACK;
    }

    UNREACHABLE();
}

VkColorComponentFlags ColorComponentToVk(RHI::ColorComponentFlags flags)
{
    VkColorComponentFlags result{};

    if (flags & RHI::COLOR_COMPONENT_R_BIT)
        result |= VK_COLOR_COMPONENT_R_BIT;
    if (flags & RHI::COLOR_COMPONENT_G_BIT)
        result |= VK_COLOR_COMPONENT_G_BIT;
    if (flags & RHI::COLOR_COMPONENT_B_BIT)
        result |= VK_COLOR_COMPONENT_B_BIT;
    if (flags & RHI::COLOR_COMPONENT_A_BIT)
        result |= VK_COLOR_COMPONENT_A_BIT;

    return result;
}

VkFilter FilterToVk(RHI::Filter filter)
{
    switch (filter)
    {
    case RHI::FILTER_NEAREST:
        return VK_FILTER_NEAREST;
    case RHI::FILTER_LINEAR:
        return VK_FILTER_LINEAR;
    }

    UNREACHABLE();
}

VkSamplerMipmapMode SamplerMipmapModeToVk(RHI::SamplerMipmapMode mode)
{
    switch (mode)
    {
    case RHI::SAMPLER_MIPMAP_MODE_NEAREST:
        return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    case RHI::SAMPLER_MIPMAP_MODE_LINEAR:
        return VK_SAMPLER_MIPMAP_MODE_LINEAR;
    }

    UNREACHABLE();
}

VkSamplerAddressMode SamplerAddressModeToVk(RHI::SamplerAddressMode mode)
{
    switch (mode)
    {
    case RHI::SAMPLER_ADDRESS_MODE_REPEAT:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case RHI::SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
        return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case RHI::SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case RHI::SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case RHI::SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
        return VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE;
    }

    UNREACHABLE();
}

VkCompareOp CompareOpToVk(RHI::CompareOp op)
{
    switch (op)
    {
    case RHI::COMPARE_OP_NEVER:
        return VK_COMPARE_OP_NEVER;
    case RHI::COMPARE_OP_LESS:
        return VK_COMPARE_OP_LESS;
    case RHI::COMPARE_OP_EQUAL:
        return VK_COMPARE_OP_EQUAL;
    case RHI::COMPARE_OP_LESS_OR_EQUAL:
        return VK_COMPARE_OP_LESS_OR_EQUAL;
    case RHI::COMPARE_OP_GREATER:
        return VK_COMPARE_OP_GREATER;
    case RHI::COMPARE_OP_NOT_EQUAL:
        return VK_COMPARE_OP_NOT_EQUAL;
    case RHI::COMPARE_OP_GREATER_OR_EQUAL:
        return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case RHI::COMPARE_OP_ALWAYS:
        return VK_COMPARE_OP_ALWAYS;
    }

    UNREACHABLE();
}

VkAttachmentLoadOp AttachmentLoadOpToVk(RHI::AttachmentLoadOp op)
{
    switch (op)
    {
    case RHI::ATTACHMENT_LOAD_OP_LOAD:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case RHI::ATTACHMENT_LOAD_OP_CLEAR:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case RHI::ATTACHMENT_LOAD_OP_DONT_CARE:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    case RHI::ATTACHMENT_LOAD_OP_NONE:
        return VK_ATTACHMENT_LOAD_OP_NONE;
    }

    UNREACHABLE();
}

VkAttachmentStoreOp AttachmentStoreOpToVk(RHI::AttachmentStoreOp op)
{
    switch (op)
    {
    case RHI::ATTACHMENT_STORE_OP_STORE:
        return VK_ATTACHMENT_STORE_OP_STORE;
    case RHI::ATTACHMENT_STORE_OP_DONT_CARE:
        return VK_ATTACHMENT_STORE_OP_DONT_CARE;
    case RHI::ATTACHMENT_STORE_OP_NONE:
        return VK_ATTACHMENT_STORE_OP_NONE;
    }

    UNREACHABLE();
}

VkIndexType IndexTypeToVk(RHI::IndexType type)
{
    switch (type)
    {
    case RHI::INDEX_TYPE_U8:
        return VK_INDEX_TYPE_UINT8;
    case RHI::INDEX_TYPE_U16:
        return VK_INDEX_TYPE_UINT16;
    case RHI::INDEX_TYPE_U32:
        return VK_INDEX_TYPE_UINT32;
    }

    UNREACHABLE();
}

VkBlendFactor BlendFactorToVk(RHI::BlendFactor factor)
{
    switch (factor)
    {
    case RHI::BLEND_FACTOR_ZERO:
        return VK_BLEND_FACTOR_ZERO;
    case RHI::BLEND_FACTOR_ONE:
        return VK_BLEND_FACTOR_ONE;
    case RHI::BLEND_FACTOR_SRC_COLOR:
        return VK_BLEND_FACTOR_SRC_COLOR;
    case RHI::BLEND_FACTOR_DST_COLOR:
        return VK_BLEND_FACTOR_DST_COLOR;
    case RHI::BLEND_FACTOR_SRC_ALPHA:
        return VK_BLEND_FACTOR_SRC_ALPHA;
    case RHI::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
        return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    }

    UNREACHABLE();
}

VkBlendOp BlendOpToVk(RHI::BlendOp op)
{
    switch (op)
    {
    case RHI::BLEND_OP_ADD:
        return VK_BLEND_OP_ADD;
    case RHI::BLEND_OP_SUBTRACT:
        return VK_BLEND_OP_SUBTRACT;
    case RHI::BLEND_OP_REVERSE_SUBTRACT:
        return VK_BLEND_OP_REVERSE_SUBTRACT;
    case RHI::BLEND_OP_MIN:
        return VK_BLEND_OP_MIN;
    case RHI::BLEND_OP_MAX:
        return VK_BLEND_OP_MAX;
    }

    UNREACHABLE();
}

RHI::Format FormatToRHI(VkFormat format)
{
    switch (format)
    {

#define RHI_XFMT(rhiFormat, vulkanFormat) \
    case vulkanFormat: \
        return RHI::rhiFormat;
#include "../FormatTable.hpp"

    default:
        fprintf(stderr, "unhandled format %d\n", format);
        UNREACHABLE();
    }
}
