#pragma once

#include "common.hpp"
#include "renderer/rhi/rhi.hpp"

struct UniformBufferManager
{
    void init(int size, int alignment);
    void cleanup();

    void on_new_frame();
    rhi::DescriptorInfo push(const void* data, int size);
    void flush();

    u8* m_cpu_buffer;
    int m_size;
    int m_alignment;
    int m_offset;
    int m_flushed_offset;
    rhi::Buffer m_buffer;
};
