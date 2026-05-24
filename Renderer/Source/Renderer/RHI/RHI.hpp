#pragma once

#include "../../Common.hpp"
#include "../../SliceArg.hpp"

// Based on Sebastian Aaltonen's approach with designated initializers:
// https://youtu.be/m3bW8d4Brec?si=7V8xsxykqCHbskvu&t=2233
// NOTE: resource descriptions are passed as rvalue references because they may
// contain std::initializer_list, that has very short lifetime, rvalue refs force
// temporaries and that prevents creating a local resource description variable
// and passing it as an argument with a dangling pointer.
// NOTE: uninitialized members will be zeroed out because of designated initializers.

// NOTE: VK_KHR_unified_image_layouts and VK_SHARING_MODE_CONCURRENT
// don't cause perf loss on Nvidia, this RHI assumes that. All future GPUs
// will probably converge to compute-first devices, I recommend reading the post below.

// Also took some inspiration from Sebastian Aaltonen's No Graphics API post:
// https://www.sebastianaaltonen.com/blog/no-graphics-api
// Didn't go fully bindless/BDA, since it messes up sync validation (for now?).

// TODO: 16 bit should be enough.
#define RHI_HANDLE(type) \
    struct type \
    { \
        u32 idx = RHI::INVALID_HANDLE; \
        u32 generation; \
        explicit operator bool() const \
        { \
            return idx != RHI::INVALID_HANDLE; \
        } \
        static type Invalid() \
        { \
            return {.idx = RHI::INVALID_HANDLE}; \
        } \
    }

struct SDL_Window;

// #define RHI_ENABLE_DEBUG_UTILS

