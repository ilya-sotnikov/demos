#pragma once

#include "../Common.hpp"
#include "RHI/RHI.hpp"

struct UniformBufferManager {
    void Init(int size, int alignment);
    void Cleanup();

    void OnNewFrame();
    RHI::DescriptorInfo Push(const void* data, int size);
    void Flush();

    u8* mCpuBuffer;
    int mSize;
    int mAlignment;
    int mOffset;
    int mFlushedOffset;
    RHI::Buffer mBuffer;
};