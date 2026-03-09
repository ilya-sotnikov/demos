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

[[vk::binding(COMBINED_IMAGE_SAMPLER_BINDING)]]
Sampler2D combinedImageSamplers[];

[[vk::binding(STORAGE_IMAGE_BINDING)]]
Texture2D storageImages[];

[[vk::binding(STORAGE_IMAGE_RW_BINDING)]]
RWTexture2D storageImagesRW[];

[[vk::binding(SAMPLER_BINDING)]]
SamplerState samplers[];

[[vk::binding(SAMPLED_IMAGE_BINDING)]]
Texture2D textures[];
