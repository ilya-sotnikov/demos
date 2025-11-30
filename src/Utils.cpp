#include "Utils.hpp"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

namespace Utils
{

void* xmalloc(size_t size)
{
    void* const ret = malloc(size);
    if (!ret)
    {
        fprintf(stderr, "malloc failed (size = %zu)\n", size);
        exit(1);
    }
    return ret;
}

void* xrealloc(void* ptr, size_t newSize)
{
    void* const ret = realloc(ptr, newSize);
    if (!ret)
    {
        fprintf(stderr, "realloc failed (newSize = %zu)\n", newSize);
        exit(1);
    }
    return ret;
}

/*
 * Copy string src to buffer dst of size dsize.  At most dsize-1
 * chars will be copied.  Always NUL terminates (unless dsize == 0).
 * Returns strlen(src); if retval >= dsize, truncation occurred.
 */
size_t strlcpy(char* dst, const char* src, size_t dsize)
{
    const char* osrc = src;
    size_t nleft = dsize;

    /* Copy as many bytes as will fit. */
    if (nleft != 0)
    {
        while (--nleft != 0)
        {
            if ((*dst++ = *src++) == '\0')
                break;
        }
    }

    /* Not enough room in dst, add NUL and traverse rest of src. */
    if (nleft == 0)
    {
        if (dsize != 0)
            *dst = '\0'; /* NUL-terminate dst */
        while (*src++)
            ;
    }

    return static_cast<size_t>(src - osrc - 1); /* count does not include NUL */
}

/*
 * Appends src to string dst of size dsize (unlike strncat, dsize is the
 * full size of dst, not space left).  At most dsize-1 characters
 * will be copied.  Always NUL terminates (unless dsize <= strlen(dst)).
 * Returns strlen(src) + MIN(dsize, strlen(initial dst)).
 * If retval >= dsize, truncation occurred.
 */
size_t strlcat(char* dst, const char* src, size_t dsize)
{
    const char* odst = dst;
    const char* osrc = src;
    size_t n = dsize;
    size_t dlen;

    /* Find the end of dst and adjust bytes left but don't go past end. */
    while (n-- != 0 && *dst != '\0')
        dst++;
    dlen = static_cast<size_t>(dst - odst);
    n = dsize - dlen;

    if (n-- == 0)
        return (dlen + strlen(src));
    while (*src != '\0')
    {
        if (n != 0)
        {
            *dst++ = *src;
            n--;
        }
        src++;
    }
    *dst = '\0';

    return (dlen + static_cast<size_t>(src - osrc)); /* count does not include NUL */
}

void FpsCounter::Update(f64& fps, f64 time)
{
    const f64 elapsedTime = time - mPrevTime;

    if (elapsedTime > 0.25)
    {
        mPrevTime = time;
        fps = static_cast<f64>(mFrameCount) / elapsedTime;
        mFrameCount = 0;
    }

    ++mFrameCount;
}

void MemoryDivider::Init(void* memory, ptrdiff_t size)
{
    mMemory = memory;
    mSize = size;
    mCurrentOffset = 0;
}

MemorySlice MemoryDivider::Take(ptrdiff_t bytes)
{
    assert(mSize > 0);
    assert(mMemory);
    assert(mCurrentOffset < mSize);

    MemorySlice result{};

    const ptrdiff_t available = mSize - mCurrentOffset;
    if (bytes <= available)
    {
        result.mCount = bytes;
        result.mData = static_cast<uchar*>(mMemory) + mCurrentOffset;
        mCurrentOffset += bytes + 1;
    }

    return result;
}

MemorySlice MemoryDivider::TakeRest()
{
    assert(mSize > 0);
    assert(mMemory);
    assert(mCurrentOffset < mSize);

    MemorySlice result{};
    result.mCount = mSize - mCurrentOffset - 1;
    result.mData = static_cast<uchar*>(mMemory) + mCurrentOffset;
    mCurrentOffset += result.mCount + 1;
    assert(mCurrentOffset == mSize);

    return result;
}

}
