#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// https://www.reddit.com/r/C_Programming/comments/vfm3s7/comment/icwsoac/

#define TEST_HEADERS
#include "TestsAll.cpp"
#undef TEST_HEADERS

#define TEST(name) test_name = name;

#define TEST_ASSERT(x) \
    do \
    { \
        test_assertion = #x; \
        test_file = __FILE__; \
        test_line = __LINE__; \
        if (x) \
        { \
            putchar('.'); \
        } \
        else \
        { \
            printf( \
                "\nTest failed at %s:%d\n    %s: %s\n", \
                test_file, \
                test_line, \
                test_name, \
                test_assertion \
            ); \
            exit(1); \
        } \
    } \
    while (0)

int main(void)
{
    const char* test_name = "";
    const char* test_assertion = "";
    const char* test_file = "";
    int test_line = 0;

#define TEST_SOURCE
#include "TestsAll.cpp"
#undef TEST_SOURCE

    printf("\nTests passed\n");

    return 0;
}
