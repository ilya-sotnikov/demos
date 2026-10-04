#pragma once

#include "Common.hpp"
#include "Math/Types.hpp"

#include <stdlib.h>
#include <string.h>

namespace utils {

struct FileData {
    void* data;
    long size;
};

// Gives ownership, call free()
FileData file_read(const char* path);

// Print an error message and exit(1).
void* xmalloc(size_t size);
// Print an error message and exit(1).
void* xrealloc(void* ptr, size_t new_size);

// Yoinked from OpenBSD, sane C-style string handling.
size_t strlcpy(char* dst, const char* src, size_t dsize);
size_t strlcat(char* dst, const char* src, size_t dsize);

// Type-punning through memcpy to avoid strict aliasing violation.
template<typename To, typename From>
To bit_cast(From src) {
    static_assert(sizeof(To) == sizeof(From));
    To dst;
    memcpy(&dst, &src, sizeof(To));
    return dst;
}

struct FpsCounter {
    void update(f64& fps, f64 time);

    f64 m_prev_time;
    u32 m_frame_count;
};

struct MemoryDivider {
    void* m_memory;
    ptrdiff_t m_current_offset;
    ptrdiff_t m_size;

    void init(void* memory, ptrdiff_t size);
    MemorySlice take(ptrdiff_t bytes);
    MemorySlice take_rest();
};

template<typename T>
T align_up_pow2(T val, T alignment) {
    DEBUG_ASSERT((alignment & (alignment - 1)) == 0);
    return (val + alignment - 1) & ~(alignment - 1);
}

inline u32 get_mip_level(u32 width, u32 height) {
    u32 mip_level = 1;

    while (width > 1 || height > 1) {
        ++mip_level;
        width /= 2;
        height /= 2;
    }

    return mip_level;
}

// Approximations.
f32 linear_to_srgb(f32 color, f32 gamma = 2.2f);
f32 srgb_to_linear(f32 color, f32 gamma = 2.2f);
Vec3 linear_to_srgb(Vec3 color, f32 gamma = 2.2f);
Vec3 srgb_to_linear(Vec3 color, f32 gamma = 2.2f);

} // namespace utils

// Free and ptr = nullptr to avoid accidental double free.
#define SAFE_FREE(ptr) \
    do { \
        free(ptr); \
        ptr = nullptr; \
    } while (0)

#define SAFE_DELETE(ptr) \
    do { \
        delete ptr; \
        ptr = nullptr; \
    } while (0)

#define SAFE_DELETE_ARRAY(ptr) \
    do { \
        delete[] ptr; \
        ptr = nullptr; \
    } while (0)

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
    do { \
        fprintf(stderr, "%s:%d unreachable\n", __FILE__, __LINE__); \
        exit(1); \
    } while (0)
