#pragma once

#include "Common.hpp"
#include "RHI/RHI.hpp"

struct UniformBufferManager {
    void init(int size, int alignment);
    void cleanup();

    void on_new_frame();
    RHI::DescriptorInfo push(const void* data, int size);
    void flush();

    u8* m_cpu_buffer;
    int m_size;
    int m_alignment;
    int m_offset;
    int m_flushed_offset;
    RHI::Buffer m_buffer;
};