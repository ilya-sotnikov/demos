#include "UniformBufferManager.hpp"

#include "../Utils.hpp"

#include <stdio.h>

void UniformBufferManager::Init(int size, int alignment)
{
    mCpuBuffer = static_cast<u8*>(Utils::xmalloc(size_t(size)));
    mSize = size;
    mAlignment = alignment;
    mOffset = 0;
    mBuffer = RHI::CreateBuffer({
        .type = RHI::MEMORY_TYPE_DEFAULT_UNIFORM,
        .size = u64(size),
        .debugName = "UniformBufferManager/UniformBuffer",
    });
}

void UniformBufferManager::Cleanup()
{
    free(mCpuBuffer);
    mOffset = 0;
    RHI::DestroyBuffer(mBuffer);
}

void UniformBufferManager::OnNewFrame()
{
    mFlushedOffset = 0;
    mOffset = 0;
}

RHI::DescriptorInfo UniformBufferManager::Push(const void* data, int size)
{
    ASSERT(size < RHI::UNIFORM_BUFFER_MAX_SIZE_BYTES);

    mOffset = Utils::AlignUpPow2(mOffset, mAlignment);
    ASSERT(mOffset + size < mSize);

    memcpy(mCpuBuffer + mOffset, data, size);

    const RHI::DescriptorInfo result{mBuffer, u64(mOffset), u64(size)};

    mOffset += size;

    return result;
}

void UniformBufferManager::Flush()
{
    if (mFlushedOffset == mOffset)
    {
        return;
    }
    DEBUG_ASSERT(mOffset > mFlushedOffset);

    u8* const dst = static_cast<u8*>(RHI::GetBufferHostPtr(mBuffer)) + mFlushedOffset;
    const u8* const src = mCpuBuffer + mFlushedOffset;
    const int size = mOffset - mFlushedOffset;

    memcpy(dst, src, size_t(size));

    mFlushedOffset = mOffset;
}
