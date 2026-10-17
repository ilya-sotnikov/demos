#if defined(TEST_HEADERS)

    #include "renderer/rhi/pool.hpp"
    #include "renderer/rhi/rhi.hpp"

#elif defined(TEST_SOURCE)

TEST("RHI Pool") {
    Pool<rhi::Texture, u32> pool;

    rhi::Texture h[3]{};
    u32* ptr{};

    h[0] = pool.create_handle(0);
    TEST_ASSERT(h[0].idx == 0);
    TEST_ASSERT(h[0].generation == 0);

    ptr = pool.get_ptr(h[0]);
    TEST_ASSERT(ptr);
    *ptr = 1337;
    ptr = pool.get_ptr(h[0]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 1337);

    h[1] = pool.create_handle(5);
    TEST_ASSERT(h[1].idx == 1);
    TEST_ASSERT(h[1].generation == 0);

    ptr = pool.get_ptr(h[1]);
    TEST_ASSERT(ptr);

    ptr = pool.get_ptr(h[0]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 1337);

    ptr = pool.get_ptr(h[1]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 5);

    pool.destroy_handle(h[0]);
    TEST_ASSERT(!pool.get_ptr(h[0]));

    h[2] = pool.create_handle(7);
    TEST_ASSERT(h[2].idx == 0);
    TEST_ASSERT(h[2].generation == 1);
    ptr = pool.get_ptr(h[2]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 7);

    h[0] = pool.create_handle(9);
    TEST_ASSERT(h[0].idx == 2);
    TEST_ASSERT(h[0].generation == 0);
    ptr = pool.get_ptr(h[0]);
    TEST_ASSERT(ptr);
    TEST_ASSERT(*ptr == 9);
}

#endif
