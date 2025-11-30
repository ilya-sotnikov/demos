#include "Arena.hpp"

#include "Utils.hpp"

#include <stdio.h>

static bool IsPowerOfTwo(ptrdiff_t x)
{
    return (x & (x - 1)) == 0;
}

static ptrdiff_t AlignForward(ptrdiff_t ptr, ptrdiff_t align)
{
    assert(IsPowerOfTwo(align));
    (void)IsPowerOfTwo;

    ptrdiff_t alignedPtr = ptr;
    const ptrdiff_t modulo = alignedPtr & (align - 1);

    if (modulo != 0)
    {
        alignedPtr += align - modulo;
    }

    return alignedPtr;
}

void Arena::Init(void* backingBuffer, ptrdiff_t size, const char* name)
{
    assert(backingBuffer);
    assert(size > 0);

    mBuffer = static_cast<uchar*>(backingBuffer);
    mBufferSize = size;
    mCurrentOffset = 0;
    mMaxOffset = 0;
    Utils::strlcpy(mName, name ? name : "Unnamed", sizeof(mName));
}

void Arena::Init(ptrdiff_t size, const char* name)
{
    assert(size > 0);

    mBuffer = static_cast<uchar*>(Utils::xmalloc(static_cast<size_t>(size)));
    mBufferSize = size;
    mCurrentOffset = 0;
    mMaxOffset = 0;
    Utils::strlcpy(mName, name ? name : "Unnamed", sizeof(mName));
}

void* Arena::Alloc(ptrdiff_t size, ptrdiff_t align, int flags)
{
    const ptrdiff_t currPtr = reinterpret_cast<ptrdiff_t>(mBuffer) + mCurrentOffset;
    ptrdiff_t offset = AlignForward(currPtr, align);
    offset -= reinterpret_cast<ptrdiff_t>(mBuffer);

    if ((size > PTRDIFF_MAX - offset) || (offset + size > mBufferSize))
    {
        return nullptr;
    }

    void* const ptr = &mBuffer[offset];
    mCurrentOffset = offset + size;
    mMaxOffset = mCurrentOffset;
    if (!(flags & FlagNoZero))
    {
        memset(ptr, 0, static_cast<size_t>(size));
    }

    return ptr;
}

void* Arena::AllocOrDie(ptrdiff_t size, ptrdiff_t align, int flags)
{
    void* const ret = Alloc(size, align, flags);
    if (!ret)
    {
        fprintf(
            stderr,
            "Arena::Alloc failed (size = %td, align = %td, name = %s)\n",
            size,
            align,
            mName
        );
        exit(1);
    }
    return ret;
}

void Arena::FreeAll()
{
    mCurrentOffset = 0;
}

void Arena::FreeBuffer()
{
    SAFE_FREE(mBuffer);
}
