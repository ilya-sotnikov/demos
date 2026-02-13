#pragma once

#include "Common.hpp"

#include <stdlib.h>

// Great series of articles:
// https://www.gingerbill.org/series/memory-allocation-strategies/

// TODO: custom allocators in ISO C/C++ violate strict aliasing, AFAIK there's nothing we can do,
// at least compilers don't break the code. It seems that placement new solves this problem,
// but only since C++20:
// https://nullprogram.com/blog/2025/09/30/
// https://stackoverflow.com/a/75418614
// NOTE: another thing is that, according to the spec, malloc doesn't even start a lifetime before
// C++20, so it's UB, once again, compilers don't break the code.
struct Arena
{
    enum
    {
        FlagNone = 0,
        FlagNoZero = (1 << 0),
    };
    uchar* mBuffer;
    ptrdiff_t mBufferSize;
    ptrdiff_t mCurrentOffset; // Relative to &mBuffer[0].
    ptrdiff_t mMaxOffset;
    char mName[32];

    void Init(void* backingBuffer, ptrdiff_t size, const char* name = nullptr);
    void Init(ptrdiff_t size, const char* name = nullptr); // Exits on allocation failure.
    void* Alloc(ptrdiff_t size, ptrdiff_t align, int flags = FlagNone);
    void* AllocOrDie(ptrdiff_t size, ptrdiff_t align, int flags = FlagNone);
    void FreeAll();
    void FreeBuffer();

    template <typename T>
    T* Alloc(ptrdiff_t count, int flags = FlagNone)
    {
        return static_cast<T*>(Alloc(static_cast<ptrdiff_t>(count) * sizeof(T), alignof(T), flags));
    }
    template <typename T>
    T* AllocOrDie(ptrdiff_t count, int flags = FlagNone)
    {
        return static_cast<T*>(
            AllocOrDie(count * static_cast<ptrdiff_t>(sizeof(T)), alignof(T), flags)
        );
    }
};

// For static resources (whole program lifetime).
inline Arena gArenaStatic;
// For resources that are reset on restart (right now it's physics in ResetWorld).
inline Arena gArenaReset;
// For per-frame resources, reset at the beginning of each frame.
inline Arena gArenaFrame;
// For swapchain resources that are recreated upon resizing.
inline Arena gArenaSwapchain;

inline constexpr Arena* gArenas[] = {
    &gArenaStatic,
    &gArenaReset,
    &gArenaFrame,
    &gArenaSwapchain,
};
