#pragma once

#include "common.hpp"
#include "slice_arg.hpp"

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
        u32 idx = rhi::INVALID_HANDLE; \
        u32 generation; \
        explicit operator bool() const \
        { \
            return idx != rhi::INVALID_HANDLE; \
        } \
        static type invalid() \
        { \
            return {.idx = rhi::INVALID_HANDLE}; \
        } \
    }

struct SDL_Window;

// #define RHI_ENABLE_DEBUG_UTILS

namespace rhi
{

using Flags = u32;
using Flags64 = u64;

inline constexpr u32 INVALID_HANDLE = UINT32_MAX;
inline constexpr int FRAMES_IN_FLIGHT = 2;
inline constexpr u32 MAX_BINDLESS_DESCRIPTOR_COUNT = 16384;
inline constexpr int PUSH_CONSTANTS_MAX_SIZE_BYTES = 128;
inline constexpr int UNIFORM_BUFFER_MAX_SIZE_BYTES = 16384;
inline constexpr u64 WHOLE_SIZE = UINT64_MAX;
inline constexpr u64 MAX_PHYSICAL_DEVICE_NAME_SIZE = 256;

RHI_HANDLE(Pipeline);
RHI_HANDLE(Buffer);
RHI_HANDLE(Texture);
RHI_HANDLE(TextureDescriptor);
RHI_HANDLE(Sampler);
RHI_HANDLE(Semaphore);
RHI_HANDLE(CommandBuffer);

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

// TODO: Honestly, not worth it.
enum Format
{
#define RHI_XFMT(rhi_format, vulkanFormat) rhi_format,
#include "format_table.hpp"
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
#include "stage_table.hpp"
};
using StageFlags = Flags;

enum AccessFlagBits : u32
{
#define RHI_XACCESS(rhi, bit, vulkan) rhi = bit,
#include "access_table.hpp"
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
// TODO: For now just enabling the needed for this project features.
void create(SDL_Window* window);
void destroy();

struct DeviceProperties
{
    u64 min_uniform_buffer_offset_alignment;
    char name[MAX_PHYSICAL_DEVICE_NAME_SIZE];
};
const DeviceProperties& get_device_properties();

// -----------------------------------------------------------------------------
// Buffer.
struct BufferDesc
{
    MemoryType type = MEMORY_TYPE_DEFAULT;
    u64 size;
    u64 min_alignment;
    const char* debug_name;
};
// TODO: Not sure if I should handle double-free explicitly, the pool API returns
// nullptr and then the RHI segfaults, for now this behavior seems reasonable
// (and a lot better than freeing some random stuff).

// Creates a default descriptor.
Buffer create_buffer(const BufferDesc&& desc);
void* get_buffer_host_ptr(Buffer buffer);
u64 get_buffer_device_ptr(Buffer buffer);
void unmap_buffer(Buffer buffer);
void destroy_buffer(Buffer buffer);

// -----------------------------------------------------------------------------
// Texture.
struct TextureDesc
{
    Format format;
    TextureType type = TEXTURE_TYPE_2D;
    Vec3U32 dimensions;
    u32 mip_count = 1;
    u32 layer_count = 1;
    TextureUsageFlags usage;
    const char* debug_name;
};

// Creates a default descriptor.
Texture create_texture(const TextureDesc&& desc);
void destroy_texture(Texture texture);

struct TextureDescriptorDesc
{
    // TODO: So far I didn't find having different image/view formats useful.
    Texture texture;
    TextureType type = TEXTURE_TYPE_2D;
    u32 base_mip;
    u32 mip_count = ALL_MIPS;
    u32 base_layer;
    u32 layer_count = ALL_LAYERS;
};
TextureDescriptor create_texture_descriptor(const TextureDescriptorDesc&& desc);
void destroy_texture_descriptor(TextureDescriptor descriptor);

Format get_texture_format(Texture texture);
Vec3U32 get_texture_dimensions(Texture texture);

// TODO: Kinda retarded, but works for now.
void update_texture_descriptor_set(Texture texture, u32 dst_array_element);

// -----------------------------------------------------------------------------
// Sampler.

struct SamplerDesc
{
    Filter mag_filter = FILTER_LINEAR;
    Filter min_filter = FILTER_LINEAR;
    SamplerReductionMode reduction_mode = SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE;
    SamplerMipmapMode mipmap_mode = SAMPLER_MIPMAP_MODE_LINEAR;
    SamplerAddressMode address_mode_u = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerAddressMode address_mode_v = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    SamplerAddressMode address_mode_w = SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    bool anisotropy_enable;
    f32 max_anisotropy;
    bool compare_enable;
    CompareOp compare_op;
    f32 min_lod;
    f32 max_lod;
};

Sampler create_sampler(const SamplerDesc&& desc);
void destroy_sampler(Sampler sampler);

// -----------------------------------------------------------------------------
// Semaphore.
Semaphore create_semaphore(u64 initial_value);
void destroy_semaphore(Semaphore semaphore);
u64 get_semaphore_value(Semaphore semaphore);
void wait_semaphore(Semaphore semaphore, u64 value, u64 timeout = 1'000'000'000);

// -----------------------------------------------------------------------------
// Command buffer and queue.
// Command pools are reset in BeginNewFrame.
struct QueueSubmitDesc
{
    struct SemaphoreSubmitDesc
    {
        Semaphore semaphore;
        u64 value;
        rhi::StageFlags stage_mask = rhi::STAGE_ALL_COMMANDS_BIT;
    };

    CommandBuffer cb;
    SliceArg<SemaphoreSubmitDesc> wait_semaphores;
    // I really didn't want to expose binary semaphores to the RHI.
    bool wait_for_texture_acquire;
    bool signal_ready_to_present;
    SliceArg<SemaphoreSubmitDesc> signal_semaphores;
};
CommandBuffer create_command_buffer(
    Queue queue,
    int frame_in_flight_idx = 0,
    const char* debug_name = nullptr
);
void destroy_command_buffer(CommandBuffer cb);
void begin_command_buffer(CommandBuffer cb);
void end_command_buffer(CommandBuffer cb);
void queue_submit(Queue queue, const SliceArg<QueueSubmitDesc>&& desc);

// -----------------------------------------------------------------------------
// Pipeline.
struct ComputePipelineDesc
{
    Slice<u8> bytecode;
    bool uses_bindless_textures;
    const char* debug_name;
};
Pipeline create_compute_pipeline(const ComputePipelineDesc&& desc);

struct PipelineColorTarget
{
    Format format;
    bool blend_enable;
    BlendOp color_op = rhi::BLEND_OP_ADD;
    BlendFactor src_color_factor = rhi::BLEND_FACTOR_ONE;
    BlendFactor dst_color_factor = rhi::BLEND_FACTOR_ZERO;
    BlendOp alpha_op = rhi::BLEND_OP_ADD;
    BlendFactor src_alpha_factor = rhi::BLEND_FACTOR_ONE;
    BlendFactor dst_alpha_factor = rhi::BLEND_FACTOR_ZERO;
    ColorComponentFlags color_component_mask = rhi::COLOR_COMPONENT_ALL_BITS;
};
struct GraphicsPipelineDesc
{
    SliceArg<Slice<u8>> bytecodes;
    bool uses_bindless_textures;
    Topology topology = TOPOLOGY_TRIANGLE_LIST;
    Cull cull = CULL_NONE;
    Format depth_format = FORMAT_UNDEFINED;
    Format stencil_format = FORMAT_UNDEFINED;
    DepthFlags depth_mask;
    bool depth_clamp_enable;
    SliceArg<PipelineColorTarget> color_targets;
    const char* debug_name;
};
Pipeline create_graphics_pipeline(const GraphicsPipelineDesc&& desc);

Vec3U32 get_pipeline_local_size(Pipeline pipeline);

void destroy_pipeline(Pipeline pipeline);

// -----------------------------------------------------------------------------
// Commands.
void cmd_barrier(
    CommandBuffer cb,
    StageFlags src_stage_mask,
    AccessFlags src_access_mask,
    StageFlags dst_stage_mask,
    AccessFlags dst_access_mask
);

struct TextureBarrierDesc
{
    Texture texture;
    TextureLayout old_layout;
    TextureLayout new_layout;
    StageFlags src_stage_mask = STAGE_NONE;
    AccessFlags src_access_mask = ACCESS_NONE;
    StageFlags dst_stage_mask = STAGE_NONE;
    AccessFlags dst_access_mask = ACCESS_NONE;
    u32 mip_count = ALL_MIPS;
    u32 layer_count = ALL_LAYERS;
};
void cmd_texture_barrier(CommandBuffer cb, const SliceArg<TextureBarrierDesc>&& desc);

// UNDEFINED -> GENERAL.
void cmd_texture_invalidate_barrier(
    CommandBuffer cb,
    StageFlags src_stage_mask,
    AccessFlags src_access_mask,
    StageFlags dst_stage_mask,
    AccessFlags dst_access_mask,
    const SliceArg<Texture>&& textures
);

struct TextureSubresourceLayers
{
    u32 mip_level;
    u32 base_array_layer;
    u32 layer_count = 1;
};
struct BufferTextureCopy
{
    u64 buffer_offset;
    u32 buffer_row_length;
    u32 buffer_texture_height;
    TextureSubresourceLayers texture_subresource;
    Vec3I32 texture_offset;
    Vec3U32 texture_dimensions;
};
void cmd_copy_buffer_to_texture(
    CommandBuffer cb,
    Buffer buffer,
    Texture texture,
    const SliceArg<BufferTextureCopy>&& copy_regions
);

void cmd_fill_buffer(CommandBuffer cb, Buffer buffer, u64 offset, u64 size, u32 data);

void cmd_bind_pipeline(CommandBuffer cb, Pipeline pipeline);

void cmd_dispatch(CommandBuffer cb, Vec3U32 group_count);

struct DescriptorInfo
{
    enum Type
    {
        TYPE_NONE,
        TYPE_TEXTURE,
        TYPE_TEXTURE_DESCRIPTOR,
        TYPE_SAMPLER,
        TYPE_BUFFER,
    };

    Type type;

    union
    {
        Texture texture;
        TextureDescriptor texture_descriptor;
        Sampler sampler;

        struct BufferInfo
        {
            Buffer buffer;
            u64 offset;
            u64 range;
        } buffer;
    } resource;

    DescriptorInfo() : type{TYPE_NONE}, resource{.buffer{}} { }

    DescriptorInfo(Texture texture) : type{TYPE_TEXTURE}, resource{.texture = texture} { }

    DescriptorInfo(TextureDescriptor descriptor)
        : type{TYPE_TEXTURE_DESCRIPTOR}
        , resource{.texture_descriptor = descriptor}
    { }

    DescriptorInfo(Sampler sampler) : type{TYPE_SAMPLER}, resource{.sampler = sampler} { }

    DescriptorInfo(Buffer buffer, u64 offset = 0, u64 range = WHOLE_SIZE)
        : type{TYPE_BUFFER}
        , resource{.buffer = {buffer, offset, range}}
    { }
};

void cmd_push_descriptors(
    CommandBuffer cb,
    Pipeline pipeline,
    const SliceArg<DescriptorInfo>&& descriptors
);

void cmd_push_constants(CommandBuffer cb, Pipeline pipeline, const void* data);

struct SetViewportDesc
{
    CommandBuffer cb;
    f32 x = 0.0f;
    f32 y = 0.0f;
    f32 width;
    f32 height;
    f32 min_depth;
    f32 max_depth = 1.0f;
};
void cmd_set_viewport(const SetViewportDesc&& desc);

struct SetScissorDesc
{
    CommandBuffer cb;
    Vec2I32 offset;
    Vec2U32 extent;
};
void cmd_set_scissor(const SetScissorDesc&& desc);

struct Attachment
{
    struct TextureOrDescriptor
    {
        enum
        {
            TYPE_NONE,
            TYPE_TEXTURE,
            TYPE_TEXTURE_DESCRIPTOR,
        } type;

        union
        {
            Texture texture;
            TextureDescriptor descriptor;
        };

        TextureOrDescriptor() : type{TYPE_NONE} { }

        TextureOrDescriptor(Texture texture) : type{TYPE_TEXTURE}, texture{texture} { }

        TextureOrDescriptor(TextureDescriptor descriptor)
            : type{TYPE_TEXTURE_DESCRIPTOR}
            , descriptor{descriptor}
        { }
    };

    TextureOrDescriptor attachment;
    AttachmentLoadOp load_op;
    AttachmentStoreOp store_op;

    union
    {
        f32 color[3];
        f32 value;
    };
};
struct BeginRenderingDesc
{
    CommandBuffer cb;
    Vec2I32 offset;
    Vec2U32 extent;
    SliceArg<Attachment> color_targets;
    Attachment depth_target;
};
void cmd_begin_rendering(const BeginRenderingDesc&& desc);
void cmd_end_rendering(CommandBuffer cb);

void cmd_bind_index_buffer(CommandBuffer cb, Buffer buffer, u64 offset, IndexType index_type);

void cmd_draw(
    CommandBuffer cb,
    u32 vertex_count,
    u32 instance_count,
    u32 first_vertex,
    u32 first_instance
);
void cmd_draw_indexed(
    CommandBuffer cb,
    u32 index_count,
    u32 instance_count,
    u32 first_index,
    i32 vertex_offset,
    u32 first_instance
);

struct DrawIndirectCommand
{
    u32 vertex_count;
    u32 instance_count;
    u32 first_vertex;
    u32 first_instance;
};

void cmd_draw_indirect(
    CommandBuffer cb,
    Buffer buffer,
    u64 offset,
    u32 draw_count,
    u32 stride = sizeof(DrawIndirectCommand)
);

struct DrawIndexedIndirectCommand
{
    u32 index_count;
    u32 instance_count;
    u32 first_index;
    i32 vertex_offset;
    u32 first_instance;
};

void cmd_draw_indexed_indirect_count(
    CommandBuffer cb,
    Buffer buffer,
    u64 offset,
    Buffer count_buffer,
    u64 count_buffer_offset,
    u32 max_draw_count,
    u32 stride = sizeof(DrawIndexedIndirectCommand)
);

// TODO: Kinda retarded, but works for now.
void cmd_bind_texture_descriptor_set(CommandBuffer cb, rhi::Pipeline pipeline);

// -----------------------------------------------------------------------------
// Swapchain.
// TODO: Swapchain API is rough, but idk.
enum SwapchainResult
{
    SWAPCHAIN_SUCCESS,
    SWAPCHAIN_OUT_OF_DATE,
    SWAPCHAIN_SUBOPTIMAL,
    SWAPCHAIN_ERROR,
};
void create_swapchain(Vec2U32 size);
void destroy_swapchain();
SwapchainResult acquire_next_swapchain_texture(rhi::Texture& swapchain_texture);
rhi::Texture get_swapchain_texture(u32 idx);
SwapchainResult queue_present(Queue queue);

// -----------------------------------------------------------------------------
// Misc stuff.
void device_wait_idle();
void queue_wait_idle(Queue queue);
// TODO: It's not great that I expose these concepts, but it simplifies some
// stuff so for now it's alright.
void begin_new_frame(int frame_in_flight_idx);
} // namespace rhi

#undef RHI_HANDLE
