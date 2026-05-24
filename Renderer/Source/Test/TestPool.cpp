#if defined(TEST_HEADERS)

#include "Renderer/RHI/RHI.hpp"
#include "Renderer/RHI/Pool.hpp"

#elif defined(TEST_SOURCE)

TEST("RHI Pool")
{
    Pool<RHI::TextureHandle, u32> pool;

    RHI::TextureHandle h[3]{};
    u32* ptr{};

    h[0] = pool.CreateHandle(0);
    TEST_ASSERT(h[0].idx == 0);
    TEST_ASSERT(h[0].generation == 0);

    ptr = pool.GetPtr(h[0]);
    TEST_ASSERT(ptr);
    *ptr = 1337;
    ptr = pool.GetPtr(h[0]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 1337);

    h[1] = pool.CreateHandle(5);
    TEST_ASSERT(h[1].idx == 1);
    TEST_ASSERT(h[1].generation == 0);

    ptr = pool.GetPtr(h[1]);
    TEST_ASSERT(ptr);

    ptr = pool.GetPtr(h[0]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 1337);

    ptr = pool.GetPtr(h[1]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 5);

    pool.DestroyHandle(h[0]);
    TEST_ASSERT(!pool.GetPtr(h[0]));

    h[2] = pool.CreateHandle(7);
    TEST_ASSERT(h[2].idx == 0);
    TEST_ASSERT(h[2].generation == 1);
    ptr = pool.GetPtr(h[2]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 7);

    h[0] = pool.CreateHandle(9);
    TEST_ASSERT(h[0].idx == 2);
    TEST_ASSERT(h[0].generation == 0);
    ptr = pool.GetPtr(h[0]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 9);
}

#endif
