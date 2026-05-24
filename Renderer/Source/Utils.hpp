#pragma once

#include "Common.hpp"
#include "Math/Types.hpp"

#include <string.h>
#include <stdlib.h>

namespace Utils
{

struct FileData
{
    void* data;
    long size;
};

// gives ownership, call free()
FileData FileRead(const char* path);

// Print an error message and exit(1).
void* xmalloc(size_t size);
// Print an error message and exit(1).
void* xrealloc(void* ptr, size_t newSize);

// yoinked from OpenBSD, sane C-style string handling.
size_t strlcpy(char* dst, const char* src, size_t dsize);
size_t strlcat(char* dst, const char* src, size_t dsize);

// Type-punning through memcpy to avoid strict aliasing violation.
template <typename To, typename From>
To BitCast(From src)
{
    static_assert(sizeof(To) == sizeof(From));
    To dst;
    memcpy(&dst, &src, sizeof(To));
    return dst;
}

struct FpsCounter
{
    f64 mPrevTime;
    u32 mFrameCount;

    void Update(f64& fps, f64 time);
};

struct MemoryDivider
{
    void* mMemory;
    ptrdiff_t mCurrentOffset;
    ptrdiff_t mSize;

    void Init(void* memory, ptrdiff_t size);
    MemorySlice Take(ptrdiff_t bytes);
    MemorySlice TakeRest();
};

template <typename T>
T AlignUpPow2(T val, T alignment)
{
    DEBUG_ASSERT(alignment % 2 == 0);
    return (val + alignment - 1) & ~(alignment - 1);
}

inline u32 GetMipLevels(u32 width, u32 height)
{
    u32 mipLevels = 1;

    while (width > 1 || height > 1)
    {
        ++mipLevels;
        width /= 2;
        height /= 2;
    }

    return mipLevels;
}

// Approximations.
f32 LinearToSrgb(f32 color, f32 gamma = 2.2f);
f32 SrgbToLinear(f32 color, f32 gamma = 2.2f);
Vec3 LinearToSrgb(Vec3 color, f32 gamma = 2.2f);
Vec3 SrgbToLinear(Vec3 color, f32 gamma = 2.2f);

}

// Free and ptr = nullptr to avoid accidental double free.
#define SAFE_FREE(ptr) \
    do \
    { \
        free(ptr); \
        ptr = nullptr; \
    } \
    while (0)

#define SAFE_DELETE(ptr) \
    do \
    { \
        delete ptr; \
        ptr = nullptr; \
    } \
    while (0)

#define SAFE_DELETE_ARRAY(ptr) \
    do \
    { \
        delete[] ptr; \
        ptr = nullptr; \
    } \
    while (0)

// clang-format off
#define ASSERT(expr) \
    do { \
        if (!(expr)) \
        { \
            fprintf(stderr, "%s:%d assertion failed \"%s\"\n", __FILE__, __LINE__, #expr); \
            exit(1); \
        } \
    } \
    while (0)
// clang-format on

#define UNREACHABLE() \
    do \
    { \
        fprintf(stderr, "%s:%d unreachable\n", __FILE__, __LINE__); \
        exit(1); \
    } \
    while (0)
