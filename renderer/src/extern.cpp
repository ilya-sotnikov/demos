#include "common.hpp"
#include <utils.hpp>

#define VMA_IMPLEMENTATION
#include "renderer/rhi/vulkan/common.hpp"

#include <cstdlib>

// For EASTL.
void* operator new[](
    size_t size,
    const char* p_name,
    int flags,
    unsigned debug_flags,
    const char* file,
    int line
) {
    UNUSED(p_name);
    UNUSED(flags);
    UNUSED(debug_flags);
    UNUSED(file);
    UNUSED(line);

    return new uint8_t[size];
}

void* operator new[](
    size_t size,
    size_t alignment,
    size_t alignment_offset,
    const char* p_name,
    int flags,
    unsigned debug_flags,
    const char* file,
    int line
) {
    UNUSED(p_name);
    UNUSED(flags);
    UNUSED(debug_flags);
    UNUSED(file);
    UNUSED(line);

    ASSERT(alignment <= __STDCPP_DEFAULT_NEW_ALIGNMENT__); // TODO
    ASSERT(alignment_offset == 0); // TODO

    return new uint8_t[size];
}