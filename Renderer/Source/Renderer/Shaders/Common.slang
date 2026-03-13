#include "SharedConfig.slang"
#include "SharedDef.slang"

struct VkDrawIndexedIndirectCommand
{
    uint32_t    indexCount;
    uint32_t    instanceCount;
    uint32_t    firstIndex;
    int32_t     vertexOffset;
    uint32_t    firstInstance;
};

// NOTE: full bindless is not practical yet since it messes up synchronization validation.
// For now it's only for read-only resources (textures).

[[vk::binding(SAMPLER_TEXTURE_BINDING)]]
SamplerState textureSampler;

[[vk::binding(SAMPLER_LINEAR_BINDING)]]
SamplerState linearSampler;

[[vk::binding(SAMPLED_IMAGE_RENDER_BINDING)]]
Texture2D renderSampledImage;

[[vk::binding(SAMPLED_IMAGE_RESOLVED_RENDER_BINDING)]]
Texture2D resolvedRenderSampledImage;

[[vk::binding(SAMPLED_IMAGE_PREV_RESOLVED_RENDER_BINDING)]]
Texture2D prevResolvedRenderSampledImage;

[[vk::binding(SAMPLED_IMAGE_ALBEDO_BINDING)]]
Texture2D albedoSampledImage;

[[vk::binding(SAMPLED_IMAGE_SHADOW_BINDING)]]
Texture2D shadowSampledImage;

[[vk::binding(SAMPLED_IMAGE_DEPTH_BINDING)]]
Texture2D depthSampledImage;

[[vk::binding(SAMPLED_IMAGE_VELOCITY_BINDING)]]
Texture2D velocitySampledImage;

[[vk::binding(STORAGE_IMAGE_RENDER_BINDING)]]
RWTexture2D renderStorageImage;

[[vk::binding(STORAGE_IMAGE_RESOLVED_RENDER_BINDING)]]
RWTexture2D resolvedRenderStorageImage;

[[vk::binding(STORAGE_IMAGE_SHADOW_BINDING)]]
RWTexture2D shadowStorageImage;

[[vk::binding(0, 1)]]
Texture2D textures[];
