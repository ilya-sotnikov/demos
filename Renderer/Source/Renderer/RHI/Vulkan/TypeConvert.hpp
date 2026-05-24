#pragma once

#include "Common.hpp"

#include "../RHI.hpp"

VkMemoryPropertyFlags MemoryTypeToVk(RHI::MemoryType type);
VkImageType TextureTypeToVk(RHI::TextureType type);
VkImageViewType TextureTypeToViewVk(RHI::TextureType type);
VkImageUsageFlags TextureUsageToVk(RHI::TextureUsageFlags usage);
VkFormat FormatToVk(RHI::Format format);
VkPipelineStageFlags2 StageToVk(RHI::StageFlags stage);
VkAccessFlags2 AccessToVk(RHI::AccessFlags access);
VkImageLayout TextureLayoutToVk(RHI::TextureLayout layout);
VkPrimitiveTopology PrimitiveTopologyToVk(RHI::Topology topology);
VkCullModeFlags CullToVk(RHI::Cull cull);
VkColorComponentFlags ColorComponentToVk(RHI::ColorComponentFlags flags);
VkFilter FilterToVk(RHI::Filter filter);
VkSamplerMipmapMode SamplerMipmapModeToVk(RHI::SamplerMipmapMode mode);
VkSamplerAddressMode SamplerAddressModeToVk(RHI::SamplerAddressMode mode);
VkCompareOp CompareOpToVk(RHI::CompareOp op);
VkAttachmentLoadOp AttachmentLoadOpToVk(RHI::AttachmentLoadOp op);
VkAttachmentStoreOp AttachmentStoreOpToVk(RHI::AttachmentStoreOp op);
VkIndexType IndexTypeToVk(RHI::IndexType type);
VkBlendFactor BlendFactorToVk(RHI::BlendFactor factor);
VkBlendOp BlendOpToVk(RHI::BlendOp op);

// TODO: kinda messy to declare it here, since it's used in the renderer.
RHI::Format FormatToRHI(VkFormat format);
