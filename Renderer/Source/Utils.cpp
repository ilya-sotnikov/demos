#include "Utils.hpp"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

Utils::FileData Utils::FileRead(const char* path)
{
    DEBUG_ASSERT(path);

    FileData result{};

    FILE* const fp = fopen(path, "rb");
    if (!fp)
    {
        fprintf(stderr, "%s: fopen %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    DEFER(fclose(fp));

    if (fseek(fp, 0, SEEK_END))
    {
        fprintf(stderr, "%s: fseek SEEK_END %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    const long fileSize = ftell(fp);
    if (fileSize == -1)
    {
        fprintf(stderr, "%s: ftell %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }
    if (fseek(fp, 0, SEEK_SET))
    {
        fprintf(stderr, "%s: fseek SEEK_SET %s failed: %s\n", __func__, path, strerror(errno));
        return result;
    }

    void* const res = malloc(size_t((fileSize + 1)) * sizeof(u8));
    if (!res)
    {
        fprintf(stderr, "%s: malloc failed (size %ld): %s\n", __func__, fileSize, strerror(errno));
        return result;
    }

    if (fread(res, sizeof(u8), size_t(fileSize), fp) != size_t(fileSize))
    {

        if (feof(fp))
        {
            fprintf(stderr, "%s: fread %s failed: EOF\n", __func__, path);
        }
        else if (ferror(fp))
        {
            fprintf(stderr, "%s: fread %s failed: %s\n", __func__, path, strerror(errno));
        }
        free(res);
        return result;
    }

    result.data = res;
    result.size = fileSize;

    return result;
}

void* Utils::xmalloc(size_t size)
{
    void* const ret = malloc(size);
    if (!ret)
    {
        fprintf(stderr, "malloc failed (size = %zu)\n", size);
        exit(1);
    }
    return ret;
}

void* Utils::xrealloc(void* ptr, size_t newSize)
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
size_t Utils::strlcpy(char* dst, const char* src, size_t dsize)
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

    return size_t(src - osrc - 1); /* count does not include NUL */
}

/*
 * Appends src to string dst of size dsize (unlike strncat, dsize is the
 * full size of dst, not space left).  At most dsize-1 characters
 * will be copied.  Always NUL terminates (unless dsize <= strlen(dst)).
 * Returns strlen(src) + MIN(dsize, strlen(initial dst)).
 * If retval >= dsize, truncation occurred.
 */
size_t Utils::strlcat(char* dst, const char* src, size_t dsize)
{
    const char* odst = dst;
    const char* osrc = src;
    size_t n = dsize;
    size_t dlen;

    /* Find the end of dst and adjust bytes left but don't go past end. */
    while (n-- != 0 && *dst != '\0')
        dst++;
    dlen = size_t(dst - odst);
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

    return (dlen + size_t(src - osrc)); /* count does not include NUL */
}

void Utils::FpsCounter::Update(f64& fps, f64 time)
{
    const f64 elapsedTime = time - mPrevTime;

    if (elapsedTime > 0.25)
    {
        mPrevTime = time;
        fps = f64(mFrameCount) / elapsedTime;
        mFrameCount = 0;
    }

    ++mFrameCount;
}

void Utils::MemoryDivider::Init(void* memory, ptrdiff_t size)
{
    mMemory = memory;
    mSize = size;
    mCurrentOffset = 0;
}

MemorySlice Utils::MemoryDivider::Take(ptrdiff_t bytes)
{
    DEBUG_ASSERT(mSize > 0);
    DEBUG_ASSERT(mMemory);
    DEBUG_ASSERT(mCurrentOffset < mSize);

    MemorySlice result{};

    const ptrdiff_t available = mSize - mCurrentOffset;
    if (bytes <= available)
    {
        result.count = bytes;
        result.data = static_cast<uchar*>(mMemory) + mCurrentOffset;
        mCurrentOffset += bytes + 1;
    }

    return result;
}

MemorySlice Utils::MemoryDivider::TakeRest()
{
    DEBUG_ASSERT(mSize > 0);
    DEBUG_ASSERT(mMemory);
    DEBUG_ASSERT(mCurrentOffset < mSize);

    MemorySlice result{};
    result.count = mSize - mCurrentOffset - 1;
    result.data = static_cast<uchar*>(mMemory) + mCurrentOffset;
    mCurrentOffset += result.count + 1;
    DEBUG_ASSERT(mCurrentOffset == mSize);

    return result;
}

f32 Utils::LinearToSrgb(f32 color, f32 gamma)
{
    DEBUG_ASSERT(gamma > 0.0f);
    return powf(color, 1.0f / gamma);
}

f32 Utils::SrgbToLinear(f32 color, f32 gamma)
{
    DEBUG_ASSERT(gamma > 0.0f);
    return powf(color, gamma);
}

Vec3 Utils::LinearToSrgb(Vec3 color, f32 gamma)
{
    DEBUG_ASSERT(gamma > 0.0f);
    f32 inverseGamma = 1.0f / gamma;
    return {
        powf(color.val[0], inverseGamma),
        powf(color.val[1], inverseGamma),
        powf(color.val[2], inverseGamma)
    };
}

Vec3 Utils::SrgbToLinear(Vec3 color, f32 gamma)
{
    DEBUG_ASSERT(gamma > 0.0f);
    return {powf(color.val[0], gamma), powf(color.val[1], gamma), powf(color.val[2], gamma)};
}
