#pragma once

#include "Common.hpp"

#include <string.h>

namespace Utils
{

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
