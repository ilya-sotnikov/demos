#if defined(TEST_HEADERS)

#include "../Arena.hpp"

#elif defined(TEST_SOURCE)

TEST("Arena")
{
    Arena arena;
    int* res;
    int cmp[] = {1337, -1, 282, 222};
    int cmpCount = ARRAY_SIZE(cmp);

    arena.Init(128);
    DEFER(arena.FreeBuffer());

    res = static_cast<int*>(arena.Alloc(sizeof(int), sizeof(int)));
    TEST_ASSERT(res);
    *res = cmp[0];
    TEST_ASSERT(!memcmp(arena.mBuffer, cmp, sizeof(int)));

    res = static_cast<int*>(arena.Alloc(sizeof(int), sizeof(int)));
    TEST_ASSERT(res);
    *res = cmp[1];
    TEST_ASSERT(!memcmp(arena.mBuffer, cmp, sizeof(int) * 2));

    arena.FreeAll();
    TEST_ASSERT(arena.mCurrentOffset == 0);

    res = static_cast<int*>(arena.Alloc(sizeof(cmp), sizeof(int)));
    TEST_ASSERT(res);
    for (int i = 0; i < cmpCount; ++i)
    {
        res[i] = cmp[i];
    }
    TEST_ASSERT(!memcmp(arena.mBuffer, cmp, sizeof(cmp)));

    arena.FreeAll();
    res = static_cast<int*>(arena.Alloc(arena.mBufferSize, 1));
    TEST_ASSERT(res);

    arena.FreeAll();
    res = static_cast<int*>(arena.Alloc(arena.mBufferSize, 16));
    TEST_ASSERT(res);

    arena.FreeAll();
    res = static_cast<int*>(arena.Alloc(arena.mBufferSize + 1, 1));
    TEST_ASSERT(!res);
}

#endif
