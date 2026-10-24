#include "temp_allocator.hpp"

#include "utils.hpp"
#include "math/utils.hpp"

#include <EASTL/vector.h>

TempAllocator::TempAllocator(const char* name): m_name{name}, m_arena{nullptr} {}

TempAllocator::TempAllocator(const TempAllocator& other):
    m_name{other.m_name},
    m_arena{other.m_arena} {}

TempAllocator::TempAllocator(const TempAllocator& other, const char* name):
    m_name{name},
    m_arena{other.m_arena} {}

TempAllocator::TempAllocator(Arena* arena, const char* name): m_name{name}, m_arena{arena} {}

TempAllocator& TempAllocator::operator=(const TempAllocator& other) {
    m_name = other.m_name;
    m_arena = other.m_arena;

    return *this;
}

void* TempAllocator::allocate(size_t n, int flags) {
    UNUSED(flags);

    return m_arena->alloc_or_die(ptrdiff_t(n), EASTL_ALLOCATOR_MIN_ALIGNMENT, Arena::FLAG_NO_ZERO);
}

void* TempAllocator::allocate(size_t n, size_t alignment, size_t offset, int flags) {
    ASSERT(offset == 0); // TODO
    UNUSED(flags);

    alignment = math::max(alignment, size_t(EASTL_ALLOCATOR_MIN_ALIGNMENT));

    return m_arena->alloc_or_die(ptrdiff_t(n), ptrdiff_t(alignment), Arena::FLAG_NO_ZERO);
}

void TempAllocator::deallocate(void* p, size_t n) {
    UNUSED(p);
    UNUSED(n);
}

const char* TempAllocator::get_name() const {
    return m_name;
}

void TempAllocator::set_name(const char* name) {
    m_name = name;
}

bool operator==(const TempAllocator& lhs, const TempAllocator& rhs) {
    return lhs.m_arena == rhs.m_arena;
}

bool operator!=(const TempAllocator& lhs, const TempAllocator& rhs) {
    return lhs.m_arena != rhs.m_arena;
}

// TODO: Allocate in global heap if on alloc failure.
struct Context {
    eastl::vector<Arena*> arenas;
    int scope;
    int max_scope;
};

static thread_local Context s_ctx;

TempAllocator get_temp_allocator() {
    DEBUG_ASSERT(s_ctx.scope > 0);

    return {s_ctx.arenas[s_ctx.scope - 1], "temp_allocator"};
}

void start_temp() {
    const int curr_scope_idx = s_ctx.scope;
    ++s_ctx.scope;
    if (s_ctx.scope > s_ctx.max_scope) {
        s_ctx.arenas.resize(s_ctx.scope);
        s_ctx.arenas[curr_scope_idx] = new Arena{};
        s_ctx.arenas[curr_scope_idx]->init(TEMP_ALLOCATOR_SIZE);
        s_ctx.max_scope = s_ctx.scope;
    }
    s_ctx.arenas[curr_scope_idx]->free_all();
}

void end_temp() {
    DEBUG_ASSERT(s_ctx.scope > 0);
    --s_ctx.scope;
}
