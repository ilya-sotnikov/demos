#include "Arena.hpp"

#include "utils.hpp"

#include <stdio.h>

static bool is_power_of_two(ptrdiff_t x) {
    return (x & (x - 1)) == 0;
}

static ptrdiff_t align_forward(ptrdiff_t ptr, ptrdiff_t align) {
    DEBUG_ASSERT(is_power_of_two(align));
    (void)is_power_of_two(0);

    ptrdiff_t aligned_ptr = ptr;
    const ptrdiff_t modulo = aligned_ptr & (align - 1);

    if (modulo != 0) {
        aligned_ptr += align - modulo;
    }

    return aligned_ptr;
}

void Arena::init(void* backing_buffer, ptrdiff_t size, const char* name) {
    DEBUG_ASSERT(backing_buffer);
    DEBUG_ASSERT(size > 0);

    m_buffer = static_cast<uchar*>(backing_buffer);
    m_buffer_size = size;
    m_current_offset = 0;
    m_max_offset = 0;
    utils::strlcpy(m_name, name ? name : "unnamed", sizeof(m_name));
}

void Arena::init(ptrdiff_t size, const char* name) {
    DEBUG_ASSERT(size > 0);

    m_buffer = static_cast<uchar*>(utils::xmalloc(size_t(size)));
    m_buffer_size = size;
    m_current_offset = 0;
    m_max_offset = 0;
    utils::strlcpy(m_name, name ? name : "unnamed", sizeof(m_name));
}

void* Arena::alloc(ptrdiff_t size, ptrdiff_t align, int flags) {
    const ptrdiff_t curr_ptr = reinterpret_cast<ptrdiff_t>(m_buffer) + m_current_offset;
    ptrdiff_t offset = align_forward(curr_ptr, align);
    offset -= reinterpret_cast<ptrdiff_t>(m_buffer);

    if ((size > PTRDIFF_MAX - offset) || (offset + size > m_buffer_size)) {
        return nullptr;
    }

    void* const ptr = &m_buffer[offset];
    m_current_offset = offset + size;
    m_max_offset = m_current_offset;
    if (!(flags & FLAG_NO_ZERO)) {
        memset(ptr, 0, size_t(size));
    }

    return ptr;
}

void* Arena::alloc_or_die(ptrdiff_t size, ptrdiff_t align, int flags) {
    void* const ret = alloc(size, align, flags);
    if (!ret) {
        fprintf(
            stderr,
            "Arena::alloc failed (size = %td, align = %td, name = %s)\n",
            size,
            align,
            m_name
        );
        exit(1);
    }
    return ret;
}

void Arena::free_all() {
    m_current_offset = 0;
}

void Arena::free_buffer() {
    SAFE_FREE(m_buffer);
}
