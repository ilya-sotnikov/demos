#pragma once

#include "common.hpp"
#include "arena.hpp"

static const int TEMP_ALLOCATOR_SIZE = 65536;

struct TempAllocator {
    explicit TempAllocator(const char* name);
    TempAllocator(const TempAllocator& other);
    TempAllocator(const TempAllocator& other, const char* name);

    TempAllocator(Arena* arena, const char* name);

    TempAllocator& operator=(const TempAllocator& other);

    void* allocate(size_t n, int flags = 0);
    void* allocate(size_t n, size_t alignment, size_t offset, int flags = 0);
    void deallocate(void* p, size_t n);

    const char* get_name() const;
    void set_name(const char* name);

    const char* m_name = nullptr;
    Arena* m_arena = nullptr;
};

bool operator==(const TempAllocator& lhs, const TempAllocator& rhs);
bool operator!=(const TempAllocator& lhs, const TempAllocator& rhs);

struct TempScope {
    TempScope();
    ~TempScope();
};

void init_temp_allocator();
TempAllocator get_temp_allocator();

#define TEMP_ALLOCATOR(temp_) \
    const TempScope temp_scope_{}; \
    TempAllocator temp_ = get_temp_allocator();