namespace RHI
{

using Flags = u32;
using Flags64 = u64;

inline constexpr u32 INVALID_HANDLE = UINT32_MAX;
inline constexpr int FRAMES_IN_FLIGHT = 2;
inline constexpr u32 MAX_BINDLESS_DESCRIPTOR_COUNT = 16384;
inline constexpr int PUSH_CONSTANTS_MAX_SIZE_BYTES = 128;

RHI_HANDLE(PipelineHandle);
RHI_HANDLE(BufferHandle);
RHI_HANDLE(TextureHandle);
RHI_HANDLE(TextureDescriptorHandle);
RHI_HANDLE(SamplerHandle);
RHI_HANDLE(SemaphoreHandle);
RHI_HANDLE(CommandBufferHandle);

enum MemoryType
{
    MEMORY_TYPE_DEFAULT, // Device-local, host-visible, host-coherent.
    MEMORY_TYPE_DEFAULT_UNIFORM, // Device-local, host-visible, host-coherent, uniform.
    MEMORY_TYPE_DEVICE, // Device-local.
};

enum Cull
{
    CULL_CCW,
    CULL_CW,
    CULL_ALL,
    CULL_NONE,
};

// clang-format off
enum DepthFlagBits : u32
{
    DEPTH_READ_BIT  = (1U << 0),
    DEPTH_WRITE_BIT = (1U << 1),
};
using DepthFlags = Flags;
// clang-format on

enum Op
{
    OP_NEVER,
    OP_LESS,
    OP_EQUAL,
    OP_LESS_EQUAl,
    OP_GREATER,
    OP_NOT_EQUAL,
    OP_GREATER_EQUAL,
    OP_ALWAYS,
};

enum BlendOp
{
    BLEND_OP_ADD,
    BLEND_OP_SUBTRACT,
    BLEND_OP_REVERSE_SUBTRACT,
    BLEND_OP_MIN,
    BLEND_OP_MAX,
};

enum BlendFactor
{
    BLEND_FACTOR_ZERO,
    BLEND_FACTOR_ONE,
    BLEND_FACTOR_SRC_COLOR,
    BLEND_FACTOR_DST_COLOR,
    BLEND_FACTOR_SRC_ALPHA,
    BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
};

enum ColorComponentFlagBits : u32
{
    COLOR_COMPONENT_R_BIT = (1U << 0),
    COLOR_COMPONENT_G_BIT = (1U << 1),
    COLOR_COMPONENT_B_BIT = (1U << 2),
    COLOR_COMPONENT_A_BIT = (1U << 3),
};
using ColorComponentFlags = Flags;

inline constexpr ColorComponentFlags COLOR_COMPONENT_ALL_BITS
    = COLOR_COMPONENT_R_BIT | COLOR_COMPONENT_G_BIT | COLOR_COMPONENT_B_BIT | COLOR_COMPONENT_A_BIT;

enum Topology
{
    TOPOLOGY_TRIANGLE_LIST,
    TOPOLOGY_TRIANGLE_STRIP,
    TOPOLOGY_TRIANGLE_FAN,
};

enum TextureType
{
    TEXTURE_TYPE_1D,
    TEXTURE_TYPE_2D,
    TEXTURE_TYPE_3D,
    TEXTURE_TYPE_2D_ARRAY,
};

enum Filter
{
    FILTER_NEAREST,
    FILTER_LINEAR,
};

enum SamplerMipmapMode
{
    SAMPLER_MIPMAP_MODE_NEAREST,
    SAMPLER_MIPMAP_MODE_LINEAR,
};

enum SamplerAddressMode
{
    SAMPLER_ADDRESS_MODE_REPEAT,
    SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT,
    SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
    SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE,
};

enum CompareOp
{
    COMPARE_OP_NEVER,
    COMPARE_OP_LESS,
    COMPARE_OP_EQUAL,
    COMPARE_OP_LESS_OR_EQUAL,
    COMPARE_OP_GREATER,
    COMPARE_OP_NOT_EQUAL,
    COMPARE_OP_GREATER_OR_EQUAL,
    COMPARE_OP_ALWAYS,
};

enum SamplerReductionMode
{
    SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE,
    SAMPLER_REDUCTION_MODE_MIN,
    SAMPLER_REDUCTION_MODE_MAX,
};

enum Format
{
#define RHI_XFMT(rhiFormat, vulkanFormat) rhiFormat,
#include "FormatTable.hpp"
};

// clang-format off
enum TextureUsageFlagBits : u32
{
    TEXTURE_USAGE_TRANSFER_SRC_BIT             = (1U << 0),
    TEXTURE_USAGE_TRANSFER_DST_BIT             = (1U << 1),
    TEXTURE_USAGE_SAMPLED_BIT                  = (1U << 2),
    TEXTURE_USAGE_STORAGE_BIT                  = (1U << 3),
    TEXTURE_USAGE_COLOR_ATTACHMENT_BIT         = (1U << 4),
    TEXTURE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT = (1U << 5),
};
using TextureUsageFlags = Flags;
// clang-format on

enum StageFlagBits : u32
{
#define RHI_XSTAGE(rhi, bit, vulkan) rhi = bit,
#include "StageTable.hpp"
};
using StageFlags = Flags;

enum AccessFlagBits : u32
{
#define RHI_XACCESS(rhi, bit, vulkan) rhi = bit,
#include "AccessTable.hpp"
};
using AccessFlags = Flags;

enum TextureLayout
{
    TEXTURE_LAYOUT_UNDEFINED,
    TEXTURE_LAYOUT_GENERAL,
    TEXTURE_LAYOUT_PRESENT_SRC,
};

enum AttachmentLoadOp
{
    ATTACHMENT_LOAD_OP_LOAD,
    ATTACHMENT_LOAD_OP_CLEAR,
    ATTACHMENT_LOAD_OP_DONT_CARE,
    ATTACHMENT_LOAD_OP_NONE,
};

enum AttachmentStoreOp
{
    ATTACHMENT_STORE_OP_STORE,
    ATTACHMENT_STORE_OP_DONT_CARE,
    ATTACHMENT_STORE_OP_NONE,
};

enum IndexType
{
    INDEX_TYPE_U8,
    INDEX_TYPE_U16,
    INDEX_TYPE_U32,
};

enum Queue
{
    QUEUE_GRAPHICS,
    QUEUE_COMPUTE,
};

// clang-format off
inline constexpr u32 ALL_MIPS   = (~0U);
inline constexpr u32 ALL_LAYERS = (~0U);
// clang-format on

// -----------------------------------------------------------------------------
// TODO: for now just enabling the needed for this project features.
bool Create(SDL_Window* window);
void Destroy();

// -----------------------------------------------------------------------------
// Buffer.
struct BufferDesc
{
    MemoryType type = MEMORY_TYPE_DEFAULT;
    u64 size;
    u64 minAlignment;
    const char* debugName;
};
// TODO: not sure if I should handle double-free explicitly, the pool API returns
// nullptr and then the RHI segfaults, for now this behavior seems reasonable
// (and a lot better than freeing some random stuff).

// Creates a default descriptor.
BufferHandle CreateBuffer(const BufferDesc&& desc);
void* GetBufferHostPtr(BufferHandle handle);
u64 GetBufferDevicePtr(BufferHandle handle);
void UnmapBuffer(BufferHandle handle);
void DestroyBuffer(BufferHandle handle);

// -----------------------------------------------------------------------------
// Texture.
struct TextureDesc
{
    Format format;
    TextureType type = TEXTURE_TYPE_2D;
    U32Vec3 dimensions;
    u32 mipCount = 1;
    u32 layerCount = 1;
    TextureUsageFlags usage;
    const char* debugName;
};

// Creates a default descriptor.
TextureHandle CreateTexture(const TextureDesc&& desc);
void DestroyTexture(TextureHandle handle);

struct TextureDescriptorDesc
{
    // TODO: so far I didn't find having different image/view formats useful.
    TextureHandle textureHandle;
    TextureType type = TEXTURE_TYPE_2D;
    u32 baseMip;
    u32 mipCount = ALL_MIPS;
    u32 baseLayer;
    u32 layerCount = ALL_LAYERS;
};
TextureDescriptorHandle CreateTextureDescriptor(const TextureDescriptorDesc&& desc);
void DestroyTextureDescriptor(TextureDescriptorHandle handle);

Format GetTextureFormat(TextureHandle handle);
U32Vec3 GetTextureDimensions(TextureHandle handle);

// TODO: kinda retarded, but works for now.
void UpdateTextureDescriptorSet(TextureHandle handle, u32 dstArrayElement);

// -----------------------------------------------------------------------------
// Sampler.

struct SamplerDesc
{
    Filter magFilter = FILTER_LINEAR;
    Filter minFilter = FILTER_LINEAR;
    SamplerReductionMode reductionMode = SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
    SamplerMipmapMode mipmapMode = SAMPLER_MIPMAP_MODE_LINEAR;
    SamplerAddressMode addressModeU = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerAddressMode addressModeV = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerAddressMode addressModeW = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    bool anisotropyEnable;
    f32 maxAnisotropy;
    bool compareEnable;
    CompareOp compareOp;
    f32 minLod;
    f32 maxLod;
};

SamplerHandle CreateSampler(const SamplerDesc&& desc);
void DestroySampler(SamplerHandle handle);

// -----------------------------------------------------------------------------
// Semaphore.
SemaphoreHandle CreateSemaphore(u64 initialValue);
void DestroySemaphore(SemaphoreHandle handle);
bool GetSemaphoreValue(SemaphoreHandle handle, u64& value);
bool WaitSemaphore(SemaphoreHandle handle, u64 value, u64 timeout = 1'000'000'000);

// -----------------------------------------------------------------------------
// Command buffer and queue.
// Command pools are reset in BeginNewFrame.
struct QueueSubmitDesc
{
    struct SemaphoreSubmitDesc
    {
        SemaphoreHandle semaphore;
        u64 value;
        RHI::StageFlags stageMask = RHI::STAGE_ALL_COMMANDS_BIT;
    };

    CommandBufferHandle cb;
    SliceArg<SemaphoreSubmitDesc> waitSemaphores;
    // I really didn't want to expose binary semaphores to the RHI.
    bool waitForTextureAcquire;
    bool signalReadyToPresent;
    SliceArg<SemaphoreSubmitDesc> signalSemaphores;
};
CommandBufferHandle CreateCommandBuffer(
    Queue queue,
    int frameInFlightIdx = 0,
    const char* debugName = nullptr
);
void DestroyCommandBuffer(CommandBufferHandle handle);
bool BeginCommandBuffer(CommandBufferHandle handle);
bool EndCommandBuffer(CommandBufferHandle handle);
bool QueueSubmit(Queue queue, const SliceArg<QueueSubmitDesc>&& desc);

// -----------------------------------------------------------------------------
// Pipeline.
struct ComputePipelineDesc
{
    Slice<u8> bytecode;
    bool usesBindlessTextures;
    const char* debugName;
};
PipelineHandle CreateComputePipeline(const ComputePipelineDesc&& desc);

struct PipelineColorTarget
{
    Format format;
    bool blendEnable;
    BlendOp colorOp = RHI::BLEND_OP_ADD;
    BlendFactor srcColorFactor = RHI::BLEND_FACTOR_ONE;
    BlendFactor dstColorFactor = RHI::BLEND_FACTOR_ZERO;
    BlendOp alphaOp = RHI::BLEND_OP_ADD;
    BlendFactor srcAlphaFactor = RHI::BLEND_FACTOR_ONE;
    BlendFactor dstAlphaFactor = RHI::BLEND_FACTOR_ZERO;
    ColorComponentFlags colorComponentMask = RHI::COLOR_COMPONENT_ALL_BITS;
};
struct GraphicsPipelineDesc
{
    SliceArg<Slice<u8>> bytecodes;
    bool usesBindlessTextures;
    Topology topology = TOPOLOGY_TRIANGLE_LIST;
    Cull cull = CULL_NONE;
    Format depthFormat = FORMAT_UNDEFINED;
    Format stencilFormat = FORMAT_UNDEFINED;
    DepthFlags depthMask;
    bool depthClampEnable;
    SliceArg<PipelineColorTarget> colorTargets;
    const char* debugName;
};
PipelineHandle CreateGraphicsPipeline(const GraphicsPipelineDesc&& desc);

U32Vec3 GetPipelineLocalSize(PipelineHandle handle);

void DestroyPipeline(PipelineHandle handle);

// -----------------------------------------------------------------------------
// Commands.
void CmdBarrier(
    CommandBufferHandle cb,
    StageFlags srcStageMask,
    AccessFlags srcAccessMask,
    StageFlags dstStageMask,
    AccessFlags dstAccessMask
);

struct TextureBarrierDesc
{
    TextureHandle handle;
    TextureLayout oldLayout;
    TextureLayout newLayout;
    StageFlags srcStageMask = STAGE_NONE;
    AccessFlags srcAccessMask = ACCESS_NONE;
    StageFlags dstStageMask = STAGE_NONE;
    AccessFlags dstAccessMask = ACCESS_NONE;
    u32 mipCount = ALL_MIPS;
    u32 layerCount = ALL_LAYERS;
};
void CmdTextureBarrier(CommandBufferHandle handle, const SliceArg<TextureBarrierDesc>&& desc);

// UNDEFINED -> GENERAL.
void CmdTextureInvalidateBarrier(
    CommandBufferHandle cb,
    StageFlags srcStageMask,
    AccessFlags srcAccessMask,
    StageFlags dstStageMask,
    AccessFlags dstAccessMask,
    const SliceArg<TextureHandle>&& textures
);

struct TextureSubresourceLayers
{
    u32 mipLevel;
    u32 baseArrayLayer;
    u32 layerCount = 1;
};
struct BufferTextureCopy
{
    u64 bufferOffset;
    u32 bufferRowLength;
    u32 bufferTextureHeight;
    TextureSubresourceLayers textureSubresource;
    I32Vec3 textureOffset;
    U32Vec3 textureDimensions;
};
void CmdCopyBufferToTexture(
    CommandBufferHandle cb,
    BufferHandle buffer,
    TextureHandle texture,
    const SliceArg<BufferTextureCopy>&& copyRegions
);

void CmdFillBuffer(CommandBufferHandle cb, BufferHandle buffer, u64 offset, u64 size, u32 data);

void CmdBindPipeline(CommandBufferHandle cb, PipelineHandle pipeline);

void CmdDispatch(CommandBufferHandle cb, U32Vec3 groupCount);

struct DescriptorInfo
{
    enum Type
    {
        TYPE_TEXTURE,
        TYPE_TEXTURE_DESCRIPTOR,
        TYPE_SAMPLER,
        TYPE_BUFFER,
    };

    Type type;

    union
    {
        TextureHandle texture;
        TextureDescriptorHandle textureDescriptor;
        SamplerHandle sampler;
        BufferHandle buffer;
    } resource;

    DescriptorInfo(TextureHandle handle) : type{TYPE_TEXTURE}, resource{.texture = handle} { }

    DescriptorInfo(TextureDescriptorHandle handle)
        : type{TYPE_TEXTURE_DESCRIPTOR}
        , resource{.textureDescriptor = handle}
    { }

    DescriptorInfo(SamplerHandle handle) : type{TYPE_SAMPLER}, resource{.sampler = handle} { }

    DescriptorInfo(BufferHandle handle) : type{TYPE_BUFFER}, resource{.buffer = handle} { }
};

void CmdPushDescriptors(
    CommandBufferHandle cb,
    PipelineHandle pipeline,
    const SliceArg<DescriptorInfo>&& descriptors
);

void CmdPushConstants(CommandBufferHandle cb, PipelineHandle pipeline, const void* data);

struct SetViewportDesc
{
    CommandBufferHandle cb;
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width;
    f32 height;
    f32 minDepth;
    f32 maxDepth = 1.0f;
};
void CmdSetViewport(const SetViewportDesc&& desc);

struct SetScissorDesc
{
    CommandBufferHandle cb;
    I32Vec2 offset;
    U32Vec2 extent;
};
void CmdSetScissor(const SetScissorDesc&& desc);

struct Attachment
{
    struct TextureOrDescriptorHandle
    {
        enum
        {
            TYPE_NONE,
            TYPE_TEXTURE,
            TYPE_TEXTURE_DESCRIPTOR,
        } type;

        union
        {
            TextureHandle texture;
            TextureDescriptorHandle descriptor;
        };

        TextureOrDescriptorHandle() : type{TYPE_NONE} { }

        TextureOrDescriptorHandle(TextureHandle handle) : type{TYPE_TEXTURE}, texture{handle} { }

        TextureOrDescriptorHandle(TextureDescriptorHandle handle)
            : type{TYPE_TEXTURE_DESCRIPTOR}
            , descriptor{handle}
        { }
    };

    TextureOrDescriptorHandle attachment;
    AttachmentLoadOp loadOp;
    AttachmentStoreOp storeOp;

    union
    {
        f32 color[3];
        f32 value;
    };
};
struct BeginRenderingDesc
{
    CommandBufferHandle cb;
    I32Vec2 offset;
    U32Vec2 extent;
    SliceArg<Attachment> colorTargets;
    Attachment depthTarget;
};
void CmdBeginRendering(const BeginRenderingDesc&& desc);
void CmdEndRendering(CommandBufferHandle cb);

void CmdBindIndexBuffer(
    CommandBufferHandle cb,
    BufferHandle buffer,
    u64 offset,
    IndexType indexType
);

void CmdDraw(
    CommandBufferHandle cb,
    u32 vertexCount,
    u32 instanceCount,
    u32 firstVertex,
    u32 firstInstance
);
void CmdDrawIndexed(
    CommandBufferHandle cb,
    u32 indexCount,
    u32 instanceCount,
    u32 firstIndex,
    i32 vertexOffset,
    u32 firstInstance
);

struct DrawIndirectCommand
{
    u32 vertexCount;
    u32 instanceCount;
    u32 firstVertex;
    u32 firstInstance;
};

void CmdDrawIndirect(
    CommandBufferHandle cb,
    BufferHandle buffer,
    u64 offset,
    u32 drawCount,
    u32 stride = sizeof(DrawIndirectCommand)
);

struct DrawIndexedIndirectCommand
{
    u32 indexCount;
    u32 instanceCount;
    u32 firstIndex;
    i32 vertexOffset;
    u32 firstInstance;
};

void CmdDrawIndexedIndirectCount(
    CommandBufferHandle cb,
    BufferHandle buffer,
    u64 offset,
    BufferHandle countBuffer,
    u64 countBufferOffset,
    u32 maxDrawCount,
    u32 stride = sizeof(DrawIndexedIndirectCommand)
);

// TODO: kinda retarded, but works for now.
void CmdBindTextureDescriptorSet(CommandBufferHandle cb, RHI::PipelineHandle pipeline);

// -----------------------------------------------------------------------------
// Swapchain.
// TODO: swapchain API is rough, but idk.
enum SwapchainResult
{
    SWAPCHAIN_SUCCESS,
    SWAPCHAIN_OUT_OF_DATE,
    SWAPCHAIN_SUBOPTIMAL,
    SWAPCHAIN_ERROR,
};
bool CreateSwapchain(U32Vec2 size);
void DestroySwapchain();
SwapchainResult AcquireNextSwapchainTexture(RHI::TextureHandle& swapchainTextureHandle);
RHI::TextureHandle GetSwapchainTexture(u32 idx);
SwapchainResult QueuePresent(Queue queue);

// -----------------------------------------------------------------------------
// Misc stuff.
bool DeviceWaitIdle();
bool QueueWaitIdle(Queue queue);
// TODO: It's not great that I expose these concepts, but it simplifies some
// stuff so for now it's alright.
bool BeginNewFrame(int frameInFlightIdx);
}

#undef RHI_HANDLE
