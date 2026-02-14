#include "SharedConfig.slang"
#include "SharedDef.slang"

struct VkDrawIndirectCommand {
    uint32_t    vertexCount;
    uint32_t    instanceCount;
    uint32_t    firstVertex;
    uint32_t    firstInstance;
};

#define PARTICLE_SYSTEM_DEAD_COUNT_OFFSET 0
#define PARTICLE_SYSTEM_ALIVE_1_COUNT_OFFSET 4
#define PARTICLE_SYSTEM_ALIVE_2_COUNT_OFFSET 8

#define INDIRECT_DATA_DISPATCH_COUNT_OFFSET 0
#define INDIRECT_DATA_DRAW_CMD_OFFSET 12

[[vk::binding(UNIFORM_BUFFER_BINDING)]]
ConstantBuffer<UniformData> uniformBuffer;

[[vk::binding(PARTICLE_COUNT_BUFFER_BINDING)]]
RWByteAddressBuffer particleCountBuffer;

[[vk::binding(PARTICLE_COUNT_UNIFORM_BUFFER_BINDING)]]
ConstantBuffer<ParticleCountData> particleCountUniformBuffer;

[[vk::binding(INDIRECT_BUFFER_BINDING)]]
RWByteAddressBuffer indirectBuffer;

// NOTE: on my GPU (Nvidia Blackwell) SoAoS is around the same speed as AoS
// if indices in alive buffers are somewhat sequential (for example, when
// many emitters have the same particle life time), but if indices are mostly
// random, SoAoS is 2x faster. Haven't tested full SoA approach.
[[vk::binding(VELOCITY_MASS_INV_BUFFER_BINDING)]]
RWStructuredBuffer<float4> velocityMassInvBuffer;

[[vk::binding(POSITION_DIST_TO_FORCE_FIELD_INV_BUFFER_BINDING)]]
RWStructuredBuffer<float4> positionDistToForceFieldInvBuffer;

[[vk::binding(RADIUS_BUFFER_BINDING)]]
RWStructuredBuffer<float> radiusBuffer;

[[vk::binding(TIME_LEFT_BUFFER_BINDING)]]
RWStructuredBuffer<float> timeLeftBuffer;

[[vk::binding(DEAD_BUFFER_BINDING)]]
RWStructuredBuffer<uint> deadBuffer;

[[vk::binding(ALIVE_BUFFER_1_BINDING)]]
RWStructuredBuffer<uint> aliveBuffer1;

[[vk::binding(ALIVE_BUFFER_2_BINDING)]]
RWStructuredBuffer<uint> aliveBuffer2;

[[vk::binding(EMITTER_IDX_BUFFER_BINDING)]]
RWStructuredBuffer<uint8_t> emitterIdxBuffer;

[[vk::binding(EMITTER_BUFFER_BINDING)]]
StructuredBuffer<Emitter> emitterBuffer;

[[vk::binding(RENDER_IMAGE_SAMPLER_BINDING)]]
Sampler2D renderImageSampler;
