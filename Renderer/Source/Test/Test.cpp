#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// https://www.reddit.com/r/C_Programming/comments/vfm3s7/comment/icwsoac/

#define TEST_HEADERS
#include "TestsAll.cpp"
#undef TEST_HEADERS

#define TEST(name) testName = name;

#define TEST_ASSERT(x) \
    do \
    { \
        testAssertion = #x; \
        testFile = __FILE__; \
        testLine = __LINE__; \
        if (x) \
        { \
            putchar('.'); \
        } \
        else \
        { \
            printf( \
                "\ntest failed at %s:%d\n    %s: %s\n", \
                testFile, \
                testLine, \
                testName, \
                testAssertion \
            ); \
            exit(1); \
        } \
    } \
    while (0)

int main(void)
{
    const char* testName = "";
    const char* testAssertion = "";
    const char* testFile = "";
    int testLine = 0;

#define TEST_SOURCE
#include "TestsAll.cpp"
#undef TEST_SOURCE

    printf("\ntests passed\n");

    return 0;
}
