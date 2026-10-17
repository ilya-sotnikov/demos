#if defined(TEST_HEADERS)

    #include "arena.hpp"

#elif defined(TEST_SOURCE)

TEST("Arena") {
    Arena arena;
    int* res;
    int cmp[] = {1337, -1, 282, 222};
    int cmp_count = ARRAY_SIZE(cmp);

    arena.init(128);
    DEFER(arena.free_buffer());

    res = static_cast<int*>(arena.alloc(sizeof(int), sizeof(int)));
    TEST_ASSERT(res);
    *res = cmp[0];
    TEST_ASSERT(!memcmp(arena.m_buffer, cmp, sizeof(int)));

    res = static_cast<int*>(arena.alloc(sizeof(int), sizeof(int)));
    TEST_ASSERT(res);
    *res = cmp[1];
    TEST_ASSERT(!memcmp(arena.m_buffer, cmp, sizeof(int) * 2));

    arena.free_all();
    TEST_ASSERT(arena.m_current_offset == 0);

    res = static_cast<int*>(arena.alloc(sizeof(cmp), sizeof(int)));
    TEST_ASSERT(res);
    for (int i = 0; i < cmp_count; ++i) {
        res[i] = cmp[i];
    }
    TEST_ASSERT(!memcmp(arena.m_buffer, cmp, sizeof(cmp)));

    arena.free_all();
    res = static_cast<int*>(arena.alloc(arena.m_buffer_size, 1));
    TEST_ASSERT(res);

    arena.free_all();
    res = static_cast<int*>(arena.alloc(arena.m_buffer_size, 16));
    TEST_ASSERT(res);

    arena.free_all();
    res = static_cast<int*>(arena.alloc(arena.m_buffer_size + 1, 1));
    TEST_ASSERT(!res);
}

#endif
