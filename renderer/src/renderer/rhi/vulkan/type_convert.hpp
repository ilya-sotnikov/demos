#pragma once

#include "common.hpp"

#include "renderer/rhi/rhi.hpp"

VkMemoryPropertyFlags MemoryTypeToVk(rhi::MemoryType type);
VkImageType TextureTypeToVk(rhi::TextureType type);
VkImageViewType TextureTypeToViewVk(rhi::TextureType type);
VkImageUsageFlags TextureUsageToVk(rhi::TextureUsageFlags usage);
VkFormat FormatToVk(rhi::Format format);
VkPipelineStageFlags2 StageToVk(rhi::StageFlags stage);
VkAccessFlags2 AccessToVk(rhi::AccessFlags access);
VkImageLayout TextureLayoutToVk(rhi::TextureLayout layout);
VkPrimitiveTopology PrimitiveTopologyToVk(rhi::Topology topology);
VkCullModeFlags CullToVk(rhi::Cull cull);
VkColorComponentFlags ColorComponentToVk(rhi::ColorComponentFlags flags);
VkFilter FilterToVk(rhi::Filter filter);
VkSamplerMipmapMode SamplerMipmapModeToVk(rhi::SamplerMipmapMode mode);
VkSamplerAddressMode SamplerAddressModeToVk(rhi::SamplerAddressMode mode);
VkCompareOp CompareOpToVk(rhi::CompareOp op);
VkAttachmentLoadOp AttachmentLoadOpToVk(rhi::AttachmentLoadOp op);
VkAttachmentStoreOp AttachmentStoreOpToVk(rhi::AttachmentStoreOp op);
VkIndexType IndexTypeToVk(rhi::IndexType type);
VkBlendFactor BlendFactorToVk(rhi::BlendFactor factor);
VkBlendOp BlendOpToVk(rhi::BlendOp op);

// TODO: kinda messy to declare it here, since it's used in the renderer.
rhi::Format FormatToRHI(VkFormat format);
