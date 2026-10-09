#include "uniform_buffer_manager.hpp"

#include "../Utils.hpp"

#include <stdio.h>

void UniformBufferManager::init(int size, int alignment) {
    m_cpu_buffer = static_cast<u8*>(utils::xmalloc(size_t(size)));
    m_size = size;
    m_alignment = alignment;
    m_offset = 0;
    m_buffer = RHI::CreateBuffer({
        .type = RHI::MEMORY_TYPE_DEFAULT_UNIFORM,
        .size = u64(size),
        .debugName = "UniformBufferManager/UniformBuffer",
    });
}

void UniformBufferManager::cleanup() {
    free(m_cpu_buffer);
    m_offset = 0;
    RHI::DestroyBuffer(m_buffer);
}

void UniformBufferManager::on_new_frame() {
    m_flushed_offset = 0;
    m_offset = 0;
}

RHI::DescriptorInfo UniformBufferManager::push(const void* data, int size) {
    ASSERT(size < RHI::UNIFORM_BUFFER_MAX_SIZE_BYTES);

    m_offset = utils::align_up_pow2(m_offset, m_alignment);
    ASSERT(m_offset + size < m_size);

    memcpy(m_cpu_buffer + m_offset, data, size);

    const RHI::DescriptorInfo result{m_buffer, u64(m_offset), u64(size)};

    m_offset += size;

    return result;
}

void UniformBufferManager::flush() {
    if (m_flushed_offset == m_offset) {
        return;
    }
    DEBUG_ASSERT(m_offset > m_flushed_offset);

    u8* const dst = static_cast<u8*>(RHI::GetBufferHostPtr(m_buffer)) + m_flushed_offset;
    const u8* const src = m_cpu_buffer + m_flushed_offset;
    const int size = m_offset - m_flushed_offset;

    memcpy(dst, src, size_t(size));

    m_flushed_offset = m_offset;
}
