#pragma once

#include "common.hpp"

#include "renderer/rhi/rhi.hpp"

VkMemoryPropertyFlags memory_type_to_vk(rhi::MemoryType type);
VkImageType texture_type_to_vk(rhi::TextureType type);
VkImageViewType texture_type_to_view_vk(rhi::TextureType type);
VkImageUsageFlags texture_usage_to_vk(rhi::TextureUsageFlags usage);
VkFormat format_to_vk(rhi::Format format);
VkPipelineStageFlags2 stage_to_vk(rhi::StageFlags stage);
VkAccessFlags2 access_to_vk(rhi::AccessFlags access);
VkImageLayout texture_layout_to_vk(rhi::TextureLayout layout);
VkPrimitiveTopology primitive_topology_to_vk(rhi::Topology topology);
VkCullModeFlags cull_to_vk(rhi::Cull cull);
VkColorComponentFlags color_component_to_vk(rhi::ColorComponentFlags flags);
VkFilter filter_to_vk(rhi::Filter filter);
VkSamplerMipmapMode sampler_mipmap_mode_to_vk(rhi::SamplerMipmapMode mode);
VkSamplerAddressMode sampler_address_mode_to_vk(rhi::SamplerAddressMode mode);
VkCompareOp compare_op_to_vk(rhi::CompareOp op);
VkAttachmentLoadOp attachment_load_op_to_vk(rhi::AttachmentLoadOp op);
VkAttachmentStoreOp attachment_store_op_to_vk(rhi::AttachmentStoreOp op);
VkIndexType index_type_to_vk(rhi::IndexType type);
VkBlendFactor blend_factor_to_vk(rhi::BlendFactor factor);
VkBlendOp blend_op_to_vk(rhi::BlendOp op);
VkQueueFlagBits queue_type_to_vk(rhi::QueueType type);

// TODO: kinda messy to declare it here, since it's used in the renderer.
rhi::Format format_to_rhi(VkFormat format);
