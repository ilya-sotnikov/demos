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
struct Arena {
    enum {
        FLAG_NONE = 0,
        FLAG_NO_ZERO = (1 << 0),
    };
    uchar* m_buffer;
    ptrdiff_t m_buffer_size;
    ptrdiff_t m_current_offset; // Relative to &m_buffer[0].
    ptrdiff_t m_max_offset;
    char m_name[32];

    void init(void* backing_buffer, ptrdiff_t size, const char* name = nullptr);
    void init(ptrdiff_t size, const char* name = nullptr); // Exits on allocation failure.
    void* alloc(ptrdiff_t size, ptrdiff_t align, int flags = FLAG_NONE);
    void* alloc_or_die(ptrdiff_t size, ptrdiff_t align, int flags = FLAG_NONE);
    void free_all();
    void free_buffer();

    template<typename T>
    T* alloc(ptrdiff_t count, int flags = FLAG_NONE) {
        return static_cast<T*>(alloc(ptrdiff_t(count) * sizeof(T), alignof(T), flags));
    }
    template<typename T>
    T* alloc_or_die(ptrdiff_t count, int flags = FLAG_NONE) {
        return static_cast<T*>(alloc_or_die(count * ptrdiff_t(sizeof(T)), alignof(T), flags));
    }
};
