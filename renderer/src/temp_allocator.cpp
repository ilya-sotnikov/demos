#include "temp_allocator.hpp"

#include "utils.hpp"

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

    return m_arena->alloc_or_die(ptrdiff_t(n), Arena::FLAG_NO_ZERO);
}

void* TempAllocator::allocate(size_t n, size_t alignment, size_t offset, int flags) {
    ASSERT(offset == 0); // TODO
    UNUSED(flags);

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

// TODO: Thread local.
// TODO: Arena stack for nested scopes.
// TODO: Allocate in global heap if on alloc failure.
static Arena s_arena;
static int s_scope_idx;

void init_temp_allocator() {
    s_arena.init(TEMP_ALLOCATOR_SIZE);
}

TempAllocator get_temp_allocator() {
    const TempAllocator result{&s_arena, "temp_allocator"};
    return result;
}

TempScope::TempScope() {
    ++s_scope_idx;
    s_arena.free_all();
}

TempScope::~TempScope() {
    DEBUG_ASSERT(s_scope_idx == 1);
    --s_scope_idx;
}